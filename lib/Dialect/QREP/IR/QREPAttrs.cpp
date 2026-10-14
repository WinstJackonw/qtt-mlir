#include "QTTMLIR/Dialect/QREP/IR/QREPAttrs.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPEnums.cpp.inc"

#define GET_ATTRDEF_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPAttrs.cpp.inc"

void mlir::qrep::QREPDialect::initializeAttributes() {
  addAttributes<
#define GET_ATTRDEF_LIST
#include "QTTMLIR/Dialect/QREP/IR/QREPAttrs.cpp.inc"
      >();
}
