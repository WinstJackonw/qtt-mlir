#include "QTTMLIR/Dialect/QREP/IR/QREPDialect.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPOpsDialect.cpp.inc"

using namespace mlir;
using namespace mlir::qrep;

void QREPDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.cpp.inc"
      >();

  addTypes<
#define GET_TYPEDEF_LIST
#include "QTTMLIR/Dialect/QREP/IR/QREPOpsTypes.cpp.inc"
      >();
}
