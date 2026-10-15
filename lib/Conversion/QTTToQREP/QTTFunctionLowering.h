#pragma once
#include "mlir/IR/BuiltinOps.h"

namespace mlir {
LogicalResult lowerQTTFunctions(ModuleOp module);
}
