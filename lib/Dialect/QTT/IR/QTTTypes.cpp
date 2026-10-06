#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"
#include "QTTMLIR/Dialect/QTT/IR/TypeDetail.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/DialectImplementation.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/Casting.h"

#define GET_TYPEDEF_CLASSES
#include "QTTMLIR/Dialect/QTT/IR/QTTOpsTypes.cpp.inc"

namespace {
// The ADT whose body is currently being parsed.
//
// A constructor reference appearing inside its own ADT's body skips the
// membership check: the ADT may be under redefinition, so membership is
// enforced later by setBody instead.
thread_local mlir::qtt::ADTType definingADT;

struct DefiningScope {
  explicit DefiningScope(mlir::qtt::ADTType adt) : previous(definingADT) { definingADT = adt; }
  ~DefiningScope() { definingADT = previous; }

  mlir::qtt::ADTType previous;
};
// Optional arguments follow the ADT name in both ADT and constructor types.
mlir::ParseResult parseTypeArguments(mlir::AsmParser &parser,
                                     llvm::SmallVectorImpl<mlir::Type> &arguments) {
  if (failed(parser.parseOptionalLParen()))
    return mlir::success();
  if (succeeded(parser.parseOptionalRParen()))
    return mlir::success();
  do {
    mlir::Type argument;
    if (failed(parser.parseType(argument)))
      return mlir::failure();
    arguments.push_back(argument);
  } while (succeeded(parser.parseOptionalComma()));
  return parser.parseRParen();
}

void printTypeArguments(mlir::AsmPrinter &printer, llvm::ArrayRef<mlir::Type> arguments) {
  if (arguments.empty())
    return;
  printer << '(';
  llvm::interleaveComma(arguments, printer, [&](mlir::Type type) { printer.printType(type); });
  printer << ')';
}
} // namespace

mlir::StringAttr mlir::qtt::ADTType::getQualifiedName() const { return getImpl()->qualifiedName_; }

llvm::ArrayRef<mlir::Type> mlir::qtt::ADTType::getTypeArguments() const {
  return getImpl()->typeArguments_;
}

bool mlir::qtt::ADTType::isInitialized() const { return getImpl()->initialized_; }

llvm::ArrayRef<mlir::qtt::CtorType> mlir::qtt::ADTType::getCtors() const {
  assert(isInitialized() && "can't inspect an incomplete type");
  return getImpl()->ctors_;
}

mlir::qtt::CtorType mlir::qtt::ADTType::lookupCtor(mlir::StringAttr name) const {
  if (!isInitialized())
    return {};

  for (CtorType ctor : getCtors()) {
    if (ctor.getName() == name)
      return ctor;
  }
  return {};
}

mlir::LogicalResult mlir::qtt::ADTType::setBody(llvm::ArrayRef<mlir::qtt::CtorType> ctors) {
  llvm::StringSet<> seen;

  for (const auto &ctor : ctors) {
    mlir::StringRef name = ctor.getName().getValue();

    if (!seen.insert(name).second)
      return mlir::failure();
  }

  return Base::mutate(ctors);
}

//===----------------------------------------------------------------------===//
// ADTType parser
//===----------------------------------------------------------------------===//
//
// Grammar:
//
//   adt-type ::=
//       `adt` `<` string (`(` type-list? `)`)? `>`
//
//     | `adt` `<` string (`(` type-list? `)`)? `,` `[`
//           ctor-type (`,` ctor-type)*
//       `]` `>`
//
// Examples:
//
//   !qtt.adt<"foo::Expr">
//
//   !qtt.adt<"foo::Expr", [
//     !qtt.ctor<"foo::Expr", "Lit"(i64)>,
//     !qtt.ctor<"foo::Expr", "Nil"()>
//   ]>
//
mlir::Type mlir::qtt::ADTType::parse(mlir::AsmParser &parser) {
  if (failed(parser.parseLess()))
    return {};

  std::string qualifiedName;
  if (failed(parser.parseString(&qualifiedName)))
    return {};

  llvm::SmallVector<Type, 2> typeArguments;
  if (failed(parseTypeArguments(parser, typeArguments)))
    return {};

  mlir::StringAttr nameAttr = parser.getBuilder().getStringAttr(qualifiedName);

  // IMPORTANT:
  //
  // Create the identified type before parsing the body.
  //
  // Therefore recursive references to the same qualified name and arguments
  // resolve to this exact TypeStorage, and constructors stored in
  // the body can point back at this (possibly still incomplete) ADT.
  ADTType result = ADTType::get(parser.getContext(), nameAttr, typeArguments);

  // Short recursive/reference form:
  //
  //   !qtt.adt<"foo::Expr">
  //
  if (succeeded(parser.parseOptionalGreater()))
    return result;

  // Full definition requires:
  //
  //   , [
  //
  if (failed(parser.parseComma()) || failed(parser.parseLSquare()))
    return {};

  llvm::SmallVector<CtorType, 4> ctors;
  llvm::StringSet<> seenCtors;

  // Empty ADT is allowed:
  //
  //   !qtt.adt<"foo::Void", []>
  //
  if (failed(parser.parseOptionalRSquare())) {
    DefiningScope scope(result);

    while (true) {
      Type entry;

      if (failed(parser.parseType(entry)))
        return {};

      auto ctor = llvm::dyn_cast<CtorType>(entry);

      if (!ctor) {
        parser.emitError(parser.getCurrentLocation(), "expected constructor type in ADT body");
        return {};
      }

      if (ctor.getParent() != result) {
        parser.emitError(parser.getCurrentLocation(), "constructor \"")
            << ctor.getName().getValue() << "\" does not belong to ADT \"" << qualifiedName << '"';
        return {};
      }

      if (!seenCtors.insert(ctor.getName().getValue()).second) {
        parser.emitError(parser.getCurrentLocation(), "duplicate constructor name \"")
            << ctor.getName().getValue() << '"';
        return {};
      }

      ctors.push_back(ctor);

      if (succeeded(parser.parseOptionalRSquare()))
        break;

      if (failed(parser.parseComma()))
        return {};
    }
  }

  if (failed(parser.parseGreater()))
    return {};

  if (failed(result.setBody(ctors))) {
    parser.emitError(parser.getCurrentLocation(), "conflicting definition of ADT \"")
        << qualifiedName << '"';

    return {};
  }

  return result;
}

//===----------------------------------------------------------------------===//
// ADTType printer
//===----------------------------------------------------------------------===//

void mlir::qtt::ADTType::print(mlir::AsmPrinter &printer) const {
  printer << '<';
  printer.printString(getQualifiedName().getValue());
  printTypeArguments(printer, getTypeArguments());

  // Recursive printer guard.
  //
  // A constructor payload stored in this ADT may reference the ADT itself:
  //
  //   Self -> [ctor Next(!qtt.adt<"Self">)] -> Self
  //
  // print A fully, then the nested reference as a short reference.
  static thread_local llvm::SmallPtrSet<const void *, 8> activeTypes;

  const void *identity = getAsOpaquePointer();

  if (!isInitialized() || activeTypes.contains(identity)) {
    printer << '>';
    return;
  }

  activeTypes.insert(identity);

  printer << ", [";

  llvm::interleaveComma(getCtors(), printer, [&](CtorType ctor) { printer.printType(ctor); });

  printer << "]>";

  activeTypes.erase(identity);
}

// CtorType
mlir::qtt::ADTType mlir::qtt::CtorType::getParent() const { return getImpl()->parent; }

mlir::StringAttr mlir::qtt::CtorType::getName() const { return getImpl()->name; }

llvm::ArrayRef<mlir::Type> mlir::qtt::CtorType::getPayload() const { return getImpl()->payload; }

mlir::qtt::CtorType mlir::qtt::CtorType::get(::mlir::qtt::ADTType parent, mlir::StringAttr name,
                                             llvm::ArrayRef<mlir::Type> payload) {
  assert(parent && "parent should be valid");

  return Base::get(parent.getContext(), parent, name, payload);
}

//===----------------------------------------------------------------------===//
// CtorType parser
//===----------------------------------------------------------------------===//
//
// Grammar:
//
//   ctor-type ::= `ctor` `<` string (`(` type-list? `)`)? `,` string `(` type-list? `)` `>`
//
// Examples:
//
//   !qtt.ctor<"foo::Expr", "Lit"(i64)>
//
//   !qtt.ctor<"foo::Expr", "Nil"()>
//
// The constructor carries its own payload, so it is always fully defined
// regardless of the state of the referenced ADT. Membership is only checked
// when the ADT is already complete (and the reference does not appear inside
// that ADT's own body).
//
mlir::Type mlir::qtt::CtorType::parse(mlir::AsmParser &parser) {
  if (failed(parser.parseLess()))
    return {};

  std::string parentName;
  if (failed(parser.parseString(&parentName)))
    return {};

  llvm::SmallVector<Type, 2> typeArguments;
  if (failed(parseTypeArguments(parser, typeArguments)))
    return {};

  if (failed(parser.parseComma()))
    return {};

  std::string ctorName;
  if (failed(parser.parseString(&ctorName)))
    return {};

  if (failed(parser.parseLParen()))
    return {};

  llvm::SmallVector<Type, 2> payload;

  // Empty payload:
  //
  //   !qtt.ctor<"foo::Expr", "Nil"()>
  //
  if (failed(parser.parseOptionalRParen())) {
    while (true) {
      Type fieldType;

      if (failed(parser.parseType(fieldType)))
        return {};

      payload.push_back(fieldType);

      if (succeeded(parser.parseOptionalRParen()))
        break;

      if (failed(parser.parseComma()))
        return {};
    }
  }

  if (failed(parser.parseGreater()))
    return {};

  mlir::StringAttr parentNameAttr = parser.getBuilder().getStringAttr(parentName);

  ADTType parent = ADTType::get(parser.getContext(), parentNameAttr, typeArguments);

  mlir::StringAttr ctorAttr = parser.getBuilder().getStringAttr(ctorName);

  if (parent.isInitialized() && definingADT != parent && !parent.lookupCtor(ctorAttr)) {
    parser.emitError(parser.getCurrentLocation(), "ADT \"")
        << parentName << "\" has no constructor \"" << ctorName << '"';

    return {};
  }

  return CtorType::get(parent, ctorAttr, payload);
}

void mlir::qtt::CtorType::print(mlir::AsmPrinter &printer) const {
  printer << '<';

  // Deliberately don't printer.printType(parent):
  //
  // otherwise the entire ADT definition gets expanded here.
  printer.printString(getParent().getQualifiedName().getValue());
  printTypeArguments(printer, getParent().getTypeArguments());

  printer << ", ";

  printer.printString(getName().getValue());

  printer << '(';

  llvm::interleaveComma(getPayload(), printer,
                        [&](Type fieldType) { printer.printType(fieldType); });

  printer << ")>";
}

// Subtyping

// Currently only Ctor is the subtype of an ADT Type
// If more types are introduced, it should also change.
bool mlir::qtt::isSubType(mlir::Type sub, mlir::Type super) {
  if (sub == super)
    return true;

  auto ctor = llvm::dyn_cast<mlir::qtt::CtorType>(sub);
  if (!ctor)
    return false;
  return ctor.getParent() == super;
}
