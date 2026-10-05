#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"
#include "QTTMLIR/Dialect/QTT/IR/TypeDetail.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace mlir::qtt;

#include "QTTMLIR/Dialect/QTT/IR/QTTOpsDialect.cpp.inc"

void QTTDialect::initialize() {
  addOperations<
#define GET_OP_LIST
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.cpp.inc"
      >();

  addTypes<
#define GET_TYPEDEF_LIST
#include "QTTMLIR/Dialect/QTT/IR/QTTOpsTypes.cpp.inc"
      >();
}
