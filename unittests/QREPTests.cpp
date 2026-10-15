#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "TestSupport.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Pass/PassManager.h"

using namespace mlir;
using namespace qtt_test;
using qrep::ImplKind;

static void load(MLIRContext &ctx) {
  loadDialects(ctx);
  loadQREP(ctx);
  ctx.loadDialect<arith::ArithDialect, cf::ControlFlowDialect, scf::SCFDialect>();
}

static void testImplKinds() {
  MLIRContext ctx;
  load(ctx);
  auto fixture = readDataFile("nat_lowering.mlir");
  auto module = parseSourceString<ModuleOp>(fixture, &ctx);
  REQUIRE(module);
  auto layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  for (auto ctor : adt.getCtors())
    for (ImplKind kind : {ImplKind::Pack, ImplKind::Upcast, ImplKind::Downcast, ImplKind::Unpack}) {
      auto impl = qrep::lookupImpl(layout, kind, ctor);
      REQUIRE(impl);
      EXPECT(impl.getKind() == kind);
      EXPECT(impl.getKindAttr() == qrep::ImplKindAttr::get(&ctx, kind));
      std::string text;
      llvm::raw_string_ostream os(text);
      os << impl.getKindAttr();
      os.flush();
      auto reparsed = dyn_cast_or_null<qrep::ImplKindAttr>(parseAttribute(text, &ctx));
      REQUIRE(reparsed);
      EXPECT(reparsed.getValue() == kind);
    }

  std::string generic;
  llvm::raw_string_ostream os(generic);
  module->print(os, OpPrintingFlags().printGenericOpForm());
  os.flush();
  MLIRContext fresh;
  load(fresh);
  auto reparsed = parseSourceString<ModuleOp>(generic, &fresh);
  REQUIRE(reparsed);
  EXPECT(succeeded(verify(*reparsed)));
  auto selected = reparsed->lookupSymbol<qrep::LayoutOp>("nat");
  for (auto impl : selected.getBody().front().getOps<qrep::ImplOp>())
    EXPECT(isa<qrep::ImplKindAttr>(impl.getKindAttr()));

  // Enum keywords may also be quoted and normalize to unquoted keywords.
  auto pos = fixture.find("qrep.impl pack()");
  REQUIRE(pos != std::string::npos);
  fixture.replace(pos, std::string("qrep.impl pack()").size(), "qrep.impl \"pack\"()");
  auto quoted = parseSourceString<ModuleOp>(fixture, &ctx);
  REQUIRE(quoted);
  EXPECT(printOp(*quoted).find("qrep.impl pack()") != std::string::npos);

  fixture = readDataFile("nat_lowering.mlir");
  auto rejectedReplacement = [&](StringRef source, StringRef needle, StringRef replacement,
                                 StringRef message) {
    std::string invalid = source.str();
    auto start = invalid.find(needle.str());
    REQUIRE(start != std::string::npos);
    invalid.replace(start, needle.size(), replacement.str());
    DiagnosticCapture diagnostics(ctx);
    EXPECT(!parseSourceString<ModuleOp>(invalid, &ctx));
    EXPECT(diagnostics.contains(message));
    if (!diagnostics.contains(message))
      llvm::errs() << "Replacement: " << replacement << "\nExpected: " << message
                   << "\nReceived: " << diagnostics.getText();
  };
  rejectedReplacement(fixture, "qrep.impl pack()", "qrep.impl unknown()", "ImplKind to be one of:");
  rejectedReplacement(generic, "#qrep<impl_kind pack>", "\"pack\"",
                      "Invalid attribute `kind` in property conversion");

  // Removed operation and attribute names have no parsing aliases.
  rejectedReplacement(fixture, "qrep.impl", "qrep.method", "qrep.method");
  rejectedReplacement(fixture, "qrep.discriminate", "qrep.match", "qrep.match");
  rejectedReplacement(generic, "\"qrep.impl\"", "\"qrep.method\"",
                      "body may only contain qrep.impl and qrep.discriminate operations");
  rejectedReplacement(generic, "\"qrep.discriminate\"", "\"qrep.match\"",
                      "body may only contain qrep.impl and qrep.discriminate operations");
  rejectedReplacement(generic, "#qrep<impl_kind pack>", "#qrep<method_kind construct>",
                      "unknown attribute");

  // Removed kinds fail in custom syntax (quoted or bare) and generic IR.
  for (StringRef kind : {"construct", "deconstruct", "adt_deconstruct"}) {
    std::string bare = "qrep.impl " + kind.str() + "()";
    std::string quotedKind = "qrep.impl \"" + kind.str() + "\"()";
    std::string attr = "#qrep<impl_kind " + kind.str() + ">";
    rejectedReplacement(fixture, "qrep.impl pack()", bare, "ImplKind to be one of:");
    rejectedReplacement(fixture, "qrep.impl pack()", quotedKind, "ImplKind to be one of:");
    rejectedReplacement(generic, "#qrep<impl_kind pack>", attr, "ImplKind to be one of:");
  }
  DiagnosticCapture diagnostics(ctx);
  EXPECT(!parseAttribute("#qrep<impl_kind unknown>", &ctx));
  EXPECT(diagnostics.contains("ImplKind to be one of:"));
  EXPECT(!parseAttribute("#qrep<method_kind construct>", &ctx));
  EXPECT(diagnostics.contains("unknown attribute"));
}

// Interpret the produced scalar IR so tests cover values above the signed
// boundary without involving an unrelated LLVM/JIT pipeline.
static SmallVector<uint64_t> evaluate(ModuleOp module, Block &block,
                                      DenseMap<Value, uint64_t> &values) {
  auto jump = [&](Block *destination, ValueRange operands) {
    SmallVector<uint64_t> arguments;
    for (Value operand : operands)
      arguments.push_back(values.lookup(operand));
    for (auto [argument, value] : llvm::zip(destination->getArguments(), arguments))
      values[argument] = value;
    return evaluate(module, *destination, values);
  };
  for (Operation &op : block) {
    if (auto c = dyn_cast<arith::ConstantOp>(op))
      values[c.getResult()] = cast<IntegerAttr>(c.getValue()).getValue().getZExtValue();
    else if (auto add = dyn_cast<arith::AddIOp>(op))
      values[add.getResult()] = values.lookup(add.getLhs()) + values.lookup(add.getRhs());
    else if (auto sub = dyn_cast<arith::SubIOp>(op))
      values[sub.getResult()] = values.lookup(sub.getLhs()) - values.lookup(sub.getRhs());
    else if (auto cmp = dyn_cast<arith::CmpIOp>(op)) {
      EXPECT(cmp.getPredicate() == arith::CmpIPredicate::ne);
      values[cmp.getResult()] = values.lookup(cmp.getLhs()) != values.lookup(cmp.getRhs());
    } else if (auto cast = dyn_cast<arith::IndexCastUIOp>(op))
      values[cast.getResult()] = values.lookup(cast.getIn());
    else if (auto extend = dyn_cast<arith::ExtUIOp>(op))
      values[extend.getResult()] = values.lookup(extend.getIn());
    else if (auto select = dyn_cast<arith::SelectOp>(op))
      values[select.getResult()] = values.lookup(
          values.lookup(select.getCondition()) ? select.getTrueValue() : select.getFalseValue());
    else if (auto branch = dyn_cast<cf::BranchOp>(op))
      return jump(branch.getDest(), branch.getDestOperands());
    else if (auto branch = dyn_cast<cf::CondBranchOp>(op)) {
      bool condition = values.lookup(branch.getCondition());
      return jump(condition ? branch.getTrueDest() : branch.getFalseDest(),
                  condition ? branch.getTrueDestOperands() : branch.getFalseDestOperands());
    } else if (auto branch = dyn_cast<cf::SwitchOp>(op)) {
      for (auto [i, tag] : llvm::enumerate(branch.getCaseValues()->getValues<APInt>()))
        if (tag.getZExtValue() == values.lookup(branch.getFlag()))
          return jump(branch.getCaseDestinations()[i], branch.getCaseOperands(i));
      return jump(branch.getDefaultDestination(), branch.getDefaultOperands());
    } else if (auto region = dyn_cast<scf::ExecuteRegionOp>(op)) {
      auto yielded = evaluate(module, region.getRegion().front(), values);
      for (auto [result, value] : llvm::zip(region.getResults(), yielded))
        values[result] = value;
    } else if (auto branch = dyn_cast<scf::IndexSwitchOp>(op)) {
      Region *region = &branch.getDefaultRegion();
      for (auto [i, tag] : llvm::enumerate(branch.getCases()))
        if (static_cast<uint64_t>(tag) == values.lookup(branch.getArg()))
          region = &branch.getCaseRegions()[i];
      auto yielded = evaluate(module, region->front(), values);
      for (auto [result, value] : llvm::zip(branch.getResults(), yielded))
        values[result] = value;
    } else if (auto call = dyn_cast<func::CallOp>(op)) {
      auto callee = module.lookupSymbol<func::FuncOp>(call.getCallee());
      DenseMap<Value, uint64_t> frame;
      for (auto [arg, operand] : llvm::zip(callee.getArguments(), call.getOperands()))
        frame[arg] = values.lookup(operand);
      auto returned = evaluate(module, callee.getBody().front(), frame);
      for (auto [result, value] : llvm::zip(call.getResults(), returned))
        values[result] = value;
    } else if (auto cast = dyn_cast<UnrealizedConversionCastOp>(op)) {
      for (Value result : cast.getResults())
        values[result] = 0;
    } else if (isa<func::ReturnOp, scf::YieldOp, qrep::YieldOp>(op)) {
      SmallVector<uint64_t> result;
      for (Value value : op.getOperands())
        result.push_back(values.lookup(value));
      return result;
    } else {
      EXPECT(false);
    }
  }
  return {};
}

static void checkValues(ModuleOp module) {
  auto run = [&](StringRef name, uint64_t input) {
    auto function = module.lookupSymbol<func::FuncOp>(name);
    DenseMap<Value, uint64_t> values;
    if (function.getNumArguments())
      values[function.getArgument(0)] = input;
    return evaluate(module, function.getBody().front(), values).front();
  };
  EXPECT(run("zero", 0) == 0);
  EXPECT(run("ctor_only", UINT64_MAX - 2) == UINT64_MAX - 2);
  EXPECT(run("call_succ", UINT64_MAX - 2) == UINT64_MAX - 1);
  EXPECT(run("cast_pred", UINT64_MAX - 1) == UINT64_MAX - 2);
  EXPECT(run("nested_construct", 42) == 44);
  for (uint64_t n : {uint64_t(0), uint64_t(1), uint64_t(42), uint64_t(1) << 63, UINT64_MAX - 2})
    EXPECT(run("succ", n) == n + 1);
  for (uint64_t n : {uint64_t(0), uint64_t(1), uint64_t(42), uint64_t(1) << 63, UINT64_MAX - 1}) {
    EXPECT(run("fn_identity", n) == n);
    EXPECT(run("fn_pred", n) == (n ? n - 1 : 0));
    if (n < UINT64_MAX - 1)
      EXPECT(run("fn_direct", n) == n);
    if (n < 100)
      EXPECT(run("fn_recursive", n) == 0);
    auto noResultEntry = module.lookupSymbol<func::FuncOp>("fn_void");
    DenseMap<Value, uint64_t> entryValues;
    entryValues[noResultEntry.getArgument(0)] = n;
    EXPECT(evaluate(module, noResultEntry.getBody().front(), entryValues).empty());
    for (uint64_t m : {uint64_t(0), uint64_t(3)}) {
      for (StringRef name : {"fn_pair", "fn_partial"}) {
        auto function = module.lookupSymbol<func::FuncOp>(name);
        DenseMap<Value, uint64_t> args;
        args[function.getArgument(0)] = n;
        args[function.getArgument(1)] = m;
        auto result = evaluate(module, function.getBody().front(), args);
        SmallVector<uint64_t> expected;
        if (name == "fn_pair")
          expected = {n ? n - 1 : 0, n && m ? m - 1 : m};
        else
          expected = {n && !m ? n - 1 : n, m};
        EXPECT(result == expected);
      }
    }
    EXPECT(run("pred", n) == (n ? n - 1 : 0));
    EXPECT(run("nested_pred", n) == (n > 1 ? n - 2 : 0));
    DenseMap<Value, uint64_t> values;
    auto pair = module.lookupSymbol<func::FuncOp>("case_pair");
    values[pair.getArgument(0)] = n;
    auto results = evaluate(module, pair.getBody().front(), values);
    EXPECT((results == SmallVector<uint64_t>{n ? n - 1 : 0, n}));
    auto noResults = module.lookupSymbol<func::FuncOp>("case_no_results");
    values[noResults.getArgument(0)] = n;
    EXPECT(evaluate(module, noResults.getBody().front(), values).empty());
  }
}

static void testLowering(bool custom, bool heterogeneous, bool distinctView = false) {
  MLIRContext ctx;
  load(ctx);
  auto module = parseSourceString<ModuleOp>(readDataFile("nat_lowering.mlir"), &ctx);
  REQUIRE(module);
  auto layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  REQUIRE(layout);
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  auto successor = adt.lookupCtor(StringAttr::get(&ctx, "S"));
  if (distinctView) {
    // Owned Nat stays i64; readonly Nat is index. Pack must see the latter.
    OpBuilder builder(&ctx);
    layout.setView(builder.getIndexType());
    SmallVector<Attribute> entries;
    for (Attribute attr : layout.getConstructors()) {
      NamedAttrList entry(cast<DictionaryAttr>(attr));
      if (entry.get("constructor") == TypeAttr::get(successor))
        entry.set("view", TypeAttr::get(builder.getIndexType()));
      entries.push_back(entry.getDictionary(&ctx));
    }
    layout.setConstructorsAttr(builder.getArrayAttr(entries));
    for (qtt::CtorType ctor : {qtt::CtorType{}, successor}) {
      auto borrow = qrep::lookupImpl(layout, ImplKind::Borrow, ctor);
      Block &block = borrow.getBody().front();
      builder.setInsertionPoint(block.getTerminator());
      Value view = arith::IndexCastUIOp::create(builder, layout.getLoc(), builder.getIndexType(),
                                                block.getArgument(0));
      block.getTerminator()->setOperand(0, view);
    }
    auto pack = qrep::lookupImpl(layout, ImplKind::Pack, successor);
    Block &block = pack.getBody().front();
    block.getArgument(0).setType(builder.getIndexType());
    builder.setInsertionPointToStart(&block);
    Value storage = arith::IndexCastUIOp::create(builder, layout.getLoc(), builder.getI64Type(),
                                                 block.getArgument(0));
    block.getArgument(0).replaceAllUsesExcept(storage, storage.getDefiningOp());
  }
  if (custom) {
    // A different, explicitly selected layout stores the predecessor in S.
    // Same physical i64, different encoding: casts must run the region bodies.
    auto alternate = cast<qrep::LayoutOp>(layout->clone());
    alternate.setSymName("predecessor_storage");
    module->getBody()->push_back(alternate);
    layout = alternate;
    OpBuilder builder(&ctx);
    for (ImplKind kind : {ImplKind::Pack, ImplKind::Unpack, ImplKind::Upcast, ImplKind::Downcast}) {
      auto impl = qrep::lookupImpl(layout, kind, successor);
      Block &block = impl.getBody().front();
      while (!block.empty())
        block.back().erase();
      builder.setInsertionPointToEnd(&block);
      Value result = block.getArgument(0);
      if (kind == ImplKind::Upcast || kind == ImplKind::Downcast) {
        auto one = arith::ConstantIntOp::create(builder, impl.getLoc(), 1, 64);
        if (kind == ImplKind::Upcast)
          result = arith::AddIOp::create(builder, impl.getLoc(), result, one);
        else
          result = arith::SubIOp::create(builder, impl.getLoc(), result, one);
      }
      qrep::YieldOp::create(builder, impl.getLoc(), ValueRange{result});
    }
    // Tags are private to the implementation. Decode S once before dispatch,
    // forward it through a CFG block argument, and bypass downcast entirely.
    auto discriminate = qrep::lookupDiscriminate(layout);
    Block &block = discriminate.getBody().front();
    auto condition = cast<cf::CondBranchOp>(block.back());
    Block *sBlock = condition.getTrueDest();
    Block *zBlock = condition.getFalseDest();
    builder.setInsertionPoint(condition);
    auto one = arith::ConstantIntOp::create(builder, layout.getLoc(), 1, 64);
    auto decoded = arith::SubIOp::create(builder, layout.getLoc(), block.getArgument(0), one);
    auto sTag = arith::ConstantIntOp::create(builder, layout.getLoc(), 19, 32);
    auto zTag = arith::ConstantIntOp::create(builder, layout.getLoc(), 7, 32);
    auto tag =
        arith::SelectOp::create(builder, layout.getLoc(), condition.getCondition(), sTag, zTag);
    sBlock->addArgument(builder.getI64Type(), layout.getLoc());
    cast<qrep::DispatchOp>(sBlock->getTerminator())
        .getValueMutable()
        .assign(sBlock->getArgument(0));
    cf::SwitchOp::create(builder, layout.getLoc(), tag, zBlock, ValueRange{}, ArrayRef<int32_t>{19},
                         BlockRange{sBlock}, ArrayRef<ValueRange>{ValueRange{decoded}});
    condition.erase();
    for (auto function : module->getOps<func::FuncOp>())
      function->setAttr("qrep.layout", FlatSymbolRefAttr::get(layout.getSymNameAttr()));
  }
  if (heterogeneous) {
    // Z uses i1 while S uses i64. Several CFG exits invoke the same Z branch.
    OpBuilder builder(&ctx);
    auto zero = adt.lookupCtor(StringAttr::get(&ctx, "Z"));
    SmallVector<Attribute> entries;
    for (Attribute attr : layout.getConstructors()) {
      NamedAttrList entry(cast<DictionaryAttr>(attr));
      if (entry.get("constructor") == TypeAttr::get(zero))
        entry.set("storage", TypeAttr::get(builder.getI1Type()));
      entries.push_back(entry.getDictionary(&ctx));
    }
    layout.setConstructorsAttr(builder.getArrayAttr(entries));
    // Borrow Z's new i1 storage into its existing none View.
    auto borrow = qrep::lookupImpl(layout, ImplKind::Borrow, zero);
    auto &borrowBlock = borrow.getBody().front();
    borrowBlock.getArgument(0).setType(builder.getI1Type());
    borrowBlock.back().erase();
    builder.setInsertionPointToEnd(&borrowBlock);
    auto unit = UnrealizedConversionCastOp::create(builder, layout.getLoc(),
                                                   TypeRange{builder.getNoneType()}, ValueRange{});
    qrep::YieldOp::create(builder, layout.getLoc(), unit.getResults());
    for (ImplKind kind : {ImplKind::Pack, ImplKind::Upcast, ImplKind::Downcast, ImplKind::Unpack}) {
      auto impl = qrep::lookupImpl(layout, kind, zero);
      Block &block = impl.getBody().front();
      while (!block.empty())
        block.back().erase();
      if (kind == ImplKind::Upcast || kind == ImplKind::Unpack)
        block.getArgument(0).setType(builder.getI1Type());
      builder.setInsertionPointToEnd(&block);
      SmallVector<Value> values;
      if (kind == ImplKind::Pack || kind == ImplKind::Downcast)
        values.push_back(arith::ConstantIntOp::create(builder, layout.getLoc(), 0, 1));
      else if (kind == ImplKind::Upcast)
        values.push_back(arith::ExtUIOp::create(builder, layout.getLoc(), builder.getI64Type(),
                                                block.getArgument(0)));
      qrep::YieldOp::create(builder, layout.getLoc(), values);
    }
    auto discriminate = qrep::lookupDiscriminate(layout);
    qrep::DispatchOp zExit;
    for (Block &block : discriminate.getBody())
      if (auto exit = dyn_cast<qrep::DispatchOp>(block.back()))
        if (exit.getConstructor() == zero)
          zExit = exit;
    builder.setInsertionPoint(zExit);
    auto value = arith::ConstantIntOp::create(builder, layout.getLoc(), 0, 1);
    auto otherExit = builder.createBlock(&discriminate.getBody());
    qrep::DispatchOp::create(builder, layout.getLoc(), zero, value);
    auto firstExit = builder.createBlock(&discriminate.getBody());
    qrep::DispatchOp::create(builder, layout.getLoc(), zero, value);
    builder.setInsertionPoint(zExit);
    cf::CondBranchOp::create(builder, layout.getLoc(), value, firstExit, otherExit);
    zExit.erase();
  }
  auto functions = parseSourceString<ModuleOp>(readDataFile("function_lowering.mlir"), &ctx);
  REQUIRE(functions);
  while (!functions->getBody()->empty()) {
    Operation &function = functions->getBody()->front();
    function.setAttr("qrep.layout", FlatSymbolRefAttr::get(&ctx, layout.getSymName()));
    function.moveBefore(module->getBody(), module->getBody()->end());
  }
  REQUIRE(succeeded(verify(*module)));
  std::string text = printOp(*module);
  EXPECT(text.find("qrep.discriminate (") != std::string::npos);
  EXPECT(text.find("qrep.impl pack()") != std::string::npos);
  MLIRContext fresh;
  load(fresh);
  auto reparsed = parseSourceString<ModuleOp>(text, &fresh);
  REQUIRE(reparsed);
  EXPECT(succeeded(verify(*reparsed)));
  EXPECT(printOp(*reparsed) == text);
  PassManager pm(&ctx);
  pm.addPass(createQTTToQREPConversionPass());
  REQUIRE(succeeded(pm.run(*module)));
  REQUIRE(succeeded(verify(*module)));
  EXPECT(printOp(module->lookupSymbol<func::FuncOp>("succ")).find("qrep.borrow") !=
         std::string::npos);
  EXPECT(printOp(module->lookupSymbol<func::FuncOp>("succ")).find("!qrep.view<") !=
         std::string::npos);
  // Both custom and generic intermediate IR must survive a fresh context.
  for (bool generic : {false, true}) {
    std::string intermediate;
    llvm::raw_string_ostream stream(intermediate);
    module->print(stream, OpPrintingFlags().printGenericOpForm(generic));
    MLIRContext roundtrip;
    load(roundtrip);
    REQUIRE(parseSourceString<ModuleOp>(intermediate, &roundtrip));
  }
  pm.clear();
  pm.addPass(createQREPToStorageConversionPass());
  REQUIRE(succeeded(pm.run(*module)));
  REQUIRE(succeeded(verify(*module)));
  for (auto function : module->getOps<func::FuncOp>()) {
    EXPECT(function->getAttrOfType<FlatSymbolRefAttr>("qrep.layout").getValue() ==
           layout.getSymName());
    EXPECT(printOp(function).find("qtt.") == std::string::npos);
  }
  if (custom) {
    auto ctorOnly = module->lookupSymbol<func::FuncOp>("ctor_only");
    EXPECT(printOp(ctorOnly).find("arith.") == std::string::npos);
  }
  auto pred = module->lookupSymbol<func::FuncOp>("pred");
  unsigned subtractions = 0, switches = 0, conditions = 0;
  pred.walk([&](Operation *op) {
    subtractions += isa<arith::SubIOp>(op);
    switches += isa<cf::SwitchOp>(op);
    conditions += isa<cf::CondBranchOp>(op);
    EXPECT((!isa<scf::IndexSwitchOp, qrep::DispatchOp, qrep::DiscriminateOp>(op)));
  });
  EXPECT(subtractions == 1);
  EXPECT(switches == unsigned(custom));
  EXPECT(conditions == unsigned(!custom) + unsigned(heterogeneous));
  if (heterogeneous) {
    unsigned extensions = 0;
    module->lookupSymbol<func::FuncOp>("case_pair").walk([&](arith::ExtUIOp) { ++extensions; });
    EXPECT(extensions == 1); // Both Z exits reuse the same continuation body.
  }
  checkValues(*module);
}

static void testErrors() {
  MLIRContext ctx;
  load(ctx);
  DiagnosticCapture diagnostics(ctx);
  auto fixture = readDataFile("nat_lowering.mlir");
  auto loweringFails = [&](StringRef name, Attribute annotation) {
    auto module = parseSourceString<ModuleOp>(fixture, &ctx);
    auto function = module->lookupSymbol<func::FuncOp>(name);
    if (annotation)
      function->setAttr("qrep.layout", annotation);
    else
      function->removeAttr("qrep.layout");
    PassManager pm(&ctx);
    pm.addPass(createQTTToQREPConversionPass());
    EXPECT(failed(pm.run(*module)));
  };
  loweringFails("zero", {});
  EXPECT(diagnostics.contains("requires a qrep.layout symbol annotation"));
  loweringFails("zero", FlatSymbolRefAttr::get(&ctx, "missing"));
  EXPECT(diagnostics.contains("must resolve to a qrep.layout symbol"));
  loweringFails("call_succ", FlatSymbolRefAttr::get(&ctx, "zero"));
  EXPECT(diagnostics.contains("must resolve to a qrep.layout symbol"));

  // Layout mappings use complete constructor identity, including payloads.
  auto invalidConstructor = [&](auto change) {
    auto invalid = parseSourceString<ModuleOp>(fixture, &ctx);
    auto selected = invalid->lookupSymbol<qrep::LayoutOp>("nat");
    auto adt = cast<qtt::ADTType>(selected.getAdt());
    SmallVector<Attribute> entries(selected.getConstructors().begin(),
                                   selected.getConstructors().end());
    NamedAttrList entry(cast<DictionaryAttr>(entries.front()));
    change(selected, adt, entry);
    entries.front() = entry.getDictionary(&ctx);
    selected.setConstructorsAttr(ArrayAttr::get(&ctx, entries));
    DiagnosticCapture error(ctx);
    EXPECT(failed(verify(*invalid)));
    EXPECT(error.contains("constructors must be declared CtorTypes and unique"));
  };
  invalidConstructor([](qrep::LayoutOp, qtt::ADTType adt, NamedAttrList &entry) {
    entry.erase("constructor");
    entry.set("name", adt.getCtors().front().getName());
  });
  invalidConstructor([](qrep::LayoutOp, qtt::ADTType adt, NamedAttrList &entry) {
    entry.set("constructor", TypeAttr::get(adt));
  });
  invalidConstructor([](qrep::LayoutOp, qtt::ADTType adt, NamedAttrList &entry) {
    entry.set("constructor", TypeAttr::get(adt.getCtors().back()));
  });
  invalidConstructor([](qrep::LayoutOp layout, qtt::ADTType adt, NamedAttrList &entry) {
    auto zero = adt.getCtors().front();
    auto foreign =
        qtt::ADTType::get(adt.getContext(), StringAttr::get(adt.getContext(), "ForeignNat"));
    auto ctor = qtt::CtorType::get(foreign, zero.getName(), zero.getPayload());
    EXPECT(!layout.getStorageFor(ctor));
    entry.set("constructor", TypeAttr::get(ctor));
  });
  invalidConstructor([](qrep::LayoutOp layout, qtt::ADTType adt, NamedAttrList &entry) {
    auto ctor = qtt::CtorType::get(adt, adt.getCtors().front().getName(),
                                   {IntegerType::get(adt.getContext(), 64)});
    EXPECT(!layout.lookupConstructor(ctor));
    EXPECT(!layout.getStorageFor(ctor));
    entry.set("constructor", TypeAttr::get(ctor));
  });

  auto invalidImpl = [&](ImplKind kind, auto change, StringRef message) {
    auto invalid = parseSourceString<ModuleOp>(fixture, &ctx);
    auto selected = invalid->lookupSymbol<qrep::LayoutOp>("nat");
    auto ctor = cast<qtt::ADTType>(selected.getAdt()).getCtors().front();
    change(selected, qrep::lookupImpl(selected, kind, ctor));
    DiagnosticCapture error(ctx);
    EXPECT(failed(verify(*invalid)));
    EXPECT(error.contains(message));
  };
  invalidImpl(
      ImplKind::Pack,
      [](qrep::LayoutOp layout, qrep::ImplOp impl) {
        layout.getBody().front().push_back(impl->clone());
      },
      "duplicate primitive implementation");
  invalidImpl(
      ImplKind::Pack, [](qrep::LayoutOp, qrep::ImplOp impl) { impl.erase(); },
      "missing pack implementation");
  invalidImpl(
      ImplKind::Upcast,
      [](qrep::LayoutOp layout, qrep::ImplOp impl) {
        impl.getBody().front().getArgument(0).setType(IntegerType::get(layout.getContext(), 32));
      },
      "implementation arguments do not match storage signature");

  auto module = parseSourceString<ModuleOp>(fixture, &ctx);
  auto layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  auto discriminate = qrep::lookupDiscriminate(layout);
  discriminate.erase();
  EXPECT(failed(verify(*module)));
  EXPECT(diagnostics.contains("missing discriminate"));

  module = parseSourceString<ModuleOp>(fixture, &ctx);
  layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  discriminate = qrep::lookupDiscriminate(layout);
  discriminate.getBody().front().getArgument(0).setType(IntegerType::get(&ctx, 32));
  EXPECT(failed(verify(*module)));
  EXPECT(diagnostics.contains("entry arguments must be the ADT storage signature"));

  auto invalidDiscriminate = [&](auto change, StringRef message) {
    auto invalid = parseSourceString<ModuleOp>(fixture, &ctx);
    auto selected = invalid->lookupSymbol<qrep::LayoutOp>("nat");
    change(selected, qrep::lookupDiscriminate(selected));
    DiagnosticCapture error(ctx);
    EXPECT(failed(verify(*invalid)));
    EXPECT(error.contains(message));
    if (!error.contains(message))
      llvm::errs() << "Expected: " << message << "\nReceived: " << error.getText();
  };
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        layout.getBody().front().push_back(discriminate->clone());
      },
      "duplicate discriminate implementation");
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        auto adt = cast<qtt::ADTType>(layout.getAdt());
        for (Block &block : discriminate.getBody())
          if (auto dispatch = dyn_cast<qrep::DispatchOp>(block.back()))
            if (dispatch.getConstructor() == adt.getCtors().back()) {
              dispatch.setConstructor(adt.getCtors().front());
              OpBuilder builder(dispatch);
              auto none = UnrealizedConversionCastOp::create(
                  builder, dispatch.getLoc(), TypeRange{builder.getNoneType()}, ValueRange{});
              dispatch.getValueMutable().assign(none.getResult(0));
            }
      },
      "missing dispatch for constructor \"S\"");
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        auto dispatch = cast<qrep::DispatchOp>(discriminate.getBody().back().back());
        OpBuilder builder(dispatch);
        auto wrong = arith::ConstantIntOp::create(builder, layout.getLoc(), 0, 32);
        dispatch.getValueMutable().assign(wrong);
      },
      "dispatch value must match constructor storage");
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        auto dispatch = cast<qrep::DispatchOp>(discriminate.getBody().back().back());
        dispatch.setConstructor(layout.getAdt());
      },
      "constructor must be declared by the parent layout ADT");
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        auto dispatch = cast<qrep::DispatchOp>(discriminate.getBody().back().back());
        auto ctor = cast<qtt::CtorType>(dispatch.getConstructor());
        auto foreign = qtt::ADTType::get(layout.getContext(),
                                         StringAttr::get(layout.getContext(), "OtherNat"));
        dispatch.setConstructor(qtt::CtorType::get(foreign, ctor.getName(), ctor.getPayload()));
      },
      "constructor must be declared by the parent layout ADT");
  invalidDiscriminate(
      [](qrep::LayoutOp, qrep::DiscriminateOp discriminate) {
        auto dispatch = cast<qrep::DispatchOp>(discriminate.getBody().back().back());
        OpBuilder builder(dispatch);
        func::ReturnOp::create(builder, dispatch.getLoc());
        dispatch.erase();
      },
      "expects parent op 'func.func'");
  invalidDiscriminate(
      [](qrep::LayoutOp layout, qrep::DiscriminateOp discriminate) {
        auto branch = cast<cf::CondBranchOp>(discriminate.getBody().front().back());
        branch->setSuccessor(&layout.getBody().front(), 0);
      },
      "entry block of region may not have predecessors");

  module = parseSourceString<ModuleOp>(fixture, &ctx);
  layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  auto alternate = cast<qrep::LayoutOp>(layout->clone());
  alternate.setSymName("other_layout");
  module->getBody()->push_back(alternate);
  module->lookupSymbol<func::FuncOp>("call_succ")
      ->setAttr("qrep.layout", FlatSymbolRefAttr::get(&ctx, "other_layout"));
  PassManager pm(&ctx);
  pm.addPass(createQTTToQREPConversionPass());
  EXPECT(failed(pm.run(*module)));
  EXPECT(diagnostics.contains("caller and callee to select the same layout"));
}

static void testViewContracts() {
  MLIRContext ctx;
  load(ctx);
  auto fixture = readDataFile("nat_lowering.mlir");
  auto module = parseSourceString<ModuleOp>(fixture, &ctx);
  REQUIRE(module);
  auto layout = module->lookupSymbol<qrep::LayoutOp>("nat");
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  auto ctor = adt.getCtors().back();
  auto ref = FlatSymbolRefAttr::get(&ctx, "nat");
  Type own = qrep::OwnType::get(&ctx, ref, Type{});
  Type view = qrep::ViewType::get(&ctx, ref, Type{});
  Type ownCtor = qrep::OwnType::get(&ctx, ref, ctor);
  Type viewCtor = qrep::ViewType::get(&ctx, ref, ctor);
  for (Type type : {own, view, ownCtor, viewCtor})
    EXPECT(parseType(printType(type), &ctx) == type);
  EXPECT(layout.getViewFor(adt) == layout.getView());
  EXPECT(layout.getViewFor(ctor) == IntegerType::get(&ctx, 64));

  auto invalidLayout = [&](auto change, StringRef expected) {
    auto invalid = parseSourceString<ModuleOp>(fixture, &ctx);
    auto selected = invalid->lookupSymbol<qrep::LayoutOp>("nat");
    change(selected);
    DiagnosticCapture diagnostics(ctx);
    EXPECT(failed(verify(*invalid)));
    EXPECT(diagnostics.contains(expected));
  };
  invalidLayout([](qrep::LayoutOp l) { l->removeAttr("view"); }, "view");
  invalidLayout([](qrep::LayoutOp l) { l.setView(l.getAdt()); }, "concrete ADT view");
  invalidLayout(
      [](qrep::LayoutOp l) {
        SmallVector<Attribute> entries(l.getConstructors().begin(), l.getConstructors().end());
        NamedAttrList entry(cast<DictionaryAttr>(entries.front()));
        entry.erase("view");
        entries.front() = entry.getDictionary(l.getContext());
        l.setConstructorsAttr(ArrayAttr::get(l.getContext(), entries));
      },
      "concrete constructor view");
  invalidLayout([](qrep::LayoutOp l) { qrep::lookupImpl(l, ImplKind::Borrow).erase(); },
                "missing borrow implementation for ADT");
  invalidLayout(
      [](qrep::LayoutOp l) {
        auto ctor = cast<qtt::ADTType>(l.getAdt()).getCtors().back();
        qrep::lookupImpl(l, ImplKind::Borrow, ctor).erase();
      },
      "missing borrow implementation for");
  invalidLayout([](qrep::LayoutOp l) { l.setView(IndexType::get(l.getContext())); },
                "implementation yield does not match storage signature");

  // A readonly function argument can feed pack directly, without another borrow.
  OpBuilder builder(module->getBodyRegion());
  builder.setInsertionPointToEnd(module->getBody());
  auto function = func::FuncOp::create(builder, layout.getLoc(), "from_view",
                                       builder.getFunctionType({view}, {ownCtor}));
  function->setAttr("qrep.layout", ref);
  Block *block = function.addEntryBlock();
  builder.setInsertionPointToEnd(block);
  auto pack =
      qrep::PackOp::create(builder, layout.getLoc(), TypeRange{ownCtor}, block->getArguments());
  func::ReturnOp::create(builder, layout.getLoc(), pack.getResults());
  REQUIRE(succeeded(verify(*module)));
  {
    DiagnosticCapture diagnostics(ctx);
    // Equal physical types do not make owned input legal for pack.
    block->getArgument(0).setType(own);
    EXPECT(failed(verify(pack)));
    EXPECT(diagnostics.contains("representation signature"));
    block->getArgument(0).setType(view);
  }
  {
    DiagnosticCapture diagnostics(ctx);
    Type missing = qrep::OwnType::get(&ctx, FlatSymbolRefAttr::get(&ctx, "missing"), ctor);
    pack.getResult(0).setType(missing);
    EXPECT(failed(verify(pack)));
    EXPECT(diagnostics.contains("must reference a qrep.layout symbol"));
    auto foreign = qtt::ADTType::get(&ctx, StringAttr::get(&ctx, "ForeignView"));
    auto wrongCtor = qtt::CtorType::get(foreign, ctor.getName(), ctor.getPayload());
    pack.getResult(0).setType(qrep::OwnType::get(&ctx, ref, wrongCtor));
    EXPECT(failed(verify(pack)));
    EXPECT(diagnostics.contains("constructor refinement must be declared"));
    pack.getResult(0).setType(ownCtor);
  }
  // Constructor borrowing preserves refinement and leaves the owner usable.
  builder.setInsertionPointToEnd(module->getBody());
  auto borrowing = func::FuncOp::create(builder, layout.getLoc(), "borrow_ctor",
                                        builder.getFunctionType({ownCtor}, {viewCtor, ownCtor}));
  borrowing->setAttr("qrep.layout", ref);
  Block *borrowBlock = borrowing.addEntryBlock();
  builder.setInsertionPointToEnd(borrowBlock);
  auto borrow = qrep::BorrowOp::create(builder, layout.getLoc(), TypeRange{viewCtor},
                                       borrowBlock->getArguments());
  func::ReturnOp::create(builder, layout.getLoc(),
                         ValueRange{borrow.getResult(0), borrowBlock->getArgument(0)});
  REQUIRE(succeeded(verify(*module)));
  {
    DiagnosticCapture diagnostics(ctx);
    borrow.getResult(0).setType(view);
    EXPECT(failed(verify(borrow)));
    EXPECT(diagnostics.contains("representation signature"));
    borrow.getResult(0).setType(viewCtor);
  }
  // Equal physical types still run the layout's borrow encoding.
  auto implementation = qrep::lookupImpl(layout, ImplKind::Borrow, ctor);
  builder.setInsertionPoint(implementation.getBody().front().getTerminator());
  Value one = arith::ConstantIntOp::create(builder, layout.getLoc(), 1, 64);
  Value encoded = arith::AddIOp::create(builder, layout.getLoc(),
                                        implementation.getBody().front().getArgument(0), one);
  implementation.getBody().front().getTerminator()->setOperand(0, encoded);
  PassManager pm(&ctx);
  pm.addPass(createQTTToQREPConversionPass());
  REQUIRE(succeeded(pm.run(*module)));
  unsigned borrows = 0;
  function.walk([&](qrep::BorrowOp) { ++borrows; });
  EXPECT(borrows == 0);
  pm.clear();
  pm.addPass(createQREPToStorageConversionPass());
  REQUIRE(succeeded(pm.run(*module)));
  DenseMap<Value, uint64_t> values;
  values[function.getArgument(0)] = 41;
  EXPECT(evaluate(*module, function.getBody().front(), values).front() == 42);
  values[borrowing.getArgument(0)] = 42;
  EXPECT((evaluate(*module, borrowing.getBody().front(), values) == SmallVector<uint64_t>{43, 42}));
}

int main() {
  currentTest = "QREPLayout";
  testImplKinds();
  testLowering(false, false);
  testLowering(true, false);
  testLowering(false, true);
  testLowering(true, true);
  testLowering(false, false, true);
  testErrors();
  testViewContracts();
  return failures ? 1 : 0;
}
