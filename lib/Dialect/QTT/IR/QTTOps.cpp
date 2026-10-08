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

mlir::ParseResult mlir::qtt::CaseOp::parse(mlir::OpAsmParser &parser,
                                           mlir::OperationState &result) {
  return parser.parseGenericOperationAfterOpName(result);
}

void mlir::qtt::CaseOp::print(mlir::OpAsmPrinter &printer) {
  printer << ' ' << getInput() << " : " << getInput().getType() << " {";
  for (Region &region : getCases()) {
    Block &block = region.front();
    auto ctor = llvm::cast<CtorType>(block.getArgument(0).getType());

    printer << " !" << ctor.getName().getValue() << '(';
    printer.printRegionArgument(block.getArgument(0), {}, /*omitType=*/true);
    printer << ") ";
    printer.printRegion(region, /*printEntryBlockArgs=*/false);
  }
  printer << " }";
  printer.printOptionalAttrDict((*this)->getAttrs());
}
