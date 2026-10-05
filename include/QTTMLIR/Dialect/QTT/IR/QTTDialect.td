#ifndef QTT_DIALECT_TD__
#define QTT_DIALECT_TD__

include "mlir/IR/DialectBase.td"

def QTT_Dialect : Dialect{
  let name = "qtt";
  let summary = "qtt is a mlir dialect designed for high performance fp langague";

  let description = [{ TODO }];

  let cppNamespace = "::mlir::qtt";
  let useDefaultTypePrinterParser = true;
  // let useDefaultAttributePrinterParser = true;
}

#endif
