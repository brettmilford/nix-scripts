{
  description = "Rust scripts";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop-rs:\\w]\$\\[\\033[0m\\] ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    rust-overlay.url = "github:oxalica/rust-overlay";
  };

  outputs =
    {
      self,
      nixpkgs,
      rust-overlay,
    }:
    let
      systems = [
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-linux"
        "x86_64-darwin"
      ];
      overlays = [(import rust-overlay)];
      mapSystems =
        f:
        builtins.listToAttrs (
          builtins.map (system: {
            name = system;
            value = f system;
          }) systems
        );
      mkSystemOutputs =
        system:
        let
          pkgs = import nixpkgs {
            inherit system overlays;
          };
          rust = pkgs.rust-bin.beta.latest.default.override {
            extensions = [
              "rust-src"
              "rust-analyzer"
            ];
          };
          paperless-to-spreadsheet = pkgs.rustPlatform.buildRustPackage {
            name = "paperless-to-spreadsheet";
            version = "0.1.0";
            src = ./paperless-to-spreadsheet;
            #cargoHash = "sha256-kiYrTei/9rFJOA7eT2/g4pv3j/2wNDMPt7tgWO4lfaw=";
            #cargoHash = "sha256-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX=";
            cargoHash = "sha256-brU4YlZakk4D5UwlUd8goJYModlXVfdEJK7kkkxwMTo=";
          };
        in
        {
          # Executed by `nix build .#<name>` (. for default)
          packages = {
            paperless-to-spreadsheet = paperless-to-spreadsheet;
          };

          # Executed by `nix run .#<name>` (. for default)
          apps = {
            default = {
              type = "app";
              program = "${paperless-to-spreadsheet}/bin/paperless-to-spreadsheet";
            };
          };

          # Executed by `nix develop .#<name>` (. for default)
          devShells =
            let
              nixShell = pkgs.mkShell {
                buildInputs = [rust];
                RUST_BACKTRACE = 1;
              };
            in
            {
              rust = nixShell;
            };
        };
    in
    {
      inherit overlays;
      packages = mapSystems (system: (mkSystemOutputs system).packages);
      apps = mapSystems (system: (mkSystemOutputs system).apps);
      devShells = mapSystems (system: (mkSystemOutputs system).devShells);
    };
}
