{ pkgs ? import <nixpkgs> {} }:
  pkgs.llvmPackages_15.stdenv.mkDerivation {
    name = "clang-nix-shell";
    buildInputs = with pkgs; [
      cmake
      go-task
      zlib
      xxd
      deno
    ];
}
