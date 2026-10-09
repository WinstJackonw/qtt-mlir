#include "mlir/IR/Builders.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Support/LLVM.h"
#include <QTTMLIR/Dialect/QTT/IR/QTTTypes.h>

#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"

#define GET_OP_CLASSES
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.cpp.inc"

namespace {
using namespace mlir;
using namespace mlir::qtt;

// Only look through a widening conversion. In particular, never cancel an
// ADT -> constructor -> ADT chain, which could hide a failing narrowing cast.
Value getWidenedConstructor(Value value) {
  Value input;
  if (auto upcast = value.getDefiningOp<UpcastOp>())
    input = upcast.getInput();
  else if (auto cast = value.getDefiningOp<CastOp>())
    input = cast.getInput();
  if (!input)
    return {};
  auto ctor = dyn_cast<CtorType>(input.getType());
  if (!ctor || ctor.getParent() != value.getType())
    return {};
  return input;
}

struct UnpackConstruct : OpRewritePattern<UnpackOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(UnpackOp op, PatternRewriter &rewriter) const override {
    auto construct = op.getInput().getDefiningOp<ConstructOp>();
    if (!construct)
      return failure();
    rewriter.replaceOp(op, construct.getArgs());
    return success();
  }
};

struct Reconstruct : OpRewritePattern<ConstructOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(ConstructOp op, PatternRewriter &rewriter) const override {
    // A nullary construction provides no source value to reconstruct.
    if (op.getArgs().empty())
      return failure();
    auto unpack = op.getArgs().front().getDefiningOp<UnpackOp>();
    if (!unpack || unpack.getInput().getType() != op.getResult().getType() ||
        op.getArgs().size() != unpack.getNumResults())
      return failure();
    for (auto [argument, result] : llvm::zip(op.getArgs(), unpack.getResults()))
      if (argument != result)
        return failure();
    rewriter.replaceOp(op, unpack.getInput());
    return success();
  }
};

struct SimplifyCast : OpRewritePattern<CastOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(CastOp op, PatternRewriter &rewriter) const override {
    if (op.getInput().getType() == op.getResult().getType()) {
      rewriter.replaceOp(op, op.getInput());
      return success();
    }
    Value ctor = getWidenedConstructor(op.getInput());
    if (!ctor || ctor.getType() != op.getResult().getType())
      return failure();
    rewriter.replaceOp(op, ctor);
    return success();
  }
};

struct SelectKnownCase : OpRewritePattern<CaseOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(CaseOp op, PatternRewriter &rewriter) const override {
    Value ctor = getWidenedConstructor(op.getInput());
    if (!ctor)
      return failure();
    for (Region &region : op.getCases()) {
      Block &block = region.front();
      if (block.getArgument(0).getType() != ctor.getType())
        continue;
      auto yield = cast<YieldOp>(block.getTerminator());
      rewriter.inlineBlockBefore(&block, op, ValueRange{ctor});
      SmallVector<Value> results(yield.getValues());
      rewriter.eraseOp(yield);
      rewriter.replaceOp(op, results);
      return success();
    }
    return failure();
  }
};
} // namespace

void mlir::qtt::ConstructOp::getCanonicalizationPatterns(RewritePatternSet &patterns,
                                                       MLIRContext *context) {
  patterns.add<Reconstruct>(context);
}
void mlir::qtt::UnpackOp::getCanonicalizationPatterns(RewritePatternSet &patterns,
                                                    MLIRContext *context) {
  patterns.add<UnpackConstruct>(context);
}
void mlir::qtt::CastOp::getCanonicalizationPatterns(RewritePatternSet &patterns,
                                                  MLIRContext *context) {
  patterns.add<SimplifyCast>(context);
}
void mlir::qtt::CaseOp::getCanonicalizationPatterns(RewritePatternSet &patterns,
                                                  MLIRContext *context) {
  patterns.add<SelectKnownCase>(context);
}

mlir::LogicalResult mlir::qtt::ConstructOp::verify() {
  auto ctor = llvm::dyn_cast<mlir::qtt::CtorType>(getResult().getType());

  if (!ctor)
    return emitError() << "result must be CtorTYpe";

  llvm::ArrayRef<mlir::Type> expected = ctor.getPayload();

  if (expected.size() != getArgs().size()) {
    return emitOpError() << "expected " << expected.size() << " constructor arguments, got "
                         << getArgs().size();
  }

  for (auto [operand, expectedType] : llvm::zip(getArgs(), expected)) {
    if (operand.getType() != expectedType) {
      return emitOpError() << "constructor payload type mismatch: "
                           << "expected " << expectedType << ", got " << operand.getType();
    }
  }

  return mlir::success();
}

mlir::LogicalResult mlir::qtt::UnpackOp::verify() {
  auto ctor = llvm::dyn_cast<mlir::qtt::CtorType>(getInput().getType());

  if (!ctor)
    return emitOpError() << "input must be CtorType";

  llvm::ArrayRef<mlir::Type> expected = ctor.getPayload();

  if (expected.size() != getArgs().size()) {
    return emitOpError() << "expected " << expected.size() << " constructor payloads, got "
                         << getArgs().size();
  }

  for (auto [result, expectedType] : llvm::zip(getArgs(), expected)) {
    if (result.getType() != expectedType) {
      return emitOpError() << "constructor payload type mismatch: "
                           << "expected " << expectedType << ", got " << result.getType();
    }
  }

  return mlir::success();
}

mlir::LogicalResult mlir::qtt::UpcastOp::verify() {
  auto source = llvm::dyn_cast<CtorType>(getInput().getType());

  auto target = llvm::dyn_cast<ADTType>(getResult().getType());

  if (!source || !target)
    return emitOpError() << "expected variant -> ADT";

  if (source.getParent() != target)
    return emitOpError() << source << " is not a subtype of " << target;

  return mlir::success();
}

mlir::LogicalResult mlir::qtt::CastOp::verify() {
  Type from = getInput().getType();
  Type to = getResult().getType();

  if (isSubType(from, to))
    return success();

  if (isSubType(to, from))
    return success();

  return emitOpError() << "types are unrelated: " << from << " and " << to;
}

mlir::LogicalResult mlir::qtt::CaseOp::verify() {
  auto adt = llvm::dyn_cast<ADTType>(getInput().getType());

  if (!adt)
    return emitOpError("input must be ADTType");

  if (!adt.isInitialized())
    return emitOpError("input ADT must be fully defined");

  llvm::DenseSet<Type> expected;
  llvm::DenseSet<Type> actual;

  for (const auto &def : adt.getCtors()) {
    expected.insert(mlir::qtt::CtorType::get(adt, def.getName(), def.getPayload()));
  }

  for (Region &region : getCases()) {
    if (!llvm::hasSingleElement(region))
      return emitOpError("each case region must contain one entry block");

    Block &block = region.front();

    if (block.empty() || !isa<YieldOp>(block.back()))
      return emitOpError("each case region must end with qtt.yield");

    if (block.getNumArguments() != 1)
      return emitOpError("each case region must have exactly one variant argument");

    Type argType = block.getArgument(0).getType();

    auto variant = dyn_cast<CtorType>(argType);

    if (!variant)
      return emitOpError("case argument must have qtt.variant type");

    if (variant.getParent() != adt)
      return emitOpError("case variant belongs to another ADT");

    if (!actual.insert(variant).second)
      return emitOpError("duplicate variant case");
  }

  if (actual != expected)
    return emitOpError("case is not exhaustive");

  return success();
}

mlir::LogicalResult mlir::qtt::YieldOp::verify() {
  auto parent = dyn_cast_or_null<CaseOp>((*this)->getParentOp());
  if (!parent)
    return emitOpError("must be directly nested in qtt.case");
  if (getNumOperands() != parent.getNumResults())
    return emitOpError("operand count must match case result count");
  for (auto [value, result] : llvm::zip(getValues(), parent.getResults())) {
    if (value.getType() != result.getType())
      return emitOpError("operand type must match case result type: expected ")
             << result.getType() << ", got " << value.getType();
  }
  return success();
}

mlir::ParseResult mlir::qtt::CaseOp::parse(mlir::OpAsmParser &parser,
                                           mlir::OperationState &result) {
  OpAsmParser::UnresolvedOperand input;
  Type inputType;
  if (parser.parseOperand(input) || parser.parseColonType(inputType) ||
      parser.resolveOperand(input, inputType, result.operands) ||
      parser.parseOptionalArrowTypeList(result.types))
    return failure();
  auto adt = dyn_cast<ADTType>(inputType);
  if (!adt || !adt.isInitialized())
    return parser.emitError(parser.getCurrentLocation(), "expected a fully defined ADT input type");
  if (parser.parseLBrace())
    return failure();
  while (failed(parser.parseOptionalRBrace())) {
    std::string name;
    OpAsmParser::Argument argument;
    if (parser.parseKeywordOrString(&name) || parser.parseLParen() ||
        parser.parseArgument(argument) || parser.parseRParen())
      return failure();
    auto ctor = adt.lookupCtor(StringAttr::get(parser.getContext(), name));
    if (!ctor)
      return parser.emitError(parser.getCurrentLocation(), "unknown case constructor: ") << name;
    argument.type = ctor;
    if (parser.parseRegion(*result.addRegion(), argument))
      return failure();
  }
  return parser.parseOptionalAttrDict(result.attributes);
}

void mlir::qtt::CaseOp::print(mlir::OpAsmPrinter &printer) {
  printer << ' ' << getInput() << " : " << getInput().getType();
  if (getNumResults()) {
    printer << " -> (";
    llvm::interleaveComma(getResultTypes(), printer);
    printer << ')';
  }
  printer << " {";
  for (Region &region : getCases()) {
    Block &block = region.front();
    auto ctor = llvm::cast<CtorType>(block.getArgument(0).getType());

    printer << ' ';
    printer.printKeywordOrString(ctor.getName().getValue());
    printer << '(';
    printer.printRegionArgument(block.getArgument(0), {}, /*omitType=*/true);
    printer << ") ";
    printer.printRegion(region, /*printEntryBlockArgs=*/false);
  }
  printer << " }";
  printer.printOptionalAttrDict((*this)->getAttrs());
}
