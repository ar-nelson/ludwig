{ pkgs ? import <nixpkgs> {} }:
  pkgs.llvmPackages_16.stdenv.mkDerivation {
    name = "clang-nix-shell";
    buildInputs = with pkgs; [
      cmake
      go-task
      nasm
      deno
    ];
}
