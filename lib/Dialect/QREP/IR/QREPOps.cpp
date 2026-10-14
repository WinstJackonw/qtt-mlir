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

static void printImplementationBody(mlir::OpAsmPrinter &printer, mlir::Operation *, mlir::Region &body) {
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
  if (!ctor || ctor.getParent() != layout.getAdt())
    return failure();
  auto adt = cast<qtt::ADTType>(layout.getAdt());
  if (adt.lookupCtor(ctor.getName()) != ctor)
    return failure();
  Type storage = layout.getStorageFor(ctor);
  if (!storage)
    return failure();
  SmallVector<Type> fields;
  for (Type payload : ctor.getPayload()) {
    Type field = layout.getStorageFor(payload);
    if (!field)
      return failure();
    fields.push_back(field);
  }
  switch (kind) {
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
    return type && !isa<qtt::ADTType, qtt::CtorType, NoneType>(type);
  };
  auto isConstructorStorage = [](Type type) {
    return type && !isa<qtt::ADTType, qtt::CtorType>(type);
  };
  if (!isConcrete(getStorage()))
    return emitOpError("requires concrete ADT storage");
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
    if (!ctor || ctor.getParent() != adt || adt.lookupCtor(ctor.getName()) != ctor ||
        !constructors.insert(ctor).second)
      return emitOpError("constructors must be declared CtorTypes and unique");
    if (!storage || !isConstructorStorage(storage.getValue()))
      return emitOpError("requires concrete constructor storage or none");
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
  for (auto ctor : adt.getCtors())
    for (ImplKind kind : {ImplKind::Pack, ImplKind::Upcast, ImplKind::Downcast,
                          ImplKind::Unpack})
      if (!lookupImpl(*this, kind, ctor))
        return emitOpError() << "missing " << stringifyImplKind(kind)
                             << " implementation for " << ctor.getName();
  return success();
}
