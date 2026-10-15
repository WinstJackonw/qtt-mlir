#pragma once

#include "mlir/Pass/Pass.h"
#include <memory>

namespace mlir {
/// Lower QTT functions to func.func and constructor decision trees, retaining
/// QTT value types. Default-only functions do not emit a qtt.case.
std::unique_ptr<Pass> createQTTFunctionLoweringPass();
void registerQTTFunctionLoweringPass();
/// Lower QTT by instantiating methods of each function's qrep.layout symbol.
/// This pass contains no Nat encoding or storage policy.
std::unique_ptr<Pass> createQTTToQREPConversionPass();
void registerQTTToQREPConversionPass();
} // namespace mlir
