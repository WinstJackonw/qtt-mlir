#ifndef QTT_TYPES_TD__
#define QTT_TYPES_TD__

include "mlir/IR/AttrTypeBase.td"
include "QTTMLIR/Dialect/QTT/IR/QTTDialect.td"

class QTT_Type<string name, string Mnemonic, list<Trait> traits = []>  : TypeDef<QTT_Dialect, name, traits>{
  let mnemonic = Mnemonic;
}

def QTT_ADTType : QTT_Type<"ADT", "adt", []>{
  let parameters = (ins "::mlir::SymbolRefAttr":$symbol);
  let assemblyFormat = "`<` $symbol `>`";
}

#endif
