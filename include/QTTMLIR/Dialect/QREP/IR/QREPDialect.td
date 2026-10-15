#ifndef QREP_DIALECT_TD__
#define QREP_DIALECT_TD__

include "mlir/IR/DialectBase.td"

def QREP_Dialect : Dialect {
  let name = "qrep";
  let summary = "an explicit, LLVM-friendly representation of QTT values";
  let description = [{
    QREP makes the storage and primitive operations of QTT values explicit.
    Named layouts own storage choices and implementation regions.
  }];
  let extraClassDeclaration = [{
    void initializeAttributes();
    void initializeTypes();
  }];
  let dependentDialects = ["::mlir::qtt::QTTDialect"];
  let cppNamespace = "::mlir::qrep";
  let useDefaultAttributePrinterParser = 1;
  let useDefaultTypePrinterParser = 1;
}

#endif // QREP_DIALECT_TD__
