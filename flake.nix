{
  description = "Idris2 QTT MLIR Dialect development environment";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = import nixpkgs { inherit system; };
        llvm = pkgs.llvmPackages_23;

        llvmDev = pkgs.lib.getDev llvm.llvm;
        mlirDev = pkgs.lib.getDev llvm.mlir;
      in
      {
        # You can use what compiler you want
        devShells.default = pkgs.mkShellNoCC {
          packages = [
            pkgs.cmake
            pkgs.ninja

            llvm.llvm
            llvm.llvm.dev
            llvm.mlir
            llvm.tblgen
          ];

          shellHook = ''
            export LLVM_DIR="${llvmDev}/lib/cmake/llvm"
            export MLIR_DIR="${mlirDev}/lib/cmake/mlir"

            echo "LLVM_DIR=$LLVM_DIR"
            echo "MLIR_DIR=$MLIR_DIR"
          '';
        };
      }
    );
}
