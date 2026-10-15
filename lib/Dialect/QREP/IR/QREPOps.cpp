#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "llvm/ADT/DenseSet.h"

// Entry block arguments belong in the implementation header. Discriminate
// bodies may also contain explicitly labeled CFG blocks.
static mlir::ParseResult parseImplementationBody(mlir::OpAsmParser &parser, mlir::Region &body) {
  llvm::SmallVector<mlir::OpAsmParser::Argument> arguments;
  if (parser.parseArgumentList(arguments, mlir::OpAsmParser::Delimiter::Paren,
                               /*allowType=*/true))
    return mlir::failure();
  return parser.parseRegion(body, arguments);
}

static void printImplementationBody(mlir::OpAsmPrinter &printer, mlir::Operation *,
                                    mlir::Region &body) {
  printer << "(";
  llvm::interleaveComma(body.front().getArguments(), printer,
                        [&](mlir::BlockArgument arg) { printer.printRegionArgument(arg); });
  printer << ") ";
  printer.printRegion(body, /*printEntryBlockArgs=*/false);
}

#define GET_OP_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.cpp.inc"

using namespace mlir;
using namespace mlir::qrep;

DictionaryAttr LayoutOp::lookupConstructor(qtt::CtorType ctor) {
  for (Attribute entry : getConstructors())
    if (auto dict = dyn_cast<DictionaryAttr>(entry))
      if (dict.getAs<TypeAttr>("constructor") == TypeAttr::get(ctor))
        return dict;
  return {};
}

Type LayoutOp::getStorageFor(Type source) {
  if (source == getAdt())
    return getStorage();
  if (auto ctor = dyn_cast<qtt::CtorType>(source)) {
    if (ctor.getParent() != getAdt())
      return {};
    if (auto entry = lookupConstructor(ctor))
      if (auto storage = entry.getAs<TypeAttr>("storage"))
        return storage.getValue();
    return {};
  }
  return isa<qtt::ADTType>(source) ? Type{} : source;
}

Type LayoutOp::getViewFor(Type source) {
  if (source == getAdt())
    return getView();
  if (auto ctor = dyn_cast<qtt::CtorType>(source)) {
    if (auto entry = lookupConstructor(ctor))
      if (auto view = entry.getAs<TypeAttr>("view"))
        return view.getValue();
    return {};
  }
  return isa<qtt::ADTType>(source) ? Type{} : source;
}

ImplOp qrep::lookupImpl(LayoutOp layout, ImplKind kind, qtt::CtorType ctor) {
  for (auto impl : layout.getBody().front().getOps<ImplOp>()) {
    Type source = impl.getConstructor().value_or(Type{});
    if (impl.getKind() == kind && source == ctor)
      return impl;
  }
  return {};
}

DiscriminateOp qrep::lookupDiscriminate(LayoutOp layout) {
  for (auto discriminate : layout.getBody().front().getOps<DiscriminateOp>())
    return discriminate;
  return {};
}

static LogicalResult implSignature(LayoutOp layout, ImplKind kind, qtt::CtorType ctor,
                                   SmallVectorImpl<Type> &inputs,
                                   SmallVectorImpl<Type> &results) {
  bool ownership = kind == ImplKind::Clone || kind == ImplKind::Drop;
  if (kind == ImplKind::Borrow && !ctor) {
    inputs.push_back(layout.getStorage());
    results.push_back(layout.getView());
    return success();
  }
  if (ownership && !ctor) {
    inputs.push_back(layout.getView());
    if (kind == ImplKind::Clone)
      results.push_back(layout.getView());
    return success();
  }
  if (!ctor || ctor.getParent() != layout.getAdt())
    return failure();
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  if (adt.lookupCtor(ctor.getName()) != ctor)
    return failure();
  Type storage = layout.getStorageFor(ctor);
  Type view = layout.getViewFor(ctor);
  if (!storage || !view)
    return failure();
  if (ownership) {
    inputs.push_back(view);
    if (kind == ImplKind::Clone)
      results.push_back(view);
    return success();
  }
  SmallVector<Type> fields;
  for (Type payload : ctor.getPayload()) {
    Type field =
        kind == ImplKind::Pack ? layout.getViewFor(payload) : layout.getStorageFor(payload);
    if (!field)
      return failure();
    fields.push_back(field);
  }
  switch (kind) {
  case ImplKind::Clone:
  case ImplKind::Drop:
    llvm_unreachable("ownership signatures handled above");
  case ImplKind::Pack:
    inputs.append(fields);
    results.push_back(storage);
    return success();
  case ImplKind::Upcast:
    inputs.push_back(storage);
    results.push_back(layout.getStorage());
    return success();
  case ImplKind::Downcast:
    inputs.push_back(layout.getStorage());
    results.push_back(storage);
    return success();
  case ImplKind::Unpack:
    inputs.push_back(storage);
    results.append(fields);
    return success();
  case ImplKind::Borrow:
    inputs.push_back(storage);
    results.push_back(view);
    return success();
  }
  return failure();
}

LogicalResult ImplOp::verify() {
  auto layout = cast<LayoutOp>((*this)->getParentOp());
  auto adt = dyn_cast<qtt::ADTType>(layout.getAdt());
  if (!adt || !adt.isInitialized())
    return emitOpError("parent layout requires a defined ADT");
  auto ctor = dyn_cast_or_null<qtt::CtorType>(getConstructor().value_or(Type{}));
  if (getConstructor() && !ctor)
    return emitOpError("constructor must be a CtorType");
  SmallVector<Type> inputs, results;
  if (failed(implSignature(layout, getKind(), ctor, inputs, results)))
    return emitOpError("unknown primitive or invalid constructor/storage mapping");
  if (!llvm::hasSingleElement(getBody()))
    return emitOpError("requires one implementation block");
  Block &block = getBody().front();
  if (block.getArgumentTypes() != TypeRange(inputs))
    return emitOpError("implementation arguments do not match storage signature");
  auto yield = block.empty() ? YieldOp{} : dyn_cast<YieldOp>(block.back());
  if (!yield || yield.getOperandTypes() != TypeRange(results))
    return emitOpError("implementation yield does not match storage signature");
  return success();
}

LogicalResult DiscriminateOp::verify() {
  auto layout = cast<LayoutOp>((*this)->getParentOp());
  auto adt = dyn_cast<qtt::ADTType>(layout.getAdt());
  if (!adt || !adt.isInitialized())
    return emitOpError("parent layout requires a defined ADT");
  if (getBody().empty() || getBody().front().getArgumentTypes() != TypeRange{layout.getStorage()})
    return emitOpError("entry arguments must be the ADT storage signature");
  return success();
}

// Inspect exits only after their operand/attribute invariants and constructor
// storage contracts have been verified.
LogicalResult DiscriminateOp::verifyRegions() {
  auto layout = cast<LayoutOp>((*this)->getParentOp());
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  llvm::DenseSet<Type> exits;
  for (Block &block : getBody()) {
    if (block.empty())
      return emitOpError("each block must end with a branch or qrep.dispatch");
    Operation &terminator = block.back();
    if (auto dispatch = dyn_cast<DispatchOp>(terminator)) {
      exits.insert(dispatch.getConstructor());
      continue;
    }
    if (!isa<BranchOpInterface>(terminator) || terminator.getNumSuccessors() == 0)
      return emitOpError("each block must end with a branch or qrep.dispatch");
    for (Block *successor : terminator.getSuccessors())
      if (successor->getParent() != &getBody())
        return emitOpError("branches must stay within the discriminate body");
  }
  for (auto ctor : adt.getCtors())
    if (!exits.contains(ctor))
      return emitOpError() << "missing dispatch for constructor " << ctor.getName();
  return success();
}

LogicalResult DispatchOp::verify() {
  auto discriminate = cast<DiscriminateOp>((*this)->getParentOp());
  auto layout = cast<LayoutOp>(discriminate->getParentOp());
  auto ctor = dyn_cast<qtt::CtorType>(getConstructor());
  auto adt = dyn_cast<qtt::ADTType>(layout.getAdt());
  if (!ctor || !adt || !adt.isInitialized() || ctor.getParent() != adt ||
      adt.lookupCtor(ctor.getName()) != ctor)
    return emitOpError("constructor must be declared by the parent layout ADT");
  if (getValue().getType() != layout.getStorageFor(ctor))
    return emitOpError("dispatch value must match constructor storage");
  return success();
}

LogicalResult LayoutOp::verify() {
  auto adt = dyn_cast<qtt::ADTType>(getAdt());
  if (!adt || !adt.isInitialized() || adt.getCtors().empty())
    return emitOpError("requires a defined nonempty ADT");
  auto isConcrete = [](Type type) {
    return type && !isa<qtt::ADTType, qtt::CtorType, OwnType, ViewType, NoneType>(type);
  };
  auto isConstructorStorage = [](Type type) {
    return type && !isa<qtt::ADTType, qtt::CtorType, OwnType, ViewType>(type);
  };
  if (!isConcrete(getStorage()))
    return emitOpError("requires concrete ADT storage");
  if (!isConcrete(getView()))
    return emitOpError("requires concrete ADT view");
  if (getConstructors().size() != adt.getCtors().size())
    return emitOpError("requires exactly one entry per constructor");
  llvm::DenseSet<Type> constructors;
  for (Attribute attr : getConstructors()) {
    auto entry = dyn_cast<DictionaryAttr>(attr);
    if (!entry)
      return emitOpError("constructor entries must be dictionaries");
    auto constructor = entry.getAs<TypeAttr>("constructor");
    auto ctor = constructor ? dyn_cast<qtt::CtorType>(constructor.getValue()) : qtt::CtorType{};
    auto storage = entry.getAs<TypeAttr>("storage");
    auto view = entry.getAs<TypeAttr>("view");
    if (!ctor || ctor.getParent() != adt || adt.lookupCtor(ctor.getName()) != ctor ||
        !constructors.insert(ctor).second)
      return emitOpError("constructors must be declared CtorTypes and unique");
    if (!storage || !isConstructorStorage(storage.getValue()))
      return emitOpError("requires concrete constructor storage or none");
    if (!view || !isConstructorStorage(view.getValue()))
      return emitOpError("requires concrete constructor view or none");
  }
  if (getBody().front().getNumArguments() != 0)
    return emitOpError("layout body cannot have runtime arguments");
  llvm::DenseSet<std::pair<ImplKindAttr, Type>> impls;
  unsigned discriminators = 0;
  for (Operation &op : getBody().front()) {
    if (isa<DiscriminateOp>(op)) {
      if (++discriminators > 1)
        return emitOpError("duplicate discriminate implementation");
      continue;
    }
    auto impl = dyn_cast<ImplOp>(op);
    if (!impl)
      return emitOpError("body may only contain qrep.impl and qrep.discriminate operations");
    if (!impls.insert({impl.getKindAttr(), impl.getConstructor().value_or(Type{})}).second)
      return emitOpError("duplicate primitive implementation");
  }
  if (!discriminators)
    return emitOpError("missing discriminate implementation");
  if (!lookupImpl(*this, ImplKind::Borrow))
    return emitOpError("missing borrow implementation for ADT");
  for (ImplKind kind : {ImplKind::Clone, ImplKind::Drop})
    if (!lookupImpl(*this, kind))
      return emitOpError() << "missing " << stringifyImplKind(kind)
                           << " implementation for ADT";
  for (auto ctor : adt.getCtors())
    for (ImplKind kind :
         {ImplKind::Pack, ImplKind::Upcast, ImplKind::Downcast, ImplKind::Unpack,
          ImplKind::Borrow, ImplKind::Clone, ImplKind::Drop})
      if (!lookupImpl(*this, kind, ctor))
        return emitOpError() << "missing " << stringifyImplKind(kind) << " implementation for "
                             << ctor.getName();
  return success();
}

// Resolve symbols at the use, since representation types themselves have no scope.
static LayoutOp resolveRepresentation(Operation *op, Type type) {
  FlatSymbolRefAttr ref;
  Type constructor;
  if (auto own = dyn_cast<OwnType>(type)) {
    ref = own.getLayout();
    constructor = own.getConstructor();
  } else if (auto view = dyn_cast<ViewType>(type)) {
    ref = view.getLayout();
    constructor = view.getConstructor();
  } else {
    return {};
  }
  auto layout = SymbolTable::lookupNearestSymbolFrom<LayoutOp>(op, ref);
  if (!layout) {
    op->emitOpError("representation type must reference a qrep.layout symbol");
    return {};
  }
  if (constructor && !layout.lookupConstructor(cast<qtt::CtorType>(constructor))) {
    op->emitOpError("constructor refinement must be declared by the referenced layout");
    return {};
  }
  return layout;
}

static Type representation(LayoutOp layout, Type source, bool view) {
  Type ctor;
  if (isa<qtt::CtorType>(source))
    ctor = source;
  else if (!isa<qtt::ADTType>(source))
    return source;
  if (!layout.getStorageFor(source))
    return {};
  auto ref = FlatSymbolRefAttr::get(layout.getSymNameAttr());
  return view ? Type(ViewType::get(layout.getContext(), ref, ctor))
              : Type(OwnType::get(layout.getContext(), ref, ctor));
}

static LogicalResult verifyPrimitive(Operation *op, ImplKind kind) {
  bool outputBased = kind == ImplKind::Pack || kind == ImplKind::Downcast;
  TypeRange anchor =
      outputBased ? TypeRange(op->getResultTypes()) : TypeRange(op->getOperandTypes());
  if (anchor.size() != 1 || !isa<OwnType>(anchor.front()))
    return op->emitOpError("requires one owned representation value");
  auto own = cast<OwnType>(anchor.front());
  auto layout = resolveRepresentation(op, own);
  if (!layout)
    return failure();
  auto ctor = dyn_cast_or_null<qtt::CtorType>(own.getConstructor());
  SmallVector<Type> inputs, results;
  if (kind == ImplKind::Borrow) {
    inputs.push_back(own);
    results.push_back(ViewType::get(op->getContext(), own.getLayout(), own.getConstructor()));
  } else {
    if (!ctor)
      return op->emitOpError("requires a constructor refinement");
    Type adt = OwnType::get(op->getContext(), own.getLayout(), Type{});
    switch (kind) {
    case ImplKind::Clone:
    case ImplKind::Drop:
      llvm_unreachable("ownership implementations do not have QREP primitive ops");
    case ImplKind::Pack:
      for (Type field : ctor.getPayload())
        inputs.push_back(representation(layout, field, true));
      results.push_back(own);
      break;
    case ImplKind::Unpack:
      inputs.push_back(own);
      for (Type field : ctor.getPayload())
        results.push_back(representation(layout, field, false));
      break;
    case ImplKind::Upcast:
      inputs.push_back(own);
      results.push_back(adt);
      break;
    case ImplKind::Downcast:
      inputs.push_back(adt);
      results.push_back(own);
      break;
    case ImplKind::Borrow:
      llvm_unreachable("handled above");
    }
  }
  if (llvm::is_contained(inputs, Type{}) || llvm::is_contained(results, Type{}) ||
      op->getOperandTypes() != TypeRange(inputs) || op->getResultTypes() != TypeRange(results))
    return op->emitOpError("operands/results do not match representation signature");
  return success();
}

LogicalResult PackOp::verify() { return verifyPrimitive(*this, ImplKind::Pack); }
LogicalResult UnpackOp::verify() { return verifyPrimitive(*this, ImplKind::Unpack); }
LogicalResult UpcastOp::verify() { return verifyPrimitive(*this, ImplKind::Upcast); }
LogicalResult DowncastOp::verify() { return verifyPrimitive(*this, ImplKind::Downcast); }
LogicalResult BorrowOp::verify() { return verifyPrimitive(*this, ImplKind::Borrow); }

LogicalResult CaseOp::verify() {
  auto own = dyn_cast<OwnType>(getInput().getType());
  if (!own || own.getConstructor())
    return emitOpError("requires an owned ADT value");
  auto layout = resolveRepresentation(*this, own);
  if (!layout)
    return failure();
  llvm::DenseSet<Type> seen;
  for (Region &region : getCases()) {
    if (!llvm::hasSingleElement(region) || region.front().getNumArguments() != 1)
      return emitOpError("each case requires one block with one constructor argument");
    auto type = dyn_cast<OwnType>(region.front().getArgument(0).getType());
    if (!type || type.getLayout() != own.getLayout() || !type.getConstructor() ||
        !layout.lookupConstructor(dyn_cast<qtt::CtorType>(type.getConstructor())) ||
        !seen.insert(type.getConstructor()).second)
      return emitOpError("case arguments must cover unique declared constructors");
    if (region.front().empty() || !isa<CaseYieldOp>(region.front().back()))
      return emitOpError("each case must end with qrep.case_yield");
  }
  if (seen.size() != layout.getConstructors().size())
    return emitOpError("cases must cover every constructor");
  return success();
}

LogicalResult CaseYieldOp::verify() {
  if (getOperandTypes() != cast<CaseOp>((*this)->getParentOp()).getResultTypes())
    return emitOpError("yield types must match case results");
  return success();
}
