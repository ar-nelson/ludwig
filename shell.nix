{ pkgs ? import <nixpkgs> {} }:
  pkgs.clangStdenv.mkDerivation {
    name = "clang-nix-shell";
    buildInputs = with pkgs; [
      cmake
      go-task
      zlib
    ];
}
