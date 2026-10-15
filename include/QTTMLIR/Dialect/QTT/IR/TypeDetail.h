#pragma once

#include "QTTMLIR/Dialect/QTT/IR/QTTTypes.h"

#include "mlir/IR/TypeSupport.h"
#include "mlir/Support/LLVM.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SmallVector.h"

#include <tuple>

namespace mlir::qtt::details {
struct ADTTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::tuple<mlir::StringAttr, llvm::ArrayRef<mlir::Type>>;

  ADTTypeStorage(mlir::StringAttr qualifiedName, llvm::ArrayRef<mlir::Type> typeArguments)
      : qualifiedName_(qualifiedName), typeArguments_(typeArguments) {}

  bool operator==(const KeyTy &key) const {
    return qualifiedName_ == std::get<0>(key) && typeArguments_ == std::get<1>(key);
  }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(std::get<0>(key), llvm::hash_combine_range(std::get<1>(key).begin(),
                                                                         std::get<1>(key).end()));
  }

  KeyTy getAsKey() const { return KeyTy(qualifiedName_, typeArguments_); }

  static ADTTypeStorage *construct(mlir::TypeStorageAllocator &allocator, const KeyTy &key) {
    return new (allocator.allocate<ADTTypeStorage>())
        ADTTypeStorage(std::get<0>(key), allocator.copyInto(std::get<1>(key)));
  }

  mlir::LogicalResult mutate(mlir::TypeStorageAllocator &allocator,
                             llvm::ArrayRef<CtorType> newCtors) {
    if (initialized_)
      return success(ctors_ == newCtors);

    ctors_ = allocator.copyInto(llvm::ArrayRef<CtorType>(newCtors));
    initialized_ = true;
    return mlir::success();
  }

  // Data
  mlir::StringAttr qualifiedName_;
  llvm::ArrayRef<mlir::Type> typeArguments_;
  llvm::ArrayRef<CtorType> ctors_;

  bool initialized_ = false;
};

struct CtorTypeStorage : public mlir::TypeStorage {
  using KeyTy = std::tuple<ADTType, mlir::StringAttr, llvm::ArrayRef<mlir::Type>,
                           llvm::ArrayRef<Multiplicity>>;

  CtorTypeStorage(ADTType parent, mlir::StringAttr name, llvm::ArrayRef<mlir::Type> payload,
                  llvm::ArrayRef<Multiplicity> fieldMultiplicities)
      : parent(parent), name(name), payload(payload), fieldMultiplicities(fieldMultiplicities) {}

  KeyTy getAsKey() const { return KeyTy(parent, name, payload, fieldMultiplicities); }

  bool operator==(const KeyTy &key) const { return getAsKey() == key; }

  static llvm::hash_code hashKey(const KeyTy &key) {
    auto hash = llvm::hash_combine(
        std::get<0>(key), std::get<1>(key),
        llvm::hash_combine_range(std::get<2>(key).begin(), std::get<2>(key).end()));
    for (Multiplicity value : std::get<3>(key))
      hash = llvm::hash_combine(hash, static_cast<unsigned>(value));
    return hash;
  }

  static CtorTypeStorage *construct(mlir::TypeStorageAllocator &allocator, const KeyTy &key) {
    return new (allocator.allocate<CtorTypeStorage>())
        CtorTypeStorage(std::get<0>(key), std::get<1>(key), allocator.copyInto(std::get<2>(key)),
                        allocator.copyInto(std::get<3>(key)));
  }

  ADTType parent;
  mlir::StringAttr name;
  llvm::ArrayRef<mlir::Type> payload;
  llvm::ArrayRef<Multiplicity> fieldMultiplicities;
};
} // namespace mlir::qtt::details
