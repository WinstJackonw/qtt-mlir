#pragma once

#include <memory>

#include "mlir/Pass/Pass.h"

namespace mlir {
/// Create the pass that lowers QTT operations and algebraic value types to
/// the opaque, LLVM-friendly QREP representation.
std::unique_ptr<Pass> createQTTToQREPConversionPass();

/// Register the QTT-to-QREP pass with MLIR's pass registry.
void registerQTTToQREPConversionPass();
} // namespace mlir
