#ifndef QREP_TYPES_TD__
#define QREP_TYPES_TD__

include "mlir/IR/AttrTypeBase.td"
include "QTTMLIR/Dialect/QREP/IR/QREPDialect.td"

class QREP_Type<string name, string Mnemonic, list<Trait> traits = []>
    : TypeDef<QREP_Dialect, name, traits> {
  let mnemonic = Mnemonic;
}

#endif // QREP_TYPES_TD__
