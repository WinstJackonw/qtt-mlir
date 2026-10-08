#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace {

class QTTToQREPTypeConverter : public TypeConverter {
public:
  QTTToQREPTypeConverter() {
    addConversion([](Type type) -> Type { return type; });
  }
};

class LowerQTTToQREPPass : public PassWrapper<LowerQTTToQREPPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerQTTToQREPPass)

  StringRef getArgument() const final { return "lower-qtt-to-qrep"; }
  StringRef getDescription() const final {
    return "Lower QTT algebraic values to the explicit QREP representation";
  }

  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<qrep::QREPDialect>();
  }

  void runOnOperation() override {}
};

} // namespace

std::unique_ptr<Pass> mlir::createQTTToQREPConversionPass() {
  return std::make_unique<LowerQTTToQREPPass>();
}

void mlir::registerQTTToQREPConversionPass() { PassRegistration<LowerQTTToQREPPass>(); }
