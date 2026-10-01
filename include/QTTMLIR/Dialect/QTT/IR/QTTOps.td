#ifndef QTT_OPS_TD__
#define QTT_OPS_TD__

include "QTTMLIR/Dialect/QTT/IR/QTTDialect.td"
include "QTTMLIR/Dialect/QTT/IR/QTTTypes.td"

include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"

class QTT_Op<string mnemonic, list<Trait> traits = []>
  : Op<QTT_Dialect , mnemonic, traits>{
}

def QTT_ADTDecl : QTT_Op<"adt_decl", [
  Symbol,
  IsolatedFromAbove,
  SingleBlock,
  NoTerminator
]>{
  let arguments = (ins TypeArrayAttr:$type_params, SymbolNameAttr:$sym_name);
  let regions = (region SizedRegion<1>:$body);

  let assemblyFormat = "$sym_name $body attr-dict";
}

def QTT_ADTCtor : QTT_Op<"adt_ctor", [
  Symbol
]>{
  let arguments = (ins TypeArrayAttr:$type_params, SymbolNameAttr:$sym_name);
  let assemblyFormat = "$sym_name attr-dict";
}

def QTT_ADTConstruct : QTT_Op<"adt_construct", [

]>{
  let arguments = (ins
    SymbolRefAttr:$ctor,
    Variadic<AnyType>:$operands
  );
  let results = (outs QTT_ADTType:$result);

  // let assemblyFormat = "$ctor `(` $operands `)` attr-dict `:` type($result)";
}

#endif
