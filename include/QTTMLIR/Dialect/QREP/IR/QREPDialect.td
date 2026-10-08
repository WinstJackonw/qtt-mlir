#ifndef QREP_DIALECT_TD__
#define QREP_DIALECT_TD__

include "mlir/IR/DialectBase.td"

def QREP_Dialect : Dialect {
  let name = "qrep";
  let summary = "an explicit, LLVM-friendly representation of QTT values";
  let description = [{
    QREP is the lowering boundary between QTT's algebraic data type
    operations and LLVM.  Aggregate values are represented as opaque
    references, while constructor and case metadata remains explicit on the
    operations for the later runtime/LLVM lowering.
  }];
  let cppNamespace = "::mlir::qrep";
  let useDefaultTypePrinterParser = false;
}

#endif // QREP_DIALECT_TD__
