#ifndef QREP_OPS_TD__
#define QREP_OPS_TD__

include "QTTMLIR/Dialect/QREP/IR/QREPDialect.td"
include "QTTMLIR/Dialect/QREP/IR/QREPAttrs.td"
include "QTTMLIR/Dialect/QREP/IR/QREPTypes.td"

include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class QREP_Op<string mnemonic, list<Trait> traits = []> : Op<QREP_Dialect, mnemonic, traits> {}

// A layout owns implementation regions. Only the layout has a symbol;
// functions being lowered select it using qrep.layout = @symbol.
def QREP_LayoutOp : QREP_Op<"layout", [Symbol, IsolatedFromAbove, SingleBlock, NoTerminator]> {
  let summary = "Named ADT storage and representation implementation";
  let arguments = (ins SymbolNameAttr:$sym_name, TypeAttr:$adt,
                       TypeAttr:$storage, TypeAttr:$view, ArrayAttr:$constructors);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "$sym_name attr-dict-with-keyword $body";
  let hasVerifier = 1;
  let extraClassDeclaration = [{
    ::mlir::DictionaryAttr lookupConstructor(::mlir::qtt::CtorType ctor);
    ::mlir::Type getStorageFor(::mlir::Type source);
    ::mlir::Type getViewFor(::mlir::Type source);
  }];
}

// Runtime operations retain layout identity until lower-qrep resolves physical types.
class QREP_Primitive<string mnemonic> : QREP_Op<mnemonic> {
  let arguments = (ins Variadic<AnyType>:$inputs);
  let results = (outs Variadic<AnyType>:$outputs);
  let assemblyFormat = "$inputs attr-dict `:` functional-type($inputs, $outputs)";
  let hasVerifier = 1;
}
def QREP_PackOp : QREP_Primitive<"pack">;
def QREP_UnpackOp : QREP_Primitive<"unpack">;
def QREP_UpcastOp : QREP_Primitive<"upcast">;
def QREP_DowncastOp : QREP_Primitive<"downcast">;
def QREP_BorrowOp : QREP_Primitive<"borrow">;

// Ownership operations are resolved through the selected layout just like
// pack/unpack. They are kept separate from the layout implementation ops so
// that ownership effects are visible in executable IR.
def QREP_CloneOp : QREP_Primitive<"clone">;
def QREP_DropOp : QREP_Primitive<"drop">;

def QREP_CaseOp : QREP_Op<"case"> {
  let arguments = (ins AnyType:$input);
  let results = (outs Variadic<AnyType>:$results);
  let regions = (region VariadicRegion<AnyRegion>:$cases);
  let hasVerifier = 1;
}
def QREP_CaseYieldOp : QREP_Op<"case_yield", [Terminator, Pure, HasParent<"CaseOp">]> {
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "attr-dict ($values^ `:` type($values))?";
  let hasVerifier = 1;
}

def QREP_ImplOp : QREP_Op<"impl", [IsolatedFromAbove, HasParent<"LayoutOp">]> {
  let summary = "A layout primitive implemented by a typed region";
  let arguments = (ins QREP_ImplKindAttr:$kind, OptionalAttr<TypeAttr>:$constructor);
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "$kind `` custom<ImplementationBody>($body) attr-dict";
  let hasVerifier = 1;
}

// An eliminator is a control-flow template, not a function returning a tag.
// Its exits invoke constructor continuations supplied by qtt.case lowering.
def QREP_DiscriminateOp : QREP_Op<"discriminate", [IsolatedFromAbove, HasParent<"LayoutOp">]> {
  let summary = "Layout-owned ADT elimination with typed constructor exits";
  let regions = (region AnyRegion:$body);
  let assemblyFormat = "custom<ImplementationBody>($body) attr-dict";
  let hasVerifier = 1;
  let hasRegionVerifier = 1;
}

def QREP_DispatchOp : QREP_Op<"dispatch", [Terminator, HasParent<"DiscriminateOp">]> {
  let summary = "Invoke a constructor continuation with decoded storage";
  let arguments = (ins TypeAttr:$constructor, AnyType:$value);
  let assemblyFormat = "$constructor `,` $value attr-dict `:` type($value)";
  let hasVerifier = 1;
}

def QREP_YieldOp : QREP_Op<"yield", [Pure, Terminator, HasParent<"ImplOp">]> {
  let arguments = (ins Variadic<AnyType>:$values);
  let assemblyFormat = "attr-dict ($values^ `:` type($values))?";
}

#endif // QREP_OPS_TD__
