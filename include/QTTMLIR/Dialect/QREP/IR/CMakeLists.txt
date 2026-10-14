add_mlir_dialect(QREPOps qrep)

set(LLVM_TARGET_DEFINITIONS QREPAttrs.td)
mlir_tablegen(QREPEnums.h.inc -gen-enum-decls)
mlir_tablegen(QREPEnums.cpp.inc -gen-enum-defs)
mlir_tablegen(QREPAttrs.h.inc -gen-attrdef-decls -attrdefs-dialect=qrep)
mlir_tablegen(QREPAttrs.cpp.inc -gen-attrdef-defs -attrdefs-dialect=qrep)
add_mlir_dialect_tablegen_target(MLIRQREPAttrsIncGen)
