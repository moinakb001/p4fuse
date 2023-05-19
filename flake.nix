{
  inputs = {
    # GitHub example, also supports GitLab:
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };
  outputs = {self, nixpkgs}: {
    devShell.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.mkShell {
      buildInputs = [
        nixpkgs.legacyPackages.x86_64-linux.fuse3 
        nixpkgs.legacyPackages.x86_64-linux.clang
        nixpkgs.legacyPackages.x86_64-linux.pkg-config
        nixpkgs.legacyPackages.x86_64-linux.liburing
        nixpkgs.legacyPackages.x86_64-linux.openssl
        nixpkgs.legacyPackages.x86_64-linux.sqlite
        nixpkgs.legacyPackages.x86_64-linux.p4
      ];
    };
  };
}

