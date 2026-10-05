#pragma once

#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/TypeSupport.h"
#include "mlir/IR/Types.h"

#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"

namespace mlir {
namespace qtt {
namespace details {
struct ADTTypeStorage;
struct CtorTypeStorage;
} // namespace details
class ADTType;
class CtorType;
} // namespace qtt
} // namespace mlir

#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QTT/IR/QTTOpsTypes.h.inc"

namespace mlir {
namespace qtt {
bool isSubType(mlir::Type sub, mlir::Type super);
} // namespace qtt
} // namespace mlir
