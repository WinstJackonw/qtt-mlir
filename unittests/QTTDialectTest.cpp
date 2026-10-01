// Minimal tests for the QTT dialect: registration, parsing and print roundtrip.
#include <cstdio>
#include <string>

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/LLVM.h"

#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"

namespace {

int failures = 0;

#define EXPECT(cond)                                                           \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::fprintf(stderr, "FAILED: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

// Parse `ir`, print it back and parse the printed form again. Returns the
// printed IR, or an empty string on failure.
std::string roundtrip(mlir::MLIRContext &ctx, mlir::OwningOpRef<mlir::ModuleOp> &mod,
                      const std::string &ir) {
  mod = mlir::parseSourceString<mlir::ModuleOp>(ir, mlir::ParserConfig(&ctx));
  if (!mod) {
    std::fprintf(stderr, "FAILED: could not parse input IR:\n%s\n", ir.c_str());
    ++failures;
    return {};
  }
  if (failed(mlir::verify(*mod))) {
    std::fprintf(stderr, "FAILED: input IR did not verify:\n%s\n", ir.c_str());
    ++failures;
    return {};
  }

  std::string printed;
  llvm::raw_string_ostream os(printed);
  mod->print(os);
  os.flush();

  auto reparsed =
      mlir::parseSourceString<mlir::ModuleOp>(printed, mlir::ParserConfig(&ctx));
  if (!reparsed) {
    std::fprintf(stderr, "FAILED: could not reparse printed IR:\n%s\n",
                 printed.c_str());
    ++failures;
    return {};
  }
  if (failed(mlir::verify(*reparsed))) {
    std::fprintf(stderr, "FAILED: printed IR did not verify:\n%s\n",
                 printed.c_str());
    ++failures;
    return {};
  }
  return printed;
}

void testDialectRegistration() {
  mlir::MLIRContext ctx;
  ctx.loadDialect<qtt::QTTDialect>();
  EXPECT(ctx.getLoadedDialect<qtt::QTTDialect>() != nullptr);
  EXPECT(ctx.getLoadedDialect("qtt") != nullptr);
}

void testADTDeclRoundtrip() {
  mlir::MLIRContext ctx;
  ctx.loadDialect<qtt::QTTDialect>();
  mlir::OwningOpRef<mlir::ModuleOp> mod;
  std::string printed = roundtrip(
      ctx, mod,
      "module {\n"
      "  qtt.adt_decl @Pair {} { type_params = [] }\n"
      "}\n");
  if (printed.empty())
    return;
  EXPECT(printed.find("adt_decl") != std::string::npos);
  EXPECT(mod->lookupSymbol<qtt::ADTDecl>("Pair") != nullptr);
}

void testADTCtorAndConstructRoundtrip() {
  mlir::MLIRContext ctx;
  ctx.loadDialect<qtt::QTTDialect>();
  mlir::OwningOpRef<mlir::ModuleOp> mod;
  std::string printed = roundtrip(
      ctx, mod,
      "module {\n"
      "  qtt.adt_ctor @pair_ctor { type_params = [] }\n"
      "  %0 = \"qtt.adt_construct\"() {ctor = @pair_ctor} : () -> "
      "!qtt.adt<@pair_ctor>\n"
      "}\n");
  if (printed.empty())
    return;
  EXPECT(printed.find("adt_ctor") != std::string::npos);
  EXPECT(printed.find("adt_construct") != std::string::npos);
  EXPECT(printed.find("!qtt.adt<@pair_ctor>") != std::string::npos);

  auto construct = mod->lookupSymbol<qtt::ADTCtor>("pair_ctor");
  EXPECT(construct != nullptr);
}

void testADTType() {
  mlir::MLIRContext ctx;
  ctx.loadDialect<qtt::QTTDialect>();

  // Roundtrip a module containing only the type, via a function-less carrier:
  // parse the type string directly and check identity through printing.
  auto type = mlir::parseType("!qtt.adt<@some_ctor>", &ctx);
  EXPECT(type != nullptr);
  if (!type)
    return;
  auto adtType = llvm::dyn_cast<qtt::ADTType>(type);
  EXPECT(adtType != nullptr);
  if (!adtType)
    return;
  EXPECT(adtType.getSymbol().getRootReference() ==
         mlir::StringAttr::get(&ctx, "some_ctor"));

  std::string printed;
  llvm::raw_string_ostream os(printed);
  type.print(os);
  os.flush();
  EXPECT(printed == "!qtt.adt<@some_ctor>");
}

void testInvalidIRDiagnostics() {
  mlir::MLIRContext ctx;
  ctx.loadDialect<qtt::QTTDialect>();
  // Unknown type mnemonic must fail to parse.
  auto type = mlir::parseType("!qtt.unknown<@x>", &ctx);
  EXPECT(type == nullptr);
}

} // namespace

int main() {
  testDialectRegistration();
  testADTDeclRoundtrip();
  testADTCtorAndConstructRoundtrip();
  testADTType();
  testInvalidIRDiagnostics();

  if (failures != 0) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("all checks passed\n");
  return 0;
}
