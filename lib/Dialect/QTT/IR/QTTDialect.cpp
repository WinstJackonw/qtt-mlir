#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/DialectImplementation.h"
#include "llvm/ADT/TypeSwitch.h"

using namespace mlir;
using namespace qtt;

#include "QTTMLIR/Dialect/QTT/IR/QTTOpsDialect.cpp.inc"

// Place this inside QTTTypes.cpp later
#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QTT/IR/QTTOpsTypes.cpp.inc"

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
