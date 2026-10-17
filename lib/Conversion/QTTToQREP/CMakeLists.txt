add_mlir_conversion_library(QTTMLIRQTTToQREP
  QTTToQREP.cpp
  QTTToLLVM.cpp
  QTTFunctionLowering.cpp

  LINK_LIBS PUBLIC
    QTTMLIRQTTIR
    QTTMLIRQREPIR
    MLIRArithToLLVM
    MLIRControlFlowToLLVM
    MLIRFuncToLLVM
    MLIRSCFToControlFlow
    MLIRLLVMCommonConversion
    MLIRLLVMDialect
    MLIRArithDialect
    MLIRControlFlowDialect
    MLIRSCFDialect
    MLIRSCFTransforms
    MLIRFuncTransforms
    MLIRFuncDialect
    MLIRIR
    MLIRPass
    MLIRSupport
    MLIRTransforms
)
