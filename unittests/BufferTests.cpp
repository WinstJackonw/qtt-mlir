#include "QTTMLIR/Conversion/QTTToQREP/QTTToQREP.h"
#include "TestSupport.h"
#include "mlir/Conversion/SCFToControlFlow/SCFToControlFlow.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Target/LLVMIR/Dialect/Builtin/BuiltinToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Dialect/LLVMIR/LLVMToLLVMIRTranslation.h"
#include "mlir/Target/LLVMIR/Export.h"
#include "runtime.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include <cstdint>
#include <unordered_set>

using namespace mlir;
using namespace qtt_test;

namespace {
std::unordered_set<void *> liveAllocations;
unsigned allocationCalls = 0;
unsigned freeCalls = 0;

// Test-only hooks retain the real runtime behavior and audit the JIT boundary.
void *trackedMalloc(size_t size) {
  ++allocationCalls;
  void *pointer = qtt_malloc(size);
  if (pointer)
    EXPECT(liveAllocations.insert(pointer).second);
  return pointer;
}
void trackedFree(void *pointer) {
  ++freeCalls;
  if (pointer && liveAllocations.erase(pointer) != 1) {
    EXPECT(false);
    return;
  }
  qtt_free(pointer);
}
} // namespace

static OwningOpRef<ModuleOp> parseBuffer(MLIRContext &ctx) {
  std::string source = readDataFile("buffer_lowering.mlir");
  const std::string type = "i" + std::to_string(sizeof(size_t) * 8);
  size_t pos;
  while ((pos = source.find("@USIZE@")) != std::string::npos)
    source.replace(pos, 7, type);
  return parseSourceString<ModuleOp>(source, &ctx);
}

static void testBuffer() {
  MLIRContext ctx;
  loadDialects(ctx);
  loadQREP(ctx);
  ctx.loadDialect<LLVM::LLVMDialect, cf::ControlFlowDialect, scf::SCFDialect>();
  registerLLVMDialectTranslation(ctx);
  registerBuiltinDialectTranslation(ctx);
  auto module = parseBuffer(ctx);
  REQUIRE(module);
  auto layout = module->lookupSymbol<qrep::LayoutOp>("box");
  REQUIRE(layout);
  auto storage = dyn_cast<LLVM::LLVMStructType>(layout.getStorage());
  EXPECT(!storage);
  auto pointerStorage = dyn_cast<LLVM::LLVMPointerType>(layout.getStorage());
  REQUIRE(pointerStorage);
  EXPECT(isa<LLVM::LLVMPointerType>(layout.getView()));
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  EXPECT(adt.getCtors().size() == 2);
  EXPECT(isa<NoneType>(layout.getStorageFor(adt.lookupCtor(StringAttr::get(&ctx, "None")))));

  PassManager abstract(&ctx);
  abstract.addPass(createQTTToQREPConversionPass());
  REQUIRE(succeeded(abstract.run(*module)));
  auto make = module->lookupSymbol<func::FuncOp>("make_box");
  EXPECT(isa<qrep::OwnType>(make.getFunctionType().getResult(0)));
  PassManager physical(&ctx);
  physical.addPass(createQREPToStorageConversionPass());
  REQUIRE(succeeded(physical.run(*module)));
  EXPECT(make.getFunctionType().getResult(0) == pointerStorage);

  PassManager llvmPasses(&ctx);
  llvmPasses.addPass(createSCFToControlFlowPass());
  llvmPasses.addPass(createQTTStorageToLLVMConversionPass());
  REQUIRE(succeeded(llvmPasses.run(*module)));
  REQUIRE(succeeded(verify(*module)));
  module->walk([&](Operation *op) {
    EXPECT(isa<ModuleOp>(op) || op->getName().getDialectNamespace() == "llvm");
    EXPECT(!op->hasAttr("qrep.layout"));
    for (Type t : op->getResultTypes())
      EXPECT(!isa<NoneType>(t));
  });
  auto llvmMake = module->lookupSymbol<LLVM::LLVMFuncOp>("make_box");
  REQUIRE(llvmMake);
  EXPECT(llvmMake.getFunctionType().getReturnType() == pointerStorage);

  auto llvmContext = std::make_unique<llvm::LLVMContext>();
  auto translated = translateModuleToLLVMIR(*module, *llvmContext);
  REQUIRE(translated);
  EXPECT(!llvm::verifyModule(*translated, &llvm::errs()));
  EXPECT(translated->getFunction("make_box")->getReturnType()->isPointerTy());

  // Also exercise the public convenience pipeline on a fresh source module.
  auto fresh = parseBuffer(ctx);
  REQUIRE(fresh);
  PassManager pipeline(&ctx);
  buildQTTToLLVMPipeline(pipeline);
  REQUIRE(succeeded(pipeline.run(*fresh)));
  EXPECT(printOp(*fresh) == printOp(*module));

  auto engine = llvm::orc::LLJITBuilder().create();
  if (!engine) {
    llvm::logAllUnhandledErrors(engine.takeError(), llvm::errs());
    EXPECT(false);
    return;
  }
  auto checkError = [](llvm::Error error) {
    if (!error)
      return true;
    llvm::logAllUnhandledErrors(std::move(error), llvm::errs());
    return false;
  };
  llvm::orc::MangleAndInterner mangle((*engine)->getExecutionSession(), (*engine)->getDataLayout());
  llvm::orc::SymbolMap symbols;
  symbols[mangle("qtt_malloc")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&trackedMalloc), llvm::JITSymbolFlags::Exported);
  symbols[mangle("qtt_free")] = llvm::orc::ExecutorSymbolDef(
      llvm::orc::ExecutorAddr::fromPtr(&trackedFree), llvm::JITSymbolFlags::Exported);
  REQUIRE(checkError(
      (*engine)->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)))));
  translated->setDataLayout((*engine)->getDataLayout());
  translated->setTargetTriple((*engine)->getTargetTriple());
  REQUIRE(checkError((*engine)->addIRModule(
      llvm::orc::ThreadSafeModule(std::move(translated), std::move(llvmContext)))));
  auto noneAddress = (*engine)->lookup("_mlir_ciface_test_none");
  if (!noneAddress) {
    llvm::logAllUnhandledErrors(noneAddress.takeError(), llvm::errs());
    EXPECT(false);
    return;
  }
  EXPECT(noneAddress->toPtr<int32_t()>()() == 1);
  EXPECT(allocationCalls == 0 && freeCalls == 0);
  auto boxAddress = (*engine)->lookup("_mlir_ciface_test_box");
  if (!boxAddress) {
    llvm::logAllUnhandledErrors(boxAddress.takeError(), llvm::errs());
    EXPECT(false);
    return;
  }
  auto runBox = boxAddress->toPtr<int32_t()>();
  EXPECT(runBox() == 1);
  EXPECT(allocationCalls == 2 && freeCalls == 2);
  EXPECT(liveAllocations.empty());
}

static void testInvalidLowering() {
  MLIRContext ctx;
  loadDialects(ctx);
  loadQREP(ctx);
  ctx.loadDialect<LLVM::LLVMDialect, cf::ControlFlowDialect, scf::SCFDialect>();
  auto source = parseBuffer(ctx);
  REQUIRE(source);
  DiagnosticCapture diagnostics(ctx);
  PassManager premature(&ctx);
  premature.addPass(createQTTStorageToLLVMConversionPass());
  EXPECT(failed(premature.run(*source)));
  EXPECT(diagnostics.contains("requires QTT/QREP storage lowering"));
  EXPECT(source->lookupSymbol<qrep::LayoutOp>("box"));

  // Only a zero-input None placeholder is a unit. Arbitrary unresolved casts
  // must not be silently legalized by the final conversion.
  auto unsupported = parseSourceString<ModuleOp>(R"mlir(
    module {
      func.func @bad() -> i32 {
        %value = builtin.unrealized_conversion_cast to i32
        return %value : i32
      }
    }
  )mlir",
                                                 &ctx);
  REQUIRE(unsupported);
  PassManager invalid(&ctx);
  invalid.addPass(createQTTStorageToLLVMConversionPass());
  EXPECT(failed(invalid.run(*unsupported)));
  EXPECT(diagnostics.contains("unrealized_conversion_cast"));
}

int main() {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  currentTest = "BufferLLVM";
  testBuffer();
  currentTest = "InvalidLLVMLowering";
  testInvalidLowering();
  return failures ? 1 : 0;
}
