#include "mlir/IR/Builders.h"
#include "mlir/Support/LLVM.h"

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

mlir::LogicalResult mlir::qtt::UpcastOp::verify() {
  auto source = llvm::dyn_cast<CtorType>(getInput().getType());

  auto target = llvm::dyn_cast<ADTType>(getResult().getType());

  if (!source || !target)
    return emitOpError() << "expected variant -> ADT";

  if (source.getParent() != target)
    return emitOpError() << source << " is not a subtype of " << target;

  return mlir::success();
}
