#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "TestSupport.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Pass/PassManager.h"

using namespace mlir;
using namespace qtt_test;

static void testRoundtrip() {
  MLIRContext ctx;
  loadDialects(ctx);
  OwningOpRef<ModuleOp> module;
  REQUIRE(!roundtrip(ctx, module, readDataFile("functions.mlir")).empty());
  module->walk([&](qtt::DispatchCallOp call) {
    EXPECT(
        isa_and_nonnull<qtt::FuncOp>(cast<CallOpInterface>(call.getOperation()).resolveCallable()));
  });
  module->walk([&](qtt::EntryCallOp call) {
    auto target = dyn_cast_or_null<qtt::FuncEntryOp>(
        cast<CallOpInterface>(call.getOperation()).resolveCallable());
    EXPECT(target);
    if (target)
      EXPECT(isa<FunctionOpInterface>(target.getOperation()));
  });
  auto unit = module->lookupSymbol<qtt::FuncOp>("unit");
  REQUIRE(unit);
  qtt::FuncEntryOp fallback;
  for (auto entry : unit.getBody().front().getOps<qtt::FuncEntryOp>())
    if (entry.getSymName() == "fallback")
      fallback = entry;
  REQUIRE(fallback);
  unsigned unitCalls = 0;
  fallback.walk([&](qtt::EntryCallOp call) {
    ++unitCalls;
    EXPECT(cast<CallOpInterface>(call.getOperation()).resolveCallable() == fallback.getOperation());
  });
  EXPECT(unitCalls == 0);
  auto unitResolver = module->lookupSymbol<func::FuncOp>("unit_resolve");
  REQUIRE(unitResolver);
  unitResolver.walk([&](qtt::EntryCallOp call) {
    ++unitCalls;
    EXPECT(cast<CallOpInterface>(call.getOperation()).resolveCallable() == fallback.getOperation());
  });
  EXPECT(unitCalls == 1);
  auto entryResolver = module->lookupSymbol<qtt::FuncOp>("entry_resolve");
  REQUIRE(entryResolver);
  qtt::FuncEntryOp zero;
  for (auto entry : entryResolver.getBody().front().getOps<qtt::FuncEntryOp>())
    if (entry.getSymName() == "zero")
      zero = entry;
  REQUIRE(zero);
  unsigned siblingCalls = 0;
  entryResolver.walk([&](qtt::EntryCallOp call) {
    ++siblingCalls;
    EXPECT(cast<CallOpInterface>(call.getOperation()).resolveCallable() == zero.getOperation());
  });
  EXPECT(siblingCalls == 1);
  std::string generic;
  llvm::raw_string_ostream out(generic);
  module->print(out, OpPrintingFlags().printGenericOpForm());
  MLIRContext fresh;
  loadDialects(fresh);
  EXPECT(parseSourceString<ModuleOp>(generic, &fresh));
}

static void reject(StringRef body, StringRef diagnostic) {
  MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture capture(ctx);
  std::string source = R"(
!nat = !qtt.adt<"N", [!qtt.ctor<"N", "Z"()>, !qtt.ctor<"N", "S"(!qtt.adt<"N">)>]>
!z = !qtt.ctor<"N", "Z"()>
!s = !qtt.ctor<"N", "S"(!nat)>
module {
)" + body.str() + "\n}";
  auto module = parseSourceString<ModuleOp>(source, &ctx);
  EXPECT(!module);
  if (!capture.contains(diagnostic)) {
    llvm::errs() << "Expected: " << diagnostic << "\nGot: " << capture.getText() << '\n';
    ++failures;
  }
}

static void testInvalidEntries() {
  reject("qtt.func @f : () -> () {}", "requires at least one");
  reject(R"(qtt.func @f : () -> () {
    func.func private @bad()
  })",
         "body may only contain");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @z(%z: !z) { qtt.return }
  })",
         "not exhaustive");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry default @a(%n: !nat) { qtt.return }
    qtt.func_entry default @b(%n: !nat) { qtt.return }
  })",
         "duplicate default");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry default @a(%n: !z) { qtt.return }
  })",
         "full parent function signature");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @a(%n: !nat) { qtt.return }
  })",
         "must be marked default");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @a(%n: i64) { qtt.return }
  })",
         "equal or refine");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @a() { qtt.return }
  })",
         "input count");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @a(%n: !z) -> !z { qtt.return %n : !z }
  })",
         "result types must match parent");
  reject(R"(qtt.func @f : (!nat, !nat) -> () {
    qtt.func_entry @a(%a: !z, %b: !nat) { qtt.return }
    qtt.func_entry @b(%a: !nat, %b: !z) { qtt.return }
    qtt.func_entry default @d(%a: !nat, %b: !nat) { qtt.return }
  })",
         "overlapping specialized");
  reject(R"(qtt.func @f : (!nat, !nat) -> () {
    qtt.func_entry @a(%a: !z, %b: !nat) { qtt.return }
    qtt.func_entry @b(%a: !s, %b: !z) { qtt.return }
  })",
         "not exhaustive");
  reject(R"(qtt.func @f : () -> () {
    qtt.func_entry default @a() { qtt.return }
    qtt.func_entry default @a() { qtt.return }
  })",
         "redefinition of symbol");
  reject(R"(qtt.func_entry default @a() { qtt.return })", "expects parent op");
  reject(R"(qtt.func @f : () -> () {
    qtt.func_entry default private @a()
  })",
         "requires an implementation body");
  reject(R"(qtt.func @f : (i64) -> () {
    qtt.func_entry default @a(%n: i64) { qtt.return %n : i64 }
  })",
         "operand types must match entry");
  reject(R"(func.func @f() { qtt.return })", "expects parent op");
}

static void testInvalidCalls() {
  reject(R"(func.func @caller() {
    qtt.dispatch_call @missing() : () -> ()
    return
  })",
         "callee must resolve to qtt.func");
  reject(R"(func.func private @f()
  func.func @caller() {
    qtt.dispatch_call @f() : () -> ()
    return
  })",
         "callee must resolve to qtt.func");
  reject(R"(qtt.func @f : () -> () {
    qtt.func_entry default @d() { qtt.return }
  }
  func.func @caller() {
    qtt.entry_call @f() : () -> ()
    return
  })",
         "callee must resolve to qtt.func_entry");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry default @d(%n: !nat) { qtt.return }
  }
  func.func @caller(%n: !z) {
    qtt.dispatch_call @f(%n) : (!z) -> ()
    return
  })",
         "operand types must match callee");
  reject(R"(qtt.func @f : (!nat) -> () {
    qtt.func_entry @s(%n: !s) { qtt.return }
    qtt.func_entry default @d(%n: !nat) { qtt.return }
  }
  func.func @caller(%n: !nat) {
    qtt.entry_call @f::@s(%n) : (!nat) -> ()
    return
  })",
         "operand types must match callee");
  reject(R"(qtt.func @f : () -> () {
    qtt.func_entry default @d() { qtt.return }
  }
  func.func @caller() {
    %r = qtt.dispatch_call @f() : () -> i64
    return
  })",
         "result types must match callee");
}

static void testLowering() {
  MLIRContext ctx;
  loadDialects(ctx);
  auto module = parseSourceString<ModuleOp>(readDataFile("functions.mlir"), &ctx);
  REQUIRE(module);
  // Reserve the natural implementation name to exercise symbol uniquing.
  OpBuilder builder(&ctx);
  builder.setInsertionPointToEnd(module->getBody());
  auto collision = func::FuncOp::create(builder, module->getLoc(), "pred$successor",
                                        builder.getFunctionType({}, {}));
  collision.setPrivate();
  PassManager pm(&ctx);
  pm.addPass(createQTTFunctionLoweringPass());
  REQUIRE(succeeded(pm.run(*module)));
  REQUIRE(succeeded(verify(*module)));
  module->walk([&](Operation *op) {
    EXPECT(
        (!isa<qtt::FuncOp, qtt::FuncEntryOp, qtt::DispatchCallOp, qtt::EntryCallOp, qtt::ReturnOp>(
            op)));
  });
  auto cases = [&](StringRef name) {
    unsigned count = 0;
    module->lookupSymbol<func::FuncOp>(name).walk([&](qtt::CaseOp) { ++count; });
    return count;
  };
  EXPECT(cases("identity") == 0);
  EXPECT(cases("opaque") == 0);
  EXPECT(cases("scalar") == 0);
  EXPECT(cases("unit") == 0);
  EXPECT(cases("pred") == 1);
  EXPECT(cases("total") == 1);
  EXPECT(cases("pair") == 2);

  auto loweredUnit = module->lookupSymbol<func::FuncOp>("unit$fallback");
  REQUIRE(loweredUnit);
  SmallVector<func::CallOp> unitCalls;
  module->lookupSymbol<func::FuncOp>("unit_resolve").walk(
      [&](func::CallOp call) { unitCalls.push_back(call); });
  EXPECT(unitCalls.size() == 1);
  for (func::CallOp call : unitCalls) {
    EXPECT(call.getCallee() == "unit$fallback");
    EXPECT(call.getNumOperands() == 0);
    EXPECT(call.getNumResults() == 0);
  }

  auto pair = module->lookupSymbol<func::FuncOp>("pair");
  REQUIRE(pair);
  auto pairEntry = [&](StringRef name, int64_t expectedTag) {
    auto entry = module->lookupSymbol<func::FuncOp>(name);
    REQUIRE(entry);
    SmallVector<int64_t> tags;
    entry.walk([&](arith::ConstantOp constant) {
      if (constant.getType().isInteger(64))
        tags.push_back(cast<IntegerAttr>(constant.getValue()).getInt());
    });
    EXPECT(tags.size() == 1);
    if (tags.size() == 1)
      EXPECT(tags.front() == expectedTag);
  };
  pairEntry("pair$zero_any", 0);
  pairEntry("pair$succ_zero", 1);
  pairEntry("pair$succ_succ", 2);

  auto onlyCall = [](Block &block) -> func::CallOp {
    func::CallOp result;
    for (Operation &op : block)
      if (auto call = dyn_cast<func::CallOp>(op))
        result = call;
    return result;
  };
  auto expectCall = [&](Block &block, StringRef expected) {
    auto call = onlyCall(block);
    EXPECT(call);
    if (call)
      EXPECT(call.getCallee() == expected);
  };
  qtt::CaseOp outerCase;
  for (Operation &op : pair.getBody().front())
    if (auto candidate = dyn_cast<qtt::CaseOp>(op))
      outerCase = candidate;
  REQUIRE(outerCase);
  auto outerCases = outerCase.getCases();
  REQUIRE(outerCases.size() == 2);
  expectCall(outerCases[0].front(), "pair$zero_any");
  auto nestedCase = dyn_cast<qtt::CaseOp>(&outerCases[1].front().front());
  REQUIRE(nestedCase);
  auto nestedCases = nestedCase.getCases();
  REQUIRE(nestedCases.size() == 2);
  expectCall(nestedCases[0].front(), "pair$succ_zero");
  expectCall(nestedCases[1].front(), "pair$succ_succ");

  auto pred = module->lookupSymbol<func::FuncOp>("pred");
  pred.walk([&](func::CallOp call) {
    auto callee = module->lookupSymbol<func::FuncOp>(call.getCallee());
    REQUIRE(callee);
    if (isa<qtt::ADTType>(callee.getArgumentTypes().front()))
      EXPECT(call.getOperand(0) == pred.getArgument(0)); // Original ADT for default.
    else
      EXPECT(call.getCallee() != "pred$successor");
  });
  std::string before = printOp(*module);
  REQUIRE(succeeded(pm.run(*module)));
  EXPECT(printOp(*module) == before);
}

int main() {
  currentTest = "Functions";
  testRoundtrip();
  testInvalidEntries();
  testInvalidCalls();
  testLowering();
  return failures ? 1 : 0;
}
