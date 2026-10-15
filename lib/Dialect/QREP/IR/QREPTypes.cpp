#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include "llvm/ADT/TypeSwitch.h"

#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOpsTypes.cpp.inc"

using namespace mlir;

LogicalResult qrep::OwnType::verify(function_ref<InFlightDiagnostic()> emitError, FlatSymbolRefAttr,
                                    Type constructor) {
  if (constructor && !isa<qtt::CtorType>(constructor))
    return emitError() << "constructor refinement must be a QTT CtorType";
  return success();
}

LogicalResult qrep::ViewType::verify(function_ref<InFlightDiagnostic()> emitError,
                                     FlatSymbolRefAttr, Type constructor) {
  return OwnType::verify(emitError, {}, constructor);
}

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
