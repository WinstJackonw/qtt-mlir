#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h"
#include "mlir/Conversion/ArithToLLVM/ArithToLLVM.h"
#include "mlir/Conversion/ControlFlowToLLVM/ControlFlowToLLVM.h"
#include "mlir/Conversion/FuncToLLVM/ConvertFuncToLLVM.h"
#include "mlir/Conversion/LLVMCommon/ConversionTarget.h"
#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace {
// Unit constructors have a value at the representation layer, but no payload.
// An empty LLVM struct keeps that value usable in function and CFG signatures.
class UnitValueLowering : public OpConversionPattern<UnrealizedConversionCastOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult matchAndRewrite(UnrealizedConversionCastOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    if (op.getNumOperands() != 0 || op.getNumResults() != 1 ||
        !isa<NoneType>(op.getResult(0).getType()))
      return failure();
    rewriter.replaceOpWithNewOp<LLVM::ZeroOp>(
        op, getTypeConverter()->convertType(op.getResult(0).getType()));
    return success();
  }
};

class StorageToLLVM : public PassWrapper<StorageToLLVM, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(StorageToLLVM)
  StringRef getArgument() const final { return "lower-qtt-storage-to-llvm"; }
  StringRef getDescription() const final { return "Lower QTT physical storage to LLVM"; }
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<LLVM::LLVMDialect>();
  }
  void runOnOperation() override {
    auto module = getOperation();
    // Layouts are compile-time templates. Only discard them after all runtime
    // representation operations have been resolved by lower-qrep.
    bool unresolved = false;
    module.walk([&](Operation *op) {
      if (isa<qrep::LayoutOp>(op) || op->getParentOfType<qrep::LayoutOp>())
        return;
      auto ns = op->getName().getDialectNamespace();
      if (ns == "qtt" || ns == "qrep") {
        op->emitError("requires QTT/QREP storage lowering before LLVM conversion");
        unresolved = true;
      }
    });
    if (unresolved)
      return signalPassFailure();
    for (auto layout : llvm::make_early_inc_range(module.getOps<qrep::LayoutOp>()))
      layout.erase();
    module.walk([](Operation *op) { op->removeAttr("qrep.layout"); });

    LLVMTypeConverter converter(&getContext());
    converter.addConversion(
        [&](NoneType) -> Type { return LLVM::LLVMStructType::getLiteral(&getContext(), {}); });
    RewritePatternSet patterns(&getContext());
    patterns.add<UnitValueLowering>(converter, &getContext());
    populateFuncToLLVMConversionPatterns(converter, patterns);
    arith::populateArithToLLVMConversionPatterns(converter, patterns);
    cf::populateControlFlowToLLVMConversionPatterns(converter, patterns);
    LLVMConversionTarget target(getContext());
    target.addLegalOp<ModuleOp>();
    target.addIllegalOp<UnrealizedConversionCastOp>();
    if (failed(applyFullConversion(module, target, std::move(patterns))))
      signalPassFailure();
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createQTTStorageToLLVMConversionPass() {
  return std::make_unique<StorageToLLVM>();
}
void mlir::registerQTTStorageToLLVMConversionPass() { PassRegistration<StorageToLLVM>(); }
void mlir::buildQTTToLLVMPipeline(OpPassManager &pm) {
  pm.addPass(createQTTToQREPConversionPass());
  pm.addPass(createQREPToStorageConversionPass());
  pm.addPass(createSCFToControlFlowPass());
  pm.addPass(createQTTStorageToLLVMConversionPass());
}
