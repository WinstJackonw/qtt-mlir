add_mlir_conversion_library(QTTMLIRQTTToQREP
  QTTToQREP.cpp

  LINK_LIBS PUBLIC
    QTTMLIRQTTIR
    QTTMLIRQREPIR
    MLIRFuncDialect
    MLIRIR
    MLIRPass
    MLIRSupport
    MLIRTransforms
)
