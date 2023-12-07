{ pkgs ? import <nixpkgs> {}, buildInputs ? [], shellHook ? "", ... }@attrs:

let
  llvmPackage = pkgs.llvmPackages_16;
  stdenv = llvmPackage.libcxxStdenv;
  libcxx = llvmPackage.libraries.libcxx;
  libcxxabi = llvmPackage.libraries.libcxxabi;
  clang-python = llvmPackage.clang-unwrapped.python;

  rest = builtins.removeAttrs attrs ["pkgs" "buildInputs" "shellHook"];

  extraBuildInputs = with pkgs; [cmake go-task nasm deno zstd];
  localBuildInputs = [libcxx libcxxabi clang-python llvmPackage.lldb];
  extraShellHook = shellHook;

in stdenv.mkDerivation (rec {
  name = "clang-nix-shell";
  buildInputs = localBuildInputs ++ extraBuildInputs;
  shellHook = ''
    export CPATH=$CPATH:${llvmPackage.libcxx}/include/c++/v1
    export CPLUS_INCLUDE_PATH=$CPATH
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${libcxx}/lib:${libcxxabi}/lib
    export PATH=$PATH:${clang-python}/share/clang
  '' + extraShellHook;
  CPATH = pkgs.lib.makeSearchPathOutput "dev" "include" (localBuildInputs ++ extraBuildInputs);
} // rest)
