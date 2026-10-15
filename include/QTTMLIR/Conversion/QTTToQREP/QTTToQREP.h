#pragma once

#include "mlir/Pass/Pass.h"
#include <memory>

namespace mlir {
/// Lower QTT functions to func.func and constructor decision trees, retaining
/// QTT value types. Default-only functions do not emit a qtt.case.
std::unique_ptr<Pass> createQTTFunctionLoweringPass();
void registerQTTFunctionLoweringPass();
/// Lower QTT to explicit QREP own/view values, primitives, and borrows.
std::unique_ptr<Pass> createQTTToQREPConversionPass();
void registerQTTToQREPConversionPass();
/// Resolve QREP own/view types and instantiate layout methods (lower-qrep).
std::unique_ptr<Pass> createQREPToStorageConversionPass();
void registerQREPToStorageConversionPass();
} // namespace mlir
