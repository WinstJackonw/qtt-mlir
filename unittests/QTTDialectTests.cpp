// QTT dialect behavior tests; textual inputs live in data/.
#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"

#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

#include "TestSupport.h"

namespace {
using namespace qtt_test;

void testADTCanonicalization() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  auto mod = mlir::parseSourceString<mlir::ModuleOp>(readDataFile("adt_canonicalization.mlir"), &ctx);
  REQUIRE(mod);
  mlir::PassManager pm(&ctx);
  pm.addPass(mlir::createCanonicalizerPass());
  REQUIRE(succeeded(pm.run(*mod)));
  REQUIRE(succeeded(mlir::verify(*mod)));
  auto count = [](mlir::func::FuncOp func, llvm::StringRef name) {
    unsigned result = 0;
    func.walk([&](mlir::Operation *op) { result += op->getName().getStringRef() == name; });
    return result;
  };
  for (auto name : {"unpack_construct", "reconstruct", "identity", "upcast_back", "cast_back",
                    "parameterized", "recursive"}) {
    auto func = mod->lookupSymbol<mlir::func::FuncOp>(name);
    REQUIRE(func);
    EXPECT(count(func, "qtt.construct") == 0);
    EXPECT(count(func, "qtt.unpack") == 0);
    EXPECT(count(func, "qtt.cast") == 0);
    EXPECT(count(func, "qtt.upcast") == 0);
    EXPECT(count(func, "qtt.case") == 0);
    auto ret = llvm::cast<mlir::func::ReturnOp>(func.getBody().front().getTerminator());
    EXPECT(ret.getOperand(0) == func.getArgument(0));
  }
  for (auto name : {"known", "known_cast", "nullary"}) {
    auto func = mod->lookupSymbol<mlir::func::FuncOp>(name);
    REQUIRE(func);
    EXPECT(count(func, "qtt.case") == 0);
    EXPECT(count(func, "qtt.yield") == 0);
    EXPECT(count(func, "qtt.unpack") == 0);
    llvm::SmallVector<mlir::func::CallOp> calls;
    func.walk([&](mlir::func::CallOp call) { calls.push_back(call); });
    REQUIRE(calls.size() == 2);
    EXPECT(calls[0].getCallee() == "first");
    EXPECT(calls[1].getCallee() == "second");
    if (llvm::StringRef(name) != "nullary") {
      auto ret = llvm::cast<mlir::func::ReturnOp>(func.getBody().front().getTerminator());
      REQUIRE(ret.getNumOperands() == 2);
      EXPECT(ret.getOperand(0) == func.getArgument(0));
      EXPECT(ret.getOperand(1) == func.getArgument(1));
    }
  }
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("unknown"), "qtt.case") == 1);
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("reordered"), "qtt.construct") == 1);
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("rebuild_other"), "qtt.construct") == 1);
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("partial"), "qtt.construct") == 1);
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("narrow_widen"), "qtt.cast") == 1);
  EXPECT(count(mod->lookupSymbol<mlir::func::FuncOp>("different_ctor"), "qtt.cast") == 1);
  std::string printed = printOp(*mod);
  REQUIRE(succeeded(pm.run(*mod)));
  EXPECT(printOp(*mod) == printed);
}

//===----------------------------------------------------------------------===//
// Dialect registration.
//===----------------------------------------------------------------------===//

void testDialectRegistration() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  EXPECT(ctx.getLoadedDialect<mlir::qtt::QTTDialect>() != nullptr);
  EXPECT(ctx.getLoadedDialect("qtt") != nullptr);
}

void testQREPRegistration() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  loadQREP(ctx);
  EXPECT(ctx.getLoadedDialect<mlir::qrep::QREPDialect>() != nullptr);
  EXPECT(ctx.getLoadedDialect("qrep") != nullptr);
}

//===----------------------------------------------------------------------===//
// Types.
//===----------------------------------------------------------------------===//

void testADTTypeRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::vector<std::string> lines = readDataLines("adt_roundtrip.mlir");
  REQUIRE(lines.size() == 2);

  mlir::Type type = mlir::parseType(lines[0], &ctx);
  REQUIRE(type != nullptr);

  auto adt = llvm::dyn_cast<mlir::qtt::ADTType>(type);
  REQUIRE(adt != nullptr);

  EXPECT(adt.isInitialized());
  EXPECT(adt.getCtors().size() == 2);

  mlir::qtt::CtorType lit = adt.lookupCtor(mlir::StringAttr::get(&ctx, "Lit"));
  REQUIRE(lit);
  REQUIRE(lit.getPayload().size() == 1);
  EXPECT(lit.getPayload()[0] == mlir::IntegerType::get(&ctx, 64));
  EXPECT(!adt.lookupCtor(mlir::StringAttr::get(&ctx, "Missing")));
  EXPECT(printType(type) == lines[0]);

  // The short reference form must resolve to the same uniqued type.
  mlir::Type again = mlir::parseType(lines[1], &ctx);
  EXPECT(again == type);
}

void testADTTypeArguments() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  auto lines = readDataLines("adt_arguments.mlir");
  REQUIRE(lines.size() == 10);
  auto reference = mlir::parseType(lines[0], &ctx);
  REQUIRE(reference);
  auto adt = llvm::cast<mlir::qtt::ADTType>(reference);
  REQUIRE(adt.getTypeArguments().size() == 2);
  EXPECT(adt.getTypeArguments()[0] == mlir::IntegerType::get(&ctx, 64));
  EXPECT(adt.getTypeArguments()[1] == mlir::Float64Type::get(&ctx));
  EXPECT(!adt.isInitialized());
  EXPECT(printType(adt) == lines[0]);
  // The builder copies arguments into context-owned storage.
  llvm::SmallVector<mlir::Type> arguments(adt.getTypeArguments());
  EXPECT(mlir::qtt::ADTType::get(&ctx, adt.getQualifiedName(), arguments) == adt);
  arguments.clear();
  EXPECT(mlir::parseType(lines[1], &ctx) == adt);
  EXPECT(mlir::parseType(lines[1], &ctx) == adt);
  REQUIRE(adt.isInitialized());
  auto other = mlir::parseType(lines[2], &ctx);
  REQUIRE(other);
  EXPECT(other != adt);
  auto ctorType = mlir::parseType(lines[3], &ctx);
  REQUIRE(ctorType);
  auto ctor = llvm::cast<mlir::qtt::CtorType>(ctorType);
  EXPECT(ctor.getParent() == adt);
  EXPECT(adt.lookupCtor(mlir::StringAttr::get(&ctx, "Left")) == ctor);
  EXPECT(mlir::qtt::isSubType(ctor, adt));
  EXPECT(!mlir::qtt::isSubType(ctor, other));
  for (unsigned i : {4u, 5u}) {
    auto distinct = mlir::parseType(lines[i], &ctx);
    REQUIRE(distinct);
    EXPECT(distinct != adt);
    EXPECT(!mlir::qtt::isSubType(ctor, distinct));
  }
  EXPECT(mlir::qtt::ADTType::get(&ctx, adt.getQualifiedName()).getTypeArguments().empty());
  {
    DiagnosticCapture diags(ctx);
    EXPECT(!mlir::parseType(lines[6], &ctx));
    EXPECT(diags.contains("does not belong to ADT"));
    EXPECT(!mlir::parseType(lines[7], &ctx));
    EXPECT(diags.contains("conflicting definition of ADT"));
    EXPECT(printType(adt) == lines[1]);
  }
  for (unsigned i : {1u, 2u, 3u, 8u, 9u}) {
    auto type = mlir::parseType(lines[i], &ctx);
    REQUIRE(type);
    auto printed = printType(type);
    mlir::MLIRContext fresh;
    loadDialects(fresh);
    auto copy = mlir::parseType(printed, &fresh);
    REQUIRE(copy);
    EXPECT(printType(copy) == printed);
    if (i == 8) {
      auto list = llvm::cast<mlir::qtt::ADTType>(copy);
      EXPECT(list.getCtors()[0].getPayload()[1] == list);
    }
  }
}

void testParameterizedConstructUpcast() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  mlir::OwningOpRef<mlir::ModuleOp> mod;
  REQUIRE(!roundtrip(ctx, mod, readDataFile("parameterized_construct_upcast.mlir")).empty());
  expectVerifierFailure("cross_arguments_upcast.mlir", "is not a subtype of");
}

void testADTTypeConflictRejected() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);

  std::vector<std::string> lines = readDataLines("adt_conflict.mlir");
  REQUIRE(lines.size() == 2);

  EXPECT(mlir::parseType(lines[0], &ctx) != nullptr);
  EXPECT(mlir::parseType(lines[1], &ctx) == nullptr);
  EXPECT(diags.contains("conflicting definition of ADT"));
}

void testADTDuplicateCtorRejected() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);

  std::vector<std::string> lines = readDataLines("adt_duplicate_ctor.mlir");
  REQUIRE(lines.size() == 1);

  EXPECT(mlir::parseType(lines[0], &ctx) == nullptr);
  EXPECT(diags.contains("duplicate constructor name"));
}

void testCtorTypeRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::vector<std::string> lines = readDataLines("ctor_roundtrip.mlir");
  REQUIRE(lines.size() == 2);

  mlir::Type adtType = mlir::parseType(lines[0], &ctx);
  REQUIRE(adtType != nullptr);
  auto adt = llvm::dyn_cast<mlir::qtt::ADTType>(adtType);
  EXPECT(adt != nullptr);

  mlir::Type type = mlir::parseType(lines[1], &ctx);
  REQUIRE(type != nullptr);

  auto ctor = llvm::dyn_cast<mlir::qtt::CtorType>(type);
  REQUIRE(ctor != nullptr);

  EXPECT(ctor.getParent() == adt);
  // The ADT stores this very CtorType, not just its name.
  EXPECT(adt.lookupCtor(mlir::StringAttr::get(&ctx, "Lit")) == ctor);
  REQUIRE(ctor.getPayload().size() == 1);
  EXPECT(ctor.getPayload()[0] == mlir::IntegerType::get(&ctx, 64));
  EXPECT(printType(type) == lines[1]);
}

void testCtorOfUndefinedADTAccepted() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::vector<std::string> lines = readDataLines("ctor_incomplete_adt.mlir");
  REQUIRE(lines.size() == 1);

  mlir::Type type = mlir::parseType(lines[0], &ctx);
  REQUIRE(type != nullptr);

  // The constructor carries its own payload, so it is fully defined even
  // though the referenced ADT never was.
  auto ctor = llvm::dyn_cast<mlir::qtt::CtorType>(type);
  REQUIRE(ctor != nullptr);
  EXPECT(!ctor.getParent().isInitialized());
  EXPECT(ctor.getPayload().empty());
  EXPECT(printType(type) == lines[0]);
}

void testCtorUnknownConstructorRejected() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);

  std::vector<std::string> lines = readDataLines("ctor_unknown_constructor.mlir");
  REQUIRE(lines.size() == 2);

  EXPECT(mlir::parseType(lines[0], &ctx) != nullptr);
  EXPECT(mlir::parseType(lines[1], &ctx) == nullptr);
  EXPECT(diags.contains("has no constructor"));
}

void testUnknownTypeRejected() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);

  std::vector<std::string> lines = readDataLines("unknown_type.mlir");
  REQUIRE(lines.size() == 1);

  EXPECT(mlir::parseType(lines[0], &ctx) == nullptr);
  EXPECT(diags.contains("type `unknown`"));
}

void testIsSubType() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::vector<std::string> lines = readDataLines("subtype.mlir");
  REQUIRE(lines.size() == 3);

  mlir::Type expr = mlir::parseType(lines[0], &ctx);
  mlir::Type other = mlir::parseType(lines[1], &ctx);
  mlir::Type ctor = mlir::parseType(lines[2], &ctx);
  EXPECT(expr != nullptr);
  EXPECT(other != nullptr);
  EXPECT(ctor != nullptr);
  if (!expr || !other || !ctor)
    return;

  EXPECT(mlir::qtt::isSubType(expr, expr));
  EXPECT(mlir::qtt::isSubType(ctor, expr));
  EXPECT(!mlir::qtt::isSubType(other, expr));
  EXPECT(!mlir::qtt::isSubType(expr, ctor));
}

//===----------------------------------------------------------------------===//
// Operations.
//===----------------------------------------------------------------------===//

void testConstructUpcastRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::string ir = readDataFile("construct_upcast.mlir");
  if (ir.empty())
    return;

  mlir::OwningOpRef<mlir::ModuleOp> mod;
  std::string printed = roundtrip(ctx, mod, ir);
  if (printed.empty())
    return;

  EXPECT(printed.find("qtt.construct") != std::string::npos);
  EXPECT(printed.find("qtt.upcast") != std::string::npos);
  EXPECT(printed.find("!qtt.ctor<\"expr\", \"Lit\"(i64)>") != std::string::npos);
  EXPECT(printed.find("!qtt.adt<\"expr\"") != std::string::npos);
  EXPECT(mod->lookupSymbol<mlir::func::FuncOp>("make") != nullptr);
}

void testUnpackRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  mlir::OwningOpRef<mlir::ModuleOp> mod;
  REQUIRE(!roundtrip(ctx, mod, readDataFile("unpack_roundtrip.mlir")).empty());

  unsigned count = 0;
  mod->walk([&](mlir::qtt::UnpackOp op) {
    if (count == 0) {
      EXPECT(op.getArgs().empty());
    } else {
      REQUIRE(op.getArgs().size() == 2);
      EXPECT(op.getArgs()[0].getType() == mlir::IntegerType::get(&ctx, 64));
      EXPECT(op.getArgs()[1].getType() == mlir::IntegerType::get(&ctx, 32));
    }
    ++count;
  });
  EXPECT(count == 2);
}

void testCasePrettyPrint() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  mlir::OwningOpRef<mlir::ModuleOp> mod;
  std::string printed = roundtrip(ctx, mod, readDataFile("case_roundtrip.mlir"));
  REQUIRE(!printed.empty());
  EXPECT(printed.find("Lit(") != std::string::npos);
  EXPECT(printed.find("Nil(") != std::string::npos);
  EXPECT(printed.find("^bb") == std::string::npos);
}

void testCaseResultsRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  mlir::OwningOpRef<mlir::ModuleOp> mod;
  auto printed = roundtrip(ctx, mod, readDataFile("case_results.mlir"));
  REQUIRE(!printed.empty());
  EXPECT(printed.find("marker = \"kept\"") != std::string::npos);
  EXPECT(printed.find("nested = true") != std::string::npos);
  unsigned cases = 0;
  mod->walk([&](mlir::qtt::CaseOp op) {
    EXPECT(op.getNumResults() == 1 || op.getNumResults() == 2);
    ++cases;
  });
  EXPECT(cases == 2);
  std::string generic;
  llvm::raw_string_ostream os(generic);
  mod->print(os, mlir::OpPrintingFlags().printGenericOpForm());
  os.flush();
  mlir::OwningOpRef<mlir::ModuleOp> copy;
  EXPECT(!roundtrip(ctx, copy, generic).empty());
}

void testYieldWrongCount() {
  expectVerifierFailure("yield_wrong_count.mlir", "operand count must match case result count");
}
void testYieldWrongType() {
  expectVerifierFailure("yield_wrong_type.mlir", "operand type must match case result type");
}
void testYieldWrongParent() {
  expectVerifierFailure("yield_wrong_parent.mlir", "qtt.case");
}
void testCaseMissingYield() {
  expectVerifierFailure("case_missing_yield.mlir", "each case region must end with qtt.yield");
}

void testCaseBranchValidation() {
  const std::string prefix = R"mlir(module {
    func.func private @adt() -> !qtt.adt<"choice", [!qtt.ctor<"choice", "A"()>, !qtt.ctor<"choice", "B"()>]>
    func.func private @other() -> !qtt.adt<"other", [!qtt.ctor<"other", "C"()>]>
    func.func @bad(%input: !qtt.adt<"choice">) {
      "qtt.case"(%input) (
  )mlir";
  const std::string suffix = R"mlir() : (!qtt.adt<"choice">) -> ()
      return
    }
  })mlir";
  const std::pair<const char *, const char *> cases[] = {
      {R"mlir({^bb0(%a: !qtt.ctor<"choice", "A"()>): qtt.yield})mlir",
       "case is not exhaustive"},
      {R"mlir({^bb0(%a: !qtt.ctor<"choice", "A"()>): qtt.yield},
              {^bb0(%b: !qtt.ctor<"choice", "A"()>): qtt.yield})mlir",
       "duplicate variant case"},
      {R"mlir({^bb0(%a: !qtt.ctor<"other", "C"()>): qtt.yield})mlir",
       "case variant belongs to another ADT"},
      {R"mlir({^bb0(%a: i64): qtt.yield})mlir",
       "case argument must have qtt.variant type"},
      {R"mlir({qtt.yield})mlir", "exactly one variant argument"},
      {R"mlir({^bb0(%a: !qtt.ctor<"choice", "A"()>): return})mlir",
       "each case region must end with qtt.yield"},
  };
  for (auto [regions, message] : cases) {
    mlir::MLIRContext ctx;
    loadDialects(ctx);
    DiagnosticCapture diagnostics(ctx);
    auto mod = mlir::parseSourceString<mlir::ModuleOp>(
        prefix + regions + suffix, mlir::ParserConfig(&ctx, false));
    REQUIRE(mod);
    EXPECT(failed(mlir::verify(*mod)));
    EXPECT(diagnostics.contains(message));
  }
}

void testCaseIncompleteADTRejected() {
  expectVerifierFailure("case_incomplete_adt.mlir", "input ADT must be fully defined");
}

void testPayloadMismatchRejected() {
  expectVerifierFailure("payload_mismatch.mlir", "constructor payload type mismatch");
}

void testCrossADTUpcastRejected() {
  expectVerifierFailure("cross_adt_upcast.mlir", "is not a subtype of");
}

// Each definition must be self-contained when printed, including recursive bodies.
void testADTRecursiveRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  auto lines = readDataLines("adt_recursive.mlir");
  REQUIRE(lines.size() == 3);
  for (const auto &line : lines) {
    auto type = mlir::parseType(line, &ctx);
    REQUIRE(type != nullptr);
    auto adt = llvm::dyn_cast<mlir::qtt::ADTType>(type);
    REQUIRE(adt != nullptr);
    REQUIRE(adt.isInitialized());
    std::string printed = printType(type);
    mlir::MLIRContext fresh;
    loadDialects(fresh);
    auto reparsed = mlir::parseType(printed, &fresh);
    REQUIRE(reparsed != nullptr);
    EXPECT(printType(reparsed) == printed);
    auto copy = llvm::dyn_cast<mlir::qtt::ADTType>(reparsed);
    REQUIRE(copy != nullptr);
    REQUIRE(copy.isInitialized());
    if (adt.getQualifiedName().getValue() == "Self") {
      REQUIRE(copy.getCtors().size() == 1);
      REQUIRE(copy.getCtors()[0].getPayload().size() == 1);
      EXPECT(copy.getCtors()[0].getPayload()[0] == copy);
    } else if (adt.getQualifiedName().getValue() == "A") {
      REQUIRE(copy.getCtors().size() == 1);
      REQUIRE(copy.getCtors()[0].getPayload().size() == 1);
      auto child = llvm::dyn_cast<mlir::qtt::ADTType>(copy.getCtors()[0].getPayload()[0]);
      REQUIRE(child != nullptr);
      REQUIRE(child.isInitialized());
      REQUIRE(child.getCtors().size() == 1);
      REQUIRE(child.getCtors()[0].getPayload().size() == 1);
      EXPECT(child.getCtors()[0].getPayload()[0] == copy);
    } else {
      EXPECT(copy.getCtors().empty());
    }
  }
}

void testADTDefinitionLifecycle() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  auto lines = readDataLines("adt_lifecycle.mlir");
  REQUIRE(lines.size() == 6);
  auto reference = mlir::parseType(lines[0], &ctx);
  REQUIRE(reference != nullptr);
  auto adt = llvm::dyn_cast<mlir::qtt::ADTType>(reference);
  REQUIRE(adt != nullptr);
  EXPECT(!adt.isInitialized());
  EXPECT(adt.lookupCtor(mlir::StringAttr::get(&ctx, "Lit")) == nullptr);
  EXPECT(printType(adt) == lines[0]);
  EXPECT(mlir::parseType(lines[1], &ctx) == reference);
  REQUIRE(adt.isInitialized());
  const auto original = printType(adt);
  EXPECT(mlir::parseType(lines[1], &ctx) == reference);
  for (size_t i = 2; i < lines.size(); ++i) {
    DiagnosticCapture diags(ctx);
    EXPECT(mlir::parseType(lines[i], &ctx) == nullptr);
    EXPECT(diags.contains("conflicting definition of ADT"));
    EXPECT(printType(adt) == original);
  }
}

void testConstructArityRoundtrip() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  auto ir = readDataFile("construct_arities.mlir");
  if (ir.empty())
    return;
  mlir::OwningOpRef<mlir::ModuleOp> mod;
  REQUIRE(!roundtrip(ctx, mod, ir).empty());
  unsigned count = 0;
  mod->walk([&](mlir::qtt::ConstructOp op) {
    EXPECT(op.getArgs().size() == (count == 0 ? 0 : 2));
    ++count;
  });
  EXPECT(count == 2);
}

void testConstructWrongResult() {
  expectVerifierFailure("construct_wrong_result.mlir", "result must be CtorTYpe");
}

void testConstructTooFewArguments() {
  expectVerifierFailure("construct_too_few.mlir", "expected 1 constructor arguments, got 0");
}

void testConstructTooManyArguments() {
  expectVerifierFailure("construct_too_many.mlir", "expected 1 constructor arguments, got 2");
}

void testUpcastWrongInput() {
  expectVerifierFailure("upcast_wrong_input.mlir", "expected variant -> ADT");
}

void testUpcastWrongResult() {
  expectVerifierFailure("upcast_wrong_result.mlir", "expected variant -> ADT");
}

//===----------------------------------------------------------------------===//
// Constructor payload and ADT body entries.
//===----------------------------------------------------------------------===//

// Same ADT, same constructor name, different payloads: payload is part of the
// CtorType identity, so both references coexist and roundtrip independently.
void testCtorPayloadCoexists() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);

  std::vector<std::string> lines = readDataLines("ctor_payload_coexists.mlir");
  REQUIRE(lines.size() == 3);

  mlir::Type adtType = mlir::parseType(lines[0], &ctx);
  REQUIRE(adtType != nullptr);
  auto adt = llvm::dyn_cast<mlir::qtt::ADTType>(adtType);
  REQUIRE(adt != nullptr);

  mlir::Type i64Ctor = mlir::parseType(lines[1], &ctx);
  mlir::Type i32Ctor = mlir::parseType(lines[2], &ctx);
  REQUIRE(i64Ctor != nullptr);
  REQUIRE(i32Ctor != nullptr);
  EXPECT(i64Ctor != i32Ctor);
  EXPECT(printType(i64Ctor) == lines[1]);
  EXPECT(printType(i32Ctor) == lines[2]);

  // The ADT still resolves its own member, untouched by the extra reference.
  EXPECT(adt.lookupCtor(mlir::StringAttr::get(&ctx, "Lit")) == i64Ctor);
}

// ADT body entries must be constructor types belonging to that very ADT.
void testADTInvalidEntryRejected() {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);

  std::vector<std::string> lines = readDataLines("adt_invalid_entry.mlir");
  REQUIRE(lines.size() == 2);

  EXPECT(mlir::parseType(lines[0], &ctx) == nullptr);
  EXPECT(diags.contains("expected constructor type in ADT body"));

  EXPECT(mlir::parseType(lines[1], &ctx) == nullptr);
  EXPECT(diags.contains("does not belong to ADT"));
}

} // namespace

struct TestCase {
  const char *name;
  void (*run)();
};

int main(int argc, char **argv) {
  const TestCase tests[] = {
      {"ADTCanonicalization", testADTCanonicalization},
      {"DialectRegistration", testDialectRegistration},
      {"QREPRegistration", testQREPRegistration},
      {"ADTTypeRoundtrip", testADTTypeRoundtrip},
      {"ADTTypeArguments", testADTTypeArguments},
      {"ParameterizedConstructUpcast", testParameterizedConstructUpcast},
      {"ADTTypeConflictRejected", testADTTypeConflictRejected},
      {"ADTDuplicateCtorRejected", testADTDuplicateCtorRejected},
      {"CtorTypeRoundtrip", testCtorTypeRoundtrip},
      {"CtorOfUndefinedADTAccepted", testCtorOfUndefinedADTAccepted},
      {"CtorUnknownConstructorRejected", testCtorUnknownConstructorRejected},
      {"UnknownTypeRejected", testUnknownTypeRejected},
      {"IsSubType", testIsSubType},
      {"ConstructUpcastRoundtrip", testConstructUpcastRoundtrip},
      {"UnpackRoundtrip", testUnpackRoundtrip},
      {"CasePrettyPrint", testCasePrettyPrint},
      {"CaseResultsRoundtrip", testCaseResultsRoundtrip},
      {"YieldWrongCount", testYieldWrongCount},
      {"YieldWrongType", testYieldWrongType},
      {"YieldWrongParent", testYieldWrongParent},
      {"CaseMissingYield", testCaseMissingYield},
      {"CaseBranchValidation", testCaseBranchValidation},
      {"CaseIncompleteADTRejected", testCaseIncompleteADTRejected},
      {"PayloadMismatchRejected", testPayloadMismatchRejected},
      {"CrossADTUpcastRejected", testCrossADTUpcastRejected},
      {"ADTRecursiveRoundtrip", testADTRecursiveRoundtrip},
      {"ADTDefinitionLifecycle", testADTDefinitionLifecycle},
      {"ConstructArityRoundtrip", testConstructArityRoundtrip},
      {"ConstructWrongResult", testConstructWrongResult},
      {"ConstructTooFewArguments", testConstructTooFewArguments},
      {"ConstructTooManyArguments", testConstructTooManyArguments},
      {"UpcastWrongInput", testUpcastWrongInput},
      {"UpcastWrongResult", testUpcastWrongResult},
      {"CtorPayloadCoexists", testCtorPayloadCoexists},
      {"ADTInvalidEntryRejected", testADTInvalidEntryRejected},
  };
  bool found = false;
  for (const auto &test : tests) {
    if (argc > 1 && std::string(argv[1]) != test.name)
      continue;
    found = true;
    qtt_test::currentTest = test.name;
    int before = qtt_test::failures;
    test.run();
    std::printf("[%s] %s\n", before == qtt_test::failures ? "PASS" : "FAIL", test.name);
  }
  if (!found) {
    std::fprintf(stderr, "Unknown test: %s\n", argv[1]);
    return 1;
  }
  return qtt_test::failures == 0 ? 0 : 1;
}
