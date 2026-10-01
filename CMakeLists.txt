cmake_minimum_required(VERSION 3.20)

project(qtt-mlir
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(QTTMLIR_BUILD_TESTS "Build QTTMLIR tests" ON)
option(QTTMLIR_BUILD_TOOLS "Build QTTMLIR tools" ON)

find_package(MLIR REQUIRED CONFIG)

message(STATUS "Using MLIRConfig.cmake: ${MLIR_DIR}")
message(STATUS "Using LLVMConfig.cmake: ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH
 "${MLIR_CMAKE_DIR}"
 "${LLVM_CMAKE_DIR}")

include(TableGen)
include(AddLLVM)
include(AddMLIR)
include(HandleLLVMOptions)

#
# Include Path
#

include_directories(
  ${LLVM_INCLUDE_DIRS}
  ${MLIR_INCLUDE_DIRS}
)

include_directories(SYSTEM
 ${PROJECT_SOURCE_DIR}/include
 ${PROJECT_BINARY_DIR}/include)


include(cmake/Dependencies.cmake)

add_subdirectory(include)
add_subdirectory(lib)

if(QTTMLIR_BUILD_TOOLS)
  add_subdirectory(tools)
endif(QTTMLIR_BUILD_TOOLS)

if(QTTMLIR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
  add_subdirectory(unittests)
endif(QTTMLIR_BUILD_TESTS)
