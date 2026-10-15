#include "QTTFunctionLowering.h"
#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Verifier.h"

using namespace mlir;

namespace {
struct Entry {
  FunctionType type;
  func::FuncOp function;
};

// A decision tree splits only refined arguments. The original ADT arguments
// remain available for wildcard parameters and for the default fallback.
SmallVector<Value> emitDispatch(OpBuilder &builder, Location loc, TypeRange results,
                                ValueRange original, SmallVector<Value> refined,
                                ArrayRef<Entry> entries, func::FuncOp fallback, unsigned pos = 0) {
  auto call = [&](func::FuncOp target) -> SmallVector<Value> {
    SmallVector<Value> args;
    for (auto [i, type] : llvm::enumerate(target.getArgumentTypes()))
      args.push_back(type == original[i].getType() ? original[i] : refined[i]);
    auto invocation = func::CallOp::create(builder, loc, target, args);
    return SmallVector<Value>(invocation.getResults());
  };
  if (entries.empty()) {
    assert(fallback && "verified dispatch has a fallback for uncovered inputs");
    return call(fallback);
  }
  if (pos == original.size()) {
    assert(entries.size() == 1 && "verified entries do not overlap");
    return call(entries.front().function);
  }
  bool splits = llvm::any_of(entries, [&](const Entry &entry) {
    return entry.type.getInput(pos) != original[pos].getType();
  });
  if (!splits)
    return emitDispatch(builder, loc, results, original, refined, entries, fallback, pos + 1);
  auto adt = cast<qtt::ADTType>(original[pos].getType());
  auto match = qtt::CaseOp::create(builder, loc, results, original[pos], adt.getCtors().size());
  for (auto [ctor, region] : llvm::zip(adt.getCtors(), match.getCases())) {
    OpBuilder::InsertionGuard guard(builder);
    Block *block = builder.createBlock(&region);
    refined[pos] = block->addArgument(ctor, loc);
    SmallVector<Entry> candidates;
    for (const Entry &entry : entries)
      if (entry.type.getInput(pos) == adt || entry.type.getInput(pos) == ctor)
        candidates.push_back(entry);
    auto values =
        emitDispatch(builder, loc, results, original, refined, candidates, fallback, pos + 1);
    qtt::YieldOp::create(builder, loc, values);
  }
  return SmallVector<Value>(match.getResults());
}

void copyDialectAttrs(Operation *source, Operation *target) {
  for (NamedAttribute attr : source->getDialectAttrs())
    target->setAttr(attr.getName(), attr.getValue());
}

class LowerQTTFunctionsPass : public PassWrapper<LowerQTTFunctionsPass, OperationPass<ModuleOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(LowerQTTFunctionsPass)
  StringRef getArgument() const final { return "lower-qtt-functions"; }
  StringRef getDescription() const final {
    return "Lower QTT entries and dispatch to func operations and exhaustive qtt.case";
  }
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<qtt::QTTDialect, func::FuncDialect>();
  }
  void runOnOperation() override {
    if (failed(lowerQTTFunctions(getOperation())))
      signalPassFailure();
  }
};
} // namespace

LogicalResult mlir::lowerQTTFunctions(ModuleOp module) {
  if (failed(verify(module)))
    return failure();
  SmallVector<qtt::FuncOp> functions;
  bool unsupported = false;
  module.walk([&](qtt::FuncOp function) {
    if (function->getParentOp() != module) {
      function.emitOpError("function lowering currently requires module-level definitions");
      unsupported = true;
    } else {
      functions.push_back(function);
    }
  });
  if (unsupported)
    return failure();

  // Resolve before moving entry bodies out of their original symbol scopes.
  SmallVector<std::pair<Operation *, Operation *>> calls;
  module.walk([&](Operation *op) {
    if (isa<qtt::DispatchCallOp, qtt::EntryCallOp>(op))
      calls.emplace_back(op, cast<CallOpInterface>(op).resolveCallable());
  });
  SymbolTable symbols(module);
  DenseMap<Operation *, func::FuncOp> lowered;
  SmallVector<func::FuncOp> dispatchers;
  OpBuilder builder(module.getContext());
  for (auto function : functions) {
    auto dispatcher =
        func::FuncOp::create(function.getLoc(), function.getSymName(), function.getFunctionType());
    if (auto visibility = function.getSymVisibilityAttr())
      dispatcher.setSymVisibilityAttr(visibility);
    copyDialectAttrs(function, dispatcher);
    if (auto attrs = function.getArgAttrsAttr())
      dispatcher.setArgAttrsAttr(attrs);
    if (auto attrs = function.getResAttrsAttr())
      dispatcher.setResAttrsAttr(attrs);
    lowered[function] = dispatcher;
    dispatchers.push_back(dispatcher);
    SmallVector<Entry> entries;
    func::FuncOp fallback;
    for (auto entry : function.getBody().front().getOps<qtt::FuncEntryOp>()) {
      auto target = func::FuncOp::create(entry.getLoc(),
                                         (function.getSymName() + "$" + entry.getSymName()).str(),
                                         entry.getFunctionType());
      copyDialectAttrs(function, target);
      copyDialectAttrs(entry, target);
      target.setPrivate();
      if (auto attrs = entry.getArgAttrsAttr())
        target.setArgAttrsAttr(attrs);
      if (auto attrs = entry.getResAttrsAttr())
        target.setResAttrsAttr(attrs);
      symbols.insert(target);
      lowered[entry] = target;
      target.getBody().takeBody(entry.getBody());
      if (entry.getIsDefault())
        fallback = target;
      else
        entries.push_back({entry.getFunctionType(), target});
    }
    Block *body = dispatcher.addEntryBlock();
    builder.setInsertionPointToStart(body);
    auto values = emitDispatch(builder, function.getLoc(), function.getFunctionType().getResults(),
                               body->getArguments(), SmallVector<Value>(body->getArguments()),
                               entries, fallback);
    func::ReturnOp::create(builder, function.getLoc(), values);
  }
  for (auto [op, callee] : calls) {
    auto target = lowered.lookup(callee);
    assert(target && "verified QTT call resolves to a lowered definition");
    builder.setInsertionPoint(op);
    auto call = func::CallOp::create(builder, op->getLoc(), target, op->getOperands());
    for (NamedAttribute attr : op->getAttrs())
      if (attr.getName() != "callee")
        call->setAttr(attr.getName(), attr.getValue());
    op->replaceAllUsesWith(call.getResults());
    op->erase();
  }
  module.walk([&](qtt::ReturnOp ret) {
    builder.setInsertionPoint(ret);
    func::ReturnOp::create(builder, ret.getLoc(), ret.getValues());
    ret.erase();
  });
  for (auto [source, dispatcher] : llvm::zip(functions, dispatchers)) {
    symbols.erase(source);
    symbols.insert(dispatcher);
  }
  return success();
}

std::unique_ptr<Pass> mlir::createQTTFunctionLoweringPass() {
  return std::make_unique<LowerQTTFunctionsPass>();
}
void mlir::registerQTTFunctionLoweringPass() { PassRegistration<LowerQTTFunctionsPass>(); }
