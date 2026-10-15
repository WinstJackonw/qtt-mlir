#ifndef QREP_TYPES_TD__
#define QREP_TYPES_TD__

include "mlir/IR/AttrTypeBase.td"
include "QTTMLIR/Dialect/QREP/IR/QREPDialect.td"

class QREP_Type<string name, string Mnemonic, list<Trait> traits = []>
    : TypeDef<QREP_Dialect, name, traits> {
  let mnemonic = Mnemonic;
}

class QREP_RepresentationType<string name, string mnemonic>
    : QREP_Type<name, mnemonic> {
  let parameters = (ins "::mlir::FlatSymbolRefAttr":$layout,
                        OptionalParameter<"::mlir::Type">:$constructor);
  let assemblyFormat = "`<` $layout (`,` $constructor^)? `>`";
  let genVerifyDecl = 1;
}

def QREP_OwnType : QREP_RepresentationType<"Own", "own">;
def QREP_ViewType : QREP_RepresentationType<"View", "view">;

#endif // QREP_TYPES_TD__
