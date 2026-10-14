#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOpsTypes.cpp.inc"

using namespace mlir;

qtt::ADTType qrep::getADT(Type source) {
  if (auto adt = dyn_cast<qtt::ADTType>(source))
    return adt;
  if (auto ctor = dyn_cast<qtt::CtorType>(source))
    return ctor.getParent();
  return {};
}

void qrep::QREPDialect::initializeTypes() {
  addTypes<
#define GET_TYPEDEF_LIST
#include "QTTMLIR/Dialect/QREP/IR/QREPOpsTypes.cpp.inc"
      >();
}
