{
  description = "Ludwig - lemmy but better";

  inputs = {
    flake-parts.url = "github:hercules-ci/flake-parts";
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-23.11";
  };

  outputs = inputs@{ flake-parts, ... }:
    flake-parts.lib.mkFlake { inherit inputs; } {
      systems = [ "x86_64-linux" "aarch64-linux" "aarch64-darwin" "x86_64-darwin" ];
      perSystem = { config, self', inputs', pkgs, system, ... }: {
        devShells.default = pkgs.llvmPackages_17.stdenv.mkDerivation {
          packages = with pkgs; [
            go-task nasm deno zstd
          ];
          buildInputs = with pkgs; [
            cmake catch2_3 mold glibc.static
          ];
          name = "ludwig";
        };
      };
    };
}
