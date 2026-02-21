{
  description = "Go scripts";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop-go:\\w]$\\[\\033[0m\\] ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    gomod2nix.url = "github:tweag/gomod2nix";
    gomod2nix.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      gomod2nix,
    }:
    let
      systems = [
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-linux"
        "x86_64-darwin"
      ];
      overlays = [ gomod2nix.overlays.default ];
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
            inherit system;
            overlays = [ gomod2nix.overlays.default ];
          };

          go-test = pkgs.stdenvNoCC.mkDerivation {
            name = "go-test";
            dontBuild = true;
            src = ./.;
            doCheck = true;
            nativeBuildInputs = with pkgs; [
              go
              writableTmpDirAsHomeHook
            ];
            checkPhase = ''
              go test -v ./...
            '';
            installPhase = ''
              mkdir "$out"
            '';
          };

          go-lint = pkgs.stdenvNoCC.mkDerivation {
            name = "go-lint";
            dontBuild = true;
            src = ./.;
            doCheck = true;
            nativeBuildInputs = with pkgs; [
              golangci-lint
              go
              writableTmpDirAsHomeHook
            ];
            checkPhase = ''
              golangci-lint run
            '';
            installPhase = ''
              mkdir "$out"
            '';
          };

          statement-extractor = pkgs.buildGoApplication {
            pname = "statement-extractor";
            version = "0.1.0";
            src = ./statement-extractor;
            # NOTE: Need to run gomod2nix next to go.mod to generate this.
            modules = ./statement-extractor/gomod2nix.toml;
          };
        in
        {
          packages = {
            inherit statement-extractor;
          };

          apps = {
            statement-extractor = {
              type = "app";
              program = "${statement-extractor}/bin/statement-extractor";
            };
          };

          devShells = {
            go = pkgs.mkShell {
              buildInputs = with pkgs; [
                go
                gopls
                gotools
                go-tools
                gomod2nix.packages.${system}.default
              ];
            };
          };

          checks = {
            inherit go-test go-lint;
          };
        };
    in
    {
      packages = mapSystems (system: (mkSystemOutputs system).packages);
      apps = mapSystems (system: (mkSystemOutputs system).apps);
      devShells = mapSystems (system: (mkSystemOutputs system).devShells);
    };
}
