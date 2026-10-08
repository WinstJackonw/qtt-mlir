#include "mlir/IR/Builders.h"
#include "mlir/Support/LLVM.h"
#include <QTTMLIR/Dialect/QTT/IR/QTTTypes.h>

#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"

#define GET_OP_CLASSES
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.cpp.inc"

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
