add_mlir_dialect_library(QTTMLIRQTTIR
  QTTDialect.cpp
  QTTOps.cpp
  QTTTypes.cpp

  ADDITIONAL_HEADER_DIRS
    ${PROJECT_SOURCE_DIR}/include/QTTMLIR/Dialect/QTT/IR

  DEPENDS
    MLIRQTTOpsIncGen
    # QTTMLIRTypesIncGen

  LINK_LIBS PUBLIC
    MLIRIR
    MLIRSupport
    MLIRFuncDialect
)
