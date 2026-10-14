add_mlir_conversion_library(QTTMLIRQTTToQREP
  QTTToQREP.cpp

  LINK_LIBS PUBLIC
    QTTMLIRQTTIR
    QTTMLIRQREPIR
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
