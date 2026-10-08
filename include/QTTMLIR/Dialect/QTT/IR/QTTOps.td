#ifndef QTT_OPS_TD__
#define QTT_OPS_TD__

include "QTTMLIR/Dialect/QTT/IR/QTTDialect.td"
include "QTTMLIR/Dialect/QTT/IR/QTTTypes.td"

include "mlir/IR/BuiltinAttributes.td"
include "mlir/IR/OpBase.td"
include "mlir/IR/SymbolInterfaces.td"
include "mlir/Interfaces/SideEffectInterfaces.td"

class QTT_Op<string mnemonic, list<Trait> traits = []>
  : Op<QTT_Dialect , mnemonic, traits>{
}

def QTT_ConstructOp : QTT_Op<"construct", [Pure]> {
  let arguments = (ins Variadic<AnyType>:$args);

  let results = (outs AnyType:$res);

  let assemblyFormat = "$args attr-dict `:`"
                       "functional-type($args,$res)";

  let hasVerifier = 1;
}

def QTT_UnpackOp : QTT_Op<"unpack", [Pure]> {
  // Keep the operand constraint open for dialect conversion.  The verifier
  // still enforces that the value is a constructor at the QTT level.
  let arguments = (ins AnyType:$input);

  let results = (outs Variadic<AnyType>:$args);

  let assemblyFormat = "$input attr-dict `:`"
                       "functional-type($input,$args)";

  let hasVerifier = 1;
}

def QTT_UpcastOp : QTT_Op<"upcast", [Pure]> {
  let arguments = (ins AnyType:$input);

  let results = (outs AnyType:$result);

  let assemblyFormat = "$input attr-dict `:` "
                       "type($input) `to` type($result)";

  let hasVerifier = 1;
}

def QTT_CastOp : QTT_Op<"cast", [Pure]> {
  let arguments = (ins AnyType:$input);

  let results = (outs AnyType:$result);

  let assemblyFormat = "$input attr-dict `:` "
                       "type($input) `to` type($result)";

  let hasVerifier = 1;
}

def QTT_CaseOp : QTT_Op<"case", []> {
  // The verifier enforces ADT-ness; AnyType lets QTT values be remapped to
  // QREP values without an illegal source-type materialization.
  let arguments = (ins AnyType:$input);

  let regions = (region VariadicRegion<AnyRegion>:$cases);

  let hasCustomAssemblyFormat = 1;
  let hasVerifier = 1;
}

#endif
