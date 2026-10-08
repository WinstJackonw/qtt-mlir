#pragma once

#include "mlir/Bytecode/BytecodeOpInterface.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"

#include "QTTMLIR/Dialect/QREP/IR/QREPTypes.h"

#define GET_OP_CLASSES
#include "QTTMLIR/Dialect/QREP/IR/QREPOps.h.inc"
