#pragma once

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPAttrs.h"
#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"

#define GET_OP_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h.inc"

namespace mlir::qrep {
/// Look up a primitive body, optionally specialized for a constructor.
ImplOp lookupImpl(LayoutOp layout, ImplKind kind, qtt::CtorType ctor = {});
/// Look up the layout's complete ADT eliminator.
DiscriminateOp lookupDiscriminate(LayoutOp layout);
} // namespace mlir::qrep
