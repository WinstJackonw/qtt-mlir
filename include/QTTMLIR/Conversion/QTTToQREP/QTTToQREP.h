#pragma once

#include "mlir/Pass/Pass.h"
#include <memory>

namespace mlir {
/// Lower QTT by instantiating methods of each function's qrep.layout symbol.
/// This pass contains no Nat encoding or storage policy.
std::unique_ptr<Pass> createQTTToQREPConversionPass();
void registerQTTToQREPConversionPass();
} // namespace mlir
