#include "QTTMLIR/Dialect/QTT/IR/QTTOps.h"
#include "mlir/IR/Builders.h"
#include "mlir/Interfaces/FunctionImplementation.h"

using namespace mlir;
using namespace mlir::qtt;

namespace {
// Entries form a private naming scope. Calls to functions (and qualified entry
// calls) are resolved in the containing function's enclosing symbol scope.
Operation *resolveCall(Operation *call, SymbolRefAttr ref, bool entry,
                       SymbolTableCollection &tables) {
  auto owner = dyn_cast_or_null<qtt::FuncOp>(SymbolTable::getNearestSymbolTable(call));
  if (owner && entry && ref.getNestedReferences().empty())
    return tables.lookupSymbolIn(owner, ref);
  Operation *scope = owner ? owner->getParentOp() : call;
  return scope ? tables.lookupNearestSymbolFrom(scope, ref) : nullptr;
}

bool overlaps(FunctionType lhs, FunctionType rhs) {
  for (auto [a, b] : llvm::zip(lhs.getInputs(), rhs.getInputs()))
    if (!isSubType(a, b) && !isSubType(b, a))
      return false;
  return true;
}

// Split only positions actually refined by an entry. Wildcards retain the
// whole ADT, so default-only functions also work with opaque ADTs.
bool covers(ArrayRef<Type> inputs, ArrayRef<FunctionType> entries, unsigned pos = 0) {
  if (entries.empty())
    return false;
  if (pos == inputs.size())
    return true;
  if (llvm::any_of(entries, [&](FunctionType entry) {
        return entry.getInputs().drop_front(pos) == inputs.drop_front(pos);
      }))
    return true;
  bool refined =
      llvm::any_of(entries, [&](FunctionType entry) { return entry.getInput(pos) != inputs[pos]; });
  if (!refined)
    return covers(inputs, entries, pos + 1);
  auto adt = dyn_cast<ADTType>(inputs[pos]);
  if (!adt || !adt.isInitialized())
    return false;
  for (CtorType ctor : adt.getCtors()) {
    SmallVector<FunctionType> matching;
    for (FunctionType entry : entries)
      if (entry.getInput(pos) == adt || entry.getInput(pos) == ctor)
        matching.push_back(entry);
    if (!covers(inputs, matching, pos + 1))
      return false;
  }
  return true;
}

LogicalResult verifyCall(Operation *call, FunctionType type) {
  if (call->getNumOperands() != type.getNumInputs() ||
      !llvm::equal(call->getOperandTypes(), type.getInputs()))
    return call->emitOpError("operand types must match callee signature");
  if (!llvm::equal(call->getResultTypes(), type.getResults()))
    return call->emitOpError("result types must match callee signature");
  return success();
}
} // namespace

LogicalResult qtt::FuncOp::verifyRegions() {
  Block &body = getBody().front();
  if (body.getNumArguments())
    return emitOpError("entry container must not have block arguments");
  if (body.empty())
    return emitOpError("requires at least one qtt.func_entry");
  SmallVector<FunctionType> specializations;
  bool hasDefault = false;
  for (Operation &op : body) {
    auto entry = dyn_cast<FuncEntryOp>(op);
    if (!entry)
      return emitOpError("body may only contain qtt.func_entry operations");
    if (entry.getIsDefault()) {
      if (hasDefault)
        return emitOpError("duplicate default entry");
      hasDefault = true;
      continue;
    }
    auto type = entry.getFunctionType();
    for (FunctionType previous : specializations)
      if (overlaps(previous, type))
        return entry.emitOpError("overlapping specialized entries");
    specializations.push_back(type);
  }
  if (!hasDefault && !covers(getFunctionType().getInputs(), specializations))
    return emitOpError(
        "entries are not exhaustive; add missing specializations or a default entry");
  return success();
}

LogicalResult FuncEntryOp::verify() {
  if (getBody().empty())
    return emitOpError("requires an implementation body");
  auto parent = cast<qtt::FuncOp>((*this)->getParentOp());
  FunctionType base = parent.getFunctionType(), type = getFunctionType();
  if (type.getNumInputs() != base.getNumInputs())
    return emitOpError("input count must match parent function");
  if (type.getResults() != base.getResults())
    return emitOpError("result types must match parent function");
  if (getIsDefault()) {
    if (type != base)
      return emitOpError("default entry must have the full parent function signature");
    return success();
  }
  bool refined = false;
  for (auto [input, original] : llvm::zip(type.getInputs(), base.getInputs())) {
    if (input == original)
      continue;
    auto ctor = dyn_cast<CtorType>(input);
    if (!ctor || ctor.getParent() != original || !ctor.getParent().isInitialized() ||
        ctor.getParent().lookupCtor(ctor.getName()) != ctor)
      return emitOpError(
          "entry inputs must equal or refine parent inputs to declared constructors");
    refined = true;
  }
  if (!refined)
    return emitOpError("unspecialized entry must be marked default");
  return success();
}

ParseResult FuncEntryOp::parse(OpAsmParser &parser, OperationState &result) {
  if (succeeded(parser.parseOptionalKeyword("default")))
    result.addAttribute("is_default", parser.getBuilder().getUnitAttr());
  auto builder = [](Builder &builder, ArrayRef<Type> inputs, ArrayRef<Type> results,
                    function_interface_impl::VariadicFlag,
                    std::string &) -> Type { return builder.getFunctionType(inputs, results); };
  return function_interface_impl::parseFunctionOp(
      parser, result, false, getFunctionTypeAttrName(result.name), builder,
      getArgAttrsAttrName(result.name), getResAttrsAttrName(result.name));
}

void FuncEntryOp::print(OpAsmPrinter &printer) {
  if (getIsDefault())
    printer << " default";
  // printFunctionOp elides only standard function attributes. Print the header
  // and region here to keep the default marker out of the attribute dictionary.
  if (auto visibility = getSymVisibility())
    printer << ' ' << *visibility;
  printer << ' ';
  printer.printSymbolName(getSymName());
  function_interface_impl::printFunctionSignature(printer, *this, getArgumentTypes(), false,
                                                  getResultTypes());
  function_interface_impl::printFunctionAttributes(printer, *this, {"is_default", "function_type"});
  printer << ' ';
  printer.printRegion(getBody(), false, true);
}

LogicalResult ReturnOp::verify() {
  auto parent = cast<FuncEntryOp>((*this)->getParentOp());
  if (!llvm::equal(getOperandTypes(), parent.getFunctionType().getResults()))
    return emitOpError("operand types must match entry result types");
  return success();
}

Operation *DispatchCallOp::resolveCallableInTable(SymbolTableCollection *tables) {
  SymbolTableCollection local;
  return resolveCall(*this, getCalleeAttr(), false, tables ? *tables : local);
}
Operation *DispatchCallOp::resolveCallable() { return resolveCallableInTable(nullptr); }
Operation *EntryCallOp::resolveCallableInTable(SymbolTableCollection *tables) {
  SymbolTableCollection local;
  return resolveCall(*this, getCalleeAttr(), true, tables ? *tables : local);
}
Operation *EntryCallOp::resolveCallable() { return resolveCallableInTable(nullptr); }

LogicalResult DispatchCallOp::verifySymbolUses(SymbolTableCollection &tables) {
  auto target = dyn_cast_or_null<qtt::FuncOp>(resolveCallableInTable(&tables));
  if (!target)
    return emitOpError("callee must resolve to qtt.func");
  return verifyCall(*this, target.getFunctionType());
}
LogicalResult EntryCallOp::verifySymbolUses(SymbolTableCollection &tables) {
  auto target = dyn_cast_or_null<FuncEntryOp>(resolveCallableInTable(&tables));
  if (!target)
    return emitOpError("callee must resolve to qtt.func_entry");
  return verifyCall(*this, target.getFunctionType());
}
