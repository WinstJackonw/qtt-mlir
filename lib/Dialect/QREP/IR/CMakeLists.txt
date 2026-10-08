add_mlir_dialect_library(QTTMLIRQREPIR
  QREPDialect.cpp
  QREPOps.cpp
  QREPTypes.cpp

  ADDITIONAL_HEADER_DIRS
    ${PROJECT_SOURCE_DIR}/include/QTTMLIR/Dialect/QREP/IR

  DEPENDS
    MLIRQREPOpsIncGen

  LINK_LIBS PUBLIC
    MLIRIR
    MLIRSupport
)
