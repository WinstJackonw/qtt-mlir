#ifndef QTT_TYPES_TD__
#define QTT_TYPES_TD__

include "mlir/IR/AttrTypeBase.td"
include "QTTMLIR/Dialect/QTT/IR/QTTDialect.td"

class QTT_Type<string name, string Mnemonic, list<Trait> traits = []>  : TypeDef<QTT_Dialect, name, traits>{
  let mnemonic = Mnemonic;
}

def QTT_ADTType : QTT_Type<"ADT", "adt", [MutableType]> {
  let parameters = (ins "::mlir::StringAttr":$qualifiedName);

  let genStorageClass = 0;
  let storageClass = "ADTTypeStorage";
  let storageNamespace = "details";

  let hasCustomAssemblyFormat = 1;

  let extraClassDeclaration = [{
    bool isInitialized() const;

    ::llvm::ArrayRef<::mlir::qtt::CtorType>
    getCtors() const;

    ::mlir::qtt::CtorType
    lookupCtor(::mlir::StringAttr ctorName) const;

    ::mlir::LogicalResult
    setBody(::llvm::ArrayRef<::mlir::qtt::CtorType> ctors);
  }];
}

def QTT_CtorType : QTT_Type<"Ctor", "ctor", []> {
  let parameters = (ins "::mlir::qtt::ADTType":$parent, "::mlir::StringAttr":$name,
      "::llvm::ArrayRef<::mlir::Type>":$payload);

  let genStorageClass = 0;
  let storageClass = "CtorTypeStorage";
  let storageNamespace = "details";

  let skipDefaultBuilders = 1;
  let hasCustomAssemblyFormat = 1;
  let extraClassDeclaration = [{
    static CtorType
    get(::mlir::qtt::ADTType parent,
        ::mlir::StringAttr name,
        ::llvm::ArrayRef<::mlir::Type> payload);
  }];
}

#endif // QTT_TYPES_TD__
