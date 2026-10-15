#ifndef QREP_ATTRS_TD__
#define QREP_ATTRS_TD__

include "QTTMLIR/Dialect/QREP/IR/QREPDialect.td"
include "mlir/IR/EnumAttr.td"

def QREP_ImplKind : I32EnumAttr<"ImplKind", "layout primitive kind", [
    I32EnumAttrCase<"Pack", 0, "pack">,
    I32EnumAttrCase<"Upcast", 1, "upcast">,
    I32EnumAttrCase<"Downcast", 2, "downcast">,
    I32EnumAttrCase<"Unpack", 3, "unpack">,
    I32EnumAttrCase<"Borrow", 4, "borrow">
]> {
  let cppNamespace = "::mlir::qrep";
  let genSpecializedAttr = 0;
}

def QREP_ImplKindAttr : EnumAttr<QREP_Dialect, QREP_ImplKind, "impl_kind">;

#endif // QREP_ATTRS_TD__
