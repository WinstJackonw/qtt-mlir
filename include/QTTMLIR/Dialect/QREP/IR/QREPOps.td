#ifndef QREP_OPS_TD__
#define QREP_OPS_TD__

include "QTTMLIR/Dialect/QREP/IR/QREPDialect.td"
include "QTTMLIR/Dialect/QREP/IR/QREPTypes.td"

include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class QREP_Op<string mnemonic, list<Trait> traits = []> : Op<QREP_Dialect, mnemonic, traits> {}

#endif // QREP_OPS_TD__
