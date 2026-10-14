#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/Func/Transforms/FuncConversions.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Dialect/SCF/Transforms/Patterns.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Transforms/DialectConversion.h"

using namespace mlir;

namespace {
class LayoutTypeConverter : public TypeConverter {
public:
  explicit LayoutTypeConverter(qrep::LayoutOp layout) {
    addConversion([layout](Type type) -> Type {
      auto selected = layout;
      return selected.getStorageFor(type);
    });
  }
};

// Instantiate the selected layout's primitive implementation.
static SmallVector<Value> instantiate(qrep::LayoutOp layout, qrep::ImplKind kind, qtt::CtorType ctor,
                                      ValueRange args, ConversionPatternRewriter &rewriter) {
  auto impl = qrep::lookupImpl(layout, kind, ctor);
  Block &body = impl.getBody().front();
  IRMapping mapping;
  mapping.map(body.getArguments(), args);
  for (Operation &op : body.without_terminator())
    rewriter.clone(op, mapping);
  SmallVector<Value> results;
  for (Value value : body.getTerminator()->getOperands())
    results.push_back(mapping.lookupOrDefault(value));
  return results;
}

class QTTLowering : public ConversionPattern {
public:
  QTTLowering(TypeConverter &converter, MLIRContext *ctx, qrep::LayoutOp layout)
      : ConversionPattern(converter, MatchAnyOpTypeTag(), 1, ctx), layout(layout) {}

  LogicalResult matchAndRewrite(Operation *op, ArrayRef<Value> args,
                                ConversionPatternRewriter &rewriter) const override {
    if (op->getName().getDialectNamespace() != "qtt")
      return failure();
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertTypes(op->getResultTypes(), results)))
      return failure();
    if (isa<qtt::ConstructOp>(op)) {
      auto ctor = cast<qtt::CtorType>(op->getResult(0).getType());
      rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Pack, ctor, args, rewriter));
    } else if (auto unpack = dyn_cast<qtt::UnpackOp>(op)) {
      auto ctor = cast<qtt::CtorType>(unpack.getInput().getType());
      rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Unpack, ctor, args, rewriter));
    } else if (isa<qtt::UpcastOp, qtt::CastOp>(op)) {
      Type from = op->getOperand(0).getType(), to = op->getResult(0).getType();
      // Compare source refinements, not storage types: equal physical types
      // need not have equal encodings (e.g. successor versus predecessor).
      if (from == to)
        rewriter.replaceOp(op, args);
      else if (auto ctor = dyn_cast<qtt::CtorType>(from))
        rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Upcast, ctor, args, rewriter));
      else
        rewriter.replaceOp(
            op, instantiate(layout, qrep::ImplKind::Downcast, cast<qtt::CtorType>(to), args, rewriter));
    } else if (auto match = dyn_cast<qtt::CaseOp>(op)) {
      // Clone the layout's CFG, preserving decoded SSA values and its choice
      // of control flow. execute_region only collects continuation results.
      auto implementation =
          cast<qrep::DiscriminateOp>(rewriter.clone(*qrep::lookupDiscriminate(layout)));
      auto branch = scf::ExecuteRegionOp::create(rewriter, op->getLoc(), results);
      Region &target = branch.getRegion();
      rewriter.inlineRegionBefore(implementation.getBody(), target, target.end());
      rewriter.eraseOp(implementation);
      TypeConverter::SignatureConversion entrySignature(1);
      entrySignature.remapInput(0, args);
      rewriter.applySignatureConversion(&target.front(), entrySignature);

      SmallVector<qrep::DispatchOp> exits;
      for (Block &block : target)
        if (auto dispatch = dyn_cast<qrep::DispatchOp>(block.back()))
          exits.push_back(dispatch);
      // One shared continuation per constructor, even if several discriminate exits
      // invoke it. Preserve source operations and their refinement types until
      // dialect conversion remaps the constructor argument to its storage.
      DenseMap<Type, Block *> continuations;
      for (Region &region : match.getCases()) {
        Block *block = &region.front();
        Type ctor = block->getArgument(0).getType();
        rewriter.inlineRegionBefore(region, target, target.end());
        TypeConverter::SignatureConversion signature(1);
        signature.addInputs(0, {layout.getStorageFor(ctor)});
        block = rewriter.applySignatureConversion(block, signature, getTypeConverter());
        continuations[ctor] = block;
      }
      for (auto dispatch : exits) {
        Block *continuation = continuations.lookup(dispatch.getConstructor());
        assert(continuation && "verified case covers every match continuation");
        OpBuilder::InsertionGuard guard(rewriter);
        rewriter.setInsertionPoint(dispatch);
        rewriter.replaceOpWithNewOp<cf::BranchOp>(dispatch, continuation,
                                                  ValueRange{dispatch.getValue()});
      }
      rewriter.replaceOp(op, branch.getResults());
    } else if (isa<qtt::YieldOp>(op)) {
      rewriter.replaceOpWithNewOp<scf::YieldOp>(op, args);
    } else {
      return failure();
    }
    return success();
  }

private:
  mutable qrep::LayoutOp layout;
};

static qrep::LayoutOp selectedLayout(func::FuncOp function) {
  auto ref = function->getAttrOfType<FlatSymbolRefAttr>("qrep.layout");
  return ref ? SymbolTable::lookupNearestSymbolFrom<qrep::LayoutOp>(function, ref)
             : qrep::LayoutOp{};
}

static bool needsLayout(func::FuncOp function) {
  bool found = false;
  auto check = [&](TypeRange types) {
    for (Type type : types)
      found |= isa<qtt::ADTType, qtt::CtorType>(type);
  };
  check(function.getFunctionType().getInputs());
  check(function.getFunctionType().getResults());
  function.walk([&](Operation *op) {
    found |= op->getName().getDialectNamespace() == "qtt";
    check(op->getOperandTypes());
    check(op->getResultTypes());
    for (Region &region : op->getRegions())
      for (Block &block : region)
        check(block.getArgumentTypes());
  });
  return found;
}

class LowerQTTToQREPPass : public PassWrapper<LowerQTTToQREPPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerQTTToQREPPass)
  StringRef getArgument() const final { return "lower-qtt-to-qrep"; }
  StringRef getDescription() const final {
    return "Lower QTT using the function's selected QREP layout";
  }
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<qrep::QREPDialect, cf::ControlFlowDialect, scf::SCFDialect>();
  }
  void runOnOperation() override {
    bool orphan = false;
    getOperation().walk([&](Operation *op) {
      if (op->getName().getDialectNamespace() == "qtt" && !op->getParentOfType<func::FuncOp>()) {
        op->emitError("QTT lowering requires an enclosing function with a qrep.layout annotation");
        orphan = true;
      }
    });
    if (orphan)
      return signalPassFailure();
    // Resolve every selection and check ABI agreement before mutating functions.
    SmallVector<std::pair<func::FuncOp, qrep::LayoutOp>> work;
    for (auto function : getOperation().getOps<func::FuncOp>()) {
      auto layout = selectedLayout(function);
      if (function->hasAttr("qrep.layout") && !layout) {
        function.emitError("qrep.layout must resolve to a qrep.layout symbol");
        return signalPassFailure();
      }
      if (!needsLayout(function))
        continue;
      if (!layout) {
        function.emitError("QTT lowering requires a qrep.layout symbol annotation");
        return signalPassFailure();
      }
      if (failed(verify(layout)))
        return signalPassFailure();
      bool mismatch = false;
      function.walk([&](func::CallOp call) {
        bool crossesADT =
            llvm::any_of(call.getOperandTypes(), [](Type t) { return bool(qrep::getADT(t)); }) ||
            llvm::any_of(call.getResultTypes(), [](Type t) { return bool(qrep::getADT(t)); });
        if (!crossesADT)
          return;
        auto callee =
            SymbolTable::lookupNearestSymbolFrom<func::FuncOp>(call, call.getCalleeAttr());
        if (!callee || selectedLayout(callee) != layout) {
          call.emitError("ADT call requires caller and callee to select the same layout");
          mismatch = true;
        }
      });
      if (mismatch)
        return signalPassFailure();
      work.emplace_back(function, layout);
    }
    for (auto [function, layout] : work) {
      LayoutTypeConverter converter(layout);
      RewritePatternSet patterns(&getContext());
      patterns.add<QTTLowering>(converter, &getContext(), layout);
      populateFunctionOpInterfaceTypeConversionPattern<func::FuncOp>(patterns, converter);
      populateReturnOpTypeConversionPattern(patterns, converter);
      populateCallOpTypeConversionPattern(patterns, converter);
      ConversionTarget target(getContext());
      target.addIllegalDialect<qtt::QTTDialect>();
      target.markUnknownOpDynamicallyLegal([&](Operation *op) { return converter.isLegal(op); });
      target.addDynamicallyLegalOp<func::FuncOp>([&](func::FuncOp op) {
        return converter.isSignatureLegal(op.getFunctionType()) && converter.isLegal(&op.getBody());
      });
      scf::populateSCFStructuralTypeConversionsAndLegality(converter, patterns, target);
      if (failed(applyFullConversion(function, target, std::move(patterns))))
        return signalPassFailure();
    }
  }
};
} // namespace

std::unique_ptr<Pass> mlir::createQTTToQREPConversionPass() {
  return std::make_unique<LowerQTTToQREPPass>();
}
void mlir::registerQTTToQREPConversionPass() { PassRegistration<LowerQTTToQREPPass>(); }
