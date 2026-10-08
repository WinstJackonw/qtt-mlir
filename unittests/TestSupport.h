#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "mlir/AsmParser/AsmParser.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Diagnostics.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/Verifier.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/LLVM.h"
#include "llvm/Support/raw_ostream.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPDialect.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTDialect.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"

#ifndef QTT_TEST_DATA_DIR
#define QTT_TEST_DATA_DIR "data"
#endif

namespace qtt_test {

inline int failures = 0;
inline const char *currentTest = "unknown";

#define EXPECT(cond)                                                                               \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::fprintf(stderr, "FAILED [%s]: %s (%s:%d)\n", currentTest, #cond, __FILE__, __LINE__);   \
      ++failures;                                                                                  \
    }                                                                                              \
  } while (0)

#define REQUIRE(cond)                                                                              \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      std::fprintf(stderr, "FAILED [%s]: %s (%s:%d)\n", currentTest, #cond, __FILE__, __LINE__);   \
      ++failures;                                                                                  \
      return;                                                                                      \
    }                                                                                              \
  } while (0)

//===----------------------------------------------------------------------===//
// Test data helpers.
//===----------------------------------------------------------------------===//

inline std::string readDataFile(const std::string &name) {
  std::string path = std::string(QTT_TEST_DATA_DIR) + "/" + name;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    std::fprintf(stderr, "FAILED: cannot open test data file %s\n", path.c_str());
    ++failures;
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  std::string contents = buffer.str();
  if (input.bad() || contents.empty()) {
    std::fprintf(stderr, "FAILED [%s]: empty or unreadable file %s\n", currentTest, path.c_str());
    ++failures;
    return {};
  }
  return contents;
}

// Reads `name` and returns its non-empty, non-comment lines. Used for data
// files holding a sequence of type-level inputs, parsed one line at a time.
inline std::vector<std::string> readDataLines(const std::string &name) {
  std::vector<std::string> lines;
  std::istringstream stream(readDataFile(name));
  std::string line;
  while (std::getline(stream, line)) {
    llvm::StringRef trimmed(line);
    if (trimmed.ends_with('\r'))
      trimmed = trimmed.drop_back();
    trimmed = trimmed.trim();
    if (trimmed.empty() || trimmed.starts_with("//"))
      continue;
    lines.push_back(trimmed.str());
  }
  return lines;
}

// Captures diagnostics emitted on a context instead of printing them to
// stderr, so negative tests can assert on the emitted message.
class DiagnosticCapture {
public:
  explicit DiagnosticCapture(mlir::MLIRContext &ctx)
      : handler(&ctx, [this](mlir::Diagnostic &diag) {
          llvm::raw_string_ostream os(text);
          diag.print(os);
          os.flush();
          text += '\n';
          return mlir::success();
        }) {}

  const std::string &getText() const { return text; }

  bool contains(llvm::StringRef fragment) const { return text.find(fragment) != std::string::npos; }

private:
  std::string text;
  mlir::ScopedDiagnosticHandler handler;
};

inline void loadDialects(mlir::MLIRContext &ctx) {
  ctx.loadDialect<mlir::qtt::QTTDialect>();
  ctx.getOrLoadDialect<mlir::func::FuncDialect>();
}

inline void loadQREP(mlir::MLIRContext &ctx) { ctx.loadDialect<mlir::qrep::QREPDialect>(); }

inline std::string printOp(mlir::Operation *op) {
  std::string printed;
  llvm::raw_string_ostream os(printed);
  op->print(os);
  os.flush();
  return printed;
}

inline std::string printType(mlir::Type type) {
  std::string printed;
  llvm::raw_string_ostream os(printed);
  type.print(os);
  os.flush();
  return printed;
}

// Parse `ir`, verify, print, re-parse the printed form, verify it again and
// check that printing is stable. Returns the printed IR, or an empty string
// on failure.
inline std::string roundtrip(mlir::MLIRContext &ctx, mlir::OwningOpRef<mlir::ModuleOp> &mod,
                             llvm::StringRef ir) {
  // Parse without the built-in post-parse verification so that parse and
  // verify failures are reported separately below.
  mod = mlir::parseSourceString<mlir::ModuleOp>(
      ir, mlir::ParserConfig(&ctx, /*verifyAfterParse=*/false));
  if (!mod) {
    std::fprintf(stderr, "FAILED: could not parse input IR:\n%s\n", ir.str().c_str());
    ++failures;
    return {};
  }
  if (failed(mlir::verify(*mod))) {
    std::fprintf(stderr, "FAILED: input IR did not verify:\n%s\n", ir.str().c_str());
    ++failures;
    return {};
  }

  std::string printed = printOp(*mod);

  mlir::MLIRContext freshContext;
  loadDialects(freshContext);
  auto reparsed = mlir::parseSourceString<mlir::ModuleOp>(
      printed, mlir::ParserConfig(&freshContext, /*verifyAfterParse=*/false));
  if (!reparsed) {
    std::fprintf(stderr, "FAILED: could not reparse printed IR:\n%s\n", printed.c_str());
    ++failures;
    return {};
  }
  if (failed(mlir::verify(*reparsed))) {
    std::fprintf(stderr, "FAILED: printed IR did not verify:\n%s\n", printed.c_str());
    ++failures;
    return {};
  }
  if (printed != printOp(*reparsed)) {
    std::fprintf(stderr, "FAILED: printed IR is not stable:\n%s\n", printed.c_str());
    ++failures;
    return {};
  }
  return printed;
}

inline void expectVerifierFailure(const std::string &file, llvm::StringRef message) {
  mlir::MLIRContext ctx;
  loadDialects(ctx);
  DiagnosticCapture diags(ctx);
  std::string ir = readDataFile(file);
  if (ir.empty())
    return;
  auto mod = mlir::parseSourceString<mlir::ModuleOp>(
      ir, mlir::ParserConfig(&ctx, /*verifyAfterParse=*/false));
  REQUIRE(mod.get() != nullptr);
  EXPECT(failed(mlir::verify(*mod)));
  if (!diags.contains(message)) {
    std::fprintf(stderr, "FAILED [%s]: expected diagnostic '%s', got:\n%s\n", currentTest,
                 message.str().c_str(), diags.getText().c_str());
    ++failures;
  }
}
} // namespace qtt_test
