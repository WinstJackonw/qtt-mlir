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
  using KeyTy = std::tuple<ADTType, mlir::StringAttr, llvm::ArrayRef<mlir::Type>>;

  CtorTypeStorage(ADTType parent, mlir::StringAttr name, llvm::ArrayRef<mlir::Type> payload)
      : parent(std::move(parent)), name(std::move(name)), payload(std::move(payload)) {}

  KeyTy getAsKey() const { return KeyTy(parent, name, payload); }

  bool operator==(const KeyTy &key) const {
    return parent == std::get<0>(key) && name == std::get<1>(key) && payload == std::get<2>(key);
  }

  static llvm::hash_code hashKey(const KeyTy &key) {
    return llvm::hash_combine(
        std::get<0>(key), std::get<1>(key),
        llvm::hash_combine_range(std::get<2>(key).begin(), std::get<2>(key).end()));
  }

  static CtorTypeStorage *construct(mlir::TypeStorageAllocator &allocator, KeyTy &&key) {
    ADTType parent = std::move(std::get<0>(key));
    mlir::StringAttr name = std::move(std::get<1>(key));
    llvm::ArrayRef<mlir::Type> payload = std::move(std::get<2>(key));
    return new (allocator.allocate<CtorTypeStorage>())
        CtorTypeStorage(std::move(parent), std::move(name), allocator.copyInto(payload));
  }

  // Data
  ADTType parent;
  mlir::StringAttr name;
  llvm::ArrayRef<mlir::Type> payload;
};
} // namespace mlir::qtt::details
