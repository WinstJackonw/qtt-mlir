#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "QTTFunctionLowering.h"
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
  explicit LayoutTypeConverter(qrep::LayoutOp layout, bool abstract) {
    addConversion([layout, abstract](Type type) -> Type {
      auto selected = layout;
      auto ref = FlatSymbolRefAttr::get(selected.getSymNameAttr());
      if (abstract) {
        if (isa<qtt::ADTType, qtt::CtorType>(type)) {
          if (!selected.getStorageFor(type))
            return {};
          return qrep::OwnType::get(type.getContext(), ref,
                                    isa<qtt::CtorType>(type) ? type : Type{});
        }
      } else {
        if (auto own = dyn_cast<qrep::OwnType>(type))
          return own.getLayout() == ref
                     ? selected.getStorageFor(own.getConstructor() ? own.getConstructor()
                                                                   : selected.getAdt())
                     : Type{};
        if (auto view = dyn_cast<qrep::ViewType>(type))
          return view.getLayout() == ref
                     ? selected.getViewFor(view.getConstructor() ? view.getConstructor()
                                                                 : selected.getAdt())
                     : Type{};
        if (isa<qtt::ADTType, qtt::CtorType>(type))
          return {};
      }
      return type;
    });
  }
};

// Instantiate the selected layout's primitive implementation.
static SmallVector<Value> instantiate(qrep::LayoutOp layout, qrep::ImplKind kind,
                                      qtt::CtorType ctor, ValueRange args,
                                      ConversionPatternRewriter &rewriter) {
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
  QTTLowering(TypeConverter &converter, MLIRContext *ctx, qrep::LayoutOp layout, bool abstract)
      : ConversionPattern(converter, MatchAnyOpTypeTag(), 1, ctx), layout(layout),
        abstract(abstract) {}

  LogicalResult matchAndRewrite(Operation *op, ArrayRef<Value> args,
                                ConversionPatternRewriter &rewriter) const override {
    if (op->getName().getDialectNamespace() != (abstract ? "qtt" : "qrep"))
      return failure();
    SmallVector<Type> results;
    if (failed(getTypeConverter()->convertTypes(op->getResultTypes(), results)))
      return failure();
    if (abstract) {
      StringRef name;
      if (isa<qtt::ConstructOp>(op))
        name = "qrep.pack";
      else if (isa<qtt::UnpackOp>(op))
        name = "qrep.unpack";
      else if (isa<qtt::UpcastOp, qtt::CastOp>(op)) {
        Type from = op->getOperand(0).getType(), to = op->getResult(0).getType();
        if (from == to) {
          rewriter.replaceOp(op, args);
          return success();
        }
        name = isa<qtt::CtorType>(from) ? "qrep.upcast" : "qrep.downcast";
      } else if (isa<qtt::CaseOp>(op))
        name = "qrep.case";
      else if (isa<qtt::YieldOp>(op))
        name = "qrep.case_yield";
      else
        return failure();
      SmallVector<Value> operands(args);
      if (isa<qtt::ConstructOp>(op))
        for (Value &arg : operands)
          if (auto own = dyn_cast<qrep::OwnType>(arg.getType())) {
            Type view =
                qrep::ViewType::get(op->getContext(), own.getLayout(), own.getConstructor());
            auto borrow =
                qrep::BorrowOp::create(rewriter, op->getLoc(), TypeRange{view}, ValueRange{arg});
            arg = borrow.getResult(0);
          }
      OperationState state(op->getLoc(), name);
      state.addOperands(operands);
      state.addTypes(results);
      for (unsigned i = 0; i < op->getNumRegions(); ++i)
        state.addRegion();
      Operation *replacement = rewriter.create(state);
      for (auto [oldRegion, newRegion] : llvm::zip(op->getRegions(), replacement->getRegions())) {
        rewriter.inlineRegionBefore(oldRegion, newRegion, newRegion.end());
        if (failed(rewriter.convertRegionTypes(&newRegion, *getTypeConverter())))
          return failure();
      }
      rewriter.replaceOp(op, replacement->getResults());
      return success();
    }
    if (isa<qrep::PackOp>(op)) {
      auto ctor =
          cast<qtt::CtorType>(cast<qrep::OwnType>(op->getResult(0).getType()).getConstructor());
      rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Pack, ctor, args, rewriter));
    } else if (isa<qrep::UnpackOp>(op)) {
      auto ctor =
          cast<qtt::CtorType>(cast<qrep::OwnType>(op->getOperand(0).getType()).getConstructor());
      rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Unpack, ctor, args, rewriter));
    } else if (isa<qrep::BorrowOp>(op)) {
      auto own = cast<qrep::OwnType>(op->getOperand(0).getType());
      auto ctor = dyn_cast_or_null<qtt::CtorType>(own.getConstructor());
      rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Borrow, ctor, args, rewriter));
    } else if (isa<qrep::UpcastOp, qrep::DowncastOp>(op)) {
      Type from = cast<qrep::OwnType>(op->getOperand(0).getType()).getConstructor();
      Type to = cast<qrep::OwnType>(op->getResult(0).getType()).getConstructor();
      // Compare source refinements, not storage types: equal physical types
      // need not have equal encodings (e.g. successor versus predecessor).
      if (from == to)
        rewriter.replaceOp(op, args);
      else if (auto ctor = dyn_cast_or_null<qtt::CtorType>(from))
        rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Upcast, ctor, args, rewriter));
      else
        rewriter.replaceOp(op, instantiate(layout, qrep::ImplKind::Downcast,
                                           cast<qtt::CtorType>(to), args, rewriter));
    } else if (auto match = dyn_cast<qrep::CaseOp>(op)) {
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
        Type ctor = cast<qrep::OwnType>(block->getArgument(0).getType()).getConstructor();
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
    } else if (isa<qrep::CaseYieldOp>(op)) {
      rewriter.replaceOpWithNewOp<scf::YieldOp>(op, args);
    } else {
      return failure();
    }
    return success();
  }

private:
  mutable qrep::LayoutOp layout;
  bool abstract;
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
      found |= isa<qtt::ADTType, qtt::CtorType, qrep::OwnType, qrep::ViewType>(type);
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

template <bool abstract>
class LowerRepresentationPass
    : public PassWrapper<LowerRepresentationPass<abstract>, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerRepresentationPass)
  using Base = PassWrapper<LowerRepresentationPass<abstract>, OperationPass<ModuleOp>>;
  using Base::getContext;
  using Base::getOperation;
  using Base::signalPassFailure;
  StringRef getArgument() const final { return abstract ? "lower-qtt-to-qrep" : "lower-qrep"; }
  StringRef getDescription() const final {
    return abstract ? "Lower QTT to explicit QREP ownership and views"
                    : "Resolve QREP ownership and layout implementations";
  }
  void getDependentDialects(DialectRegistry &registry) const override {
    registry
        .insert<qrep::QREPDialect, cf::ControlFlowDialect, scf::SCFDialect, func::FuncDialect>();
  }
  void runOnOperation() override {
    if constexpr (abstract)
      if (failed(lowerQTTFunctions(getOperation())))
        return signalPassFailure();
    bool orphan = false;
    getOperation().walk([&](Operation *op) {
      bool runtime = op->getName().getDialectNamespace() == "qtt" ||
                     isa<qrep::PackOp, qrep::UnpackOp, qrep::UpcastOp, qrep::DowncastOp,
                         qrep::BorrowOp, qrep::CaseOp, qrep::CaseYieldOp>(op);
      if (runtime && !op->getParentOfType<func::FuncOp>()) {
        op->emitError("QTT lowering requires an enclosing function with a qrep.layout annotation");
        orphan = true;
      }
    });
    if (orphan)
      return signalPassFailure();
    // Resolve every selection and check ABI agreement before mutating functions.
    SmallVector<std::pair<func::FuncOp, qrep::LayoutOp>> work;
    for (auto function : getOperation().template getOps<func::FuncOp>()) {
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
      auto checkRepresentation = [&](Type type) {
        FlatSymbolRefAttr ref;
        Type ctor;
        if (auto own = dyn_cast<qrep::OwnType>(type)) {
          ref = own.getLayout();
          ctor = own.getConstructor();
        } else if (auto view = dyn_cast<qrep::ViewType>(type)) {
          ref = view.getLayout();
          ctor = view.getConstructor();
        } else {
          return;
        }
        if (SymbolTable::lookupNearestSymbolFrom<qrep::LayoutOp>(function, ref) != layout ||
            (ctor && !layout.lookupConstructor(cast<qtt::CtorType>(ctor)))) {
          function.emitError(
              "representation type must reference the selected layout and a declared constructor");
          mismatch = true;
        }
      };
      for (Type type : function.getFunctionType().getInputs())
        checkRepresentation(type);
      for (Type type : function.getFunctionType().getResults())
        checkRepresentation(type);
      function.walk([&](Operation *op) {
        for (Type type : op->getOperandTypes())
          checkRepresentation(type);
        for (Type type : op->getResultTypes())
          checkRepresentation(type);
        for (Region &region : op->getRegions())
          for (Block &block : region)
            for (Type type : block.getArgumentTypes())
              checkRepresentation(type);
      });
      function.walk([&](func::CallOp call) {
        bool crossesADT =
            llvm::any_of(call.getOperandTypes(),
                         [](Type t) {
                           return bool(qrep::getADT(t)) || isa<qrep::OwnType, qrep::ViewType>(t);
                         }) ||
            llvm::any_of(call.getResultTypes(), [](Type t) {
              return bool(qrep::getADT(t)) || isa<qrep::OwnType, qrep::ViewType>(t);
            });
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
      LayoutTypeConverter converter(layout, abstract);
      RewritePatternSet patterns(&getContext());
      patterns.add<QTTLowering>(converter, &getContext(), layout, abstract);
      populateFunctionOpInterfaceTypeConversionPattern<func::FuncOp>(patterns, converter);
      populateReturnOpTypeConversionPattern(patterns, converter);
      populateCallOpTypeConversionPattern(patterns, converter);
      ConversionTarget target(getContext());
      target.addIllegalDialect<qtt::QTTDialect>();
      if constexpr (!abstract)
        target.addIllegalOp<qrep::PackOp, qrep::UnpackOp, qrep::UpcastOp, qrep::DowncastOp,
                            qrep::BorrowOp, qrep::CaseOp, qrep::CaseYieldOp>();
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
  return std::make_unique<LowerRepresentationPass<true>>();
}
void mlir::registerQTTToQREPConversionPass() { PassRegistration<LowerRepresentationPass<true>>(); }
std::unique_ptr<Pass> mlir::createQREPToStorageConversionPass() {
  return std::make_unique<LowerRepresentationPass<false>>();
}
void mlir::registerQREPToStorageConversionPass() {
  PassRegistration<LowerRepresentationPass<false>>();
}
