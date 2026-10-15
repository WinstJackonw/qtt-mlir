#pragma once

#include "QTTMLIR/Dialect/QREP/IR/QREPDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"
#include "mlir/IR/BuiltinAttributes.h"

#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOpsTypes.h.inc"

namespace mlir::qrep {
qtt::ADTType getADT(Type source);
} // namespace mlir::qrep
