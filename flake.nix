{
  description = "Scripts";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop:\\w]\$\\[\\033[0m\\] ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    shFlake.url = "path:./sh";
    shFlake.inputs.nixpkgs.follows = "nixpkgs";
    pyFlake.url = "path:./py";
    pyFlake.inputs.nixpkgs.follows = "nixpkgs";
  };

  outputs =
    {
      self,
      nixpkgs,
      shFlake,
      pyFlake,
    }:
    let
      systems = [
        "aarch64-linux"
        "aarch64-darwin"
        "x86_64-linux"
        "x86_64-darwin"
      ];
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
          pkgs = nixpkgs.legacyPackages.${system};
          help-script = pkgs.writeShellScriptBin "help" ''
            echo "  nix run .#<tool-name> [args...]"
          '';
        in
        {
          # Executed by `nix fmt`
          formatter = pkgs.nixfmt-rfc-style;

          # Executed by `nix build .#<name>` (. for default)
          packages = {
            default = help-script;
          };

          # Executed by `nix run .#<name>` (. for default)
          apps = {
            default = {
              type = "app";
              program = "${help-script}/bin/help";
            };
          }
          // shFlake.apps.${system}
          // pyFlake.apps.${system};

          # Executed by `nix develop .#<name>` (. for default)
          devShells =
            let
              nixShell = pkgs.mkShell {
                buildInputs = with pkgs; [
                  nixfmt-rfc-style
                ];
              };
            in
            {
              default = nixShell;
              nix = nixShell;
            }
            // shFlake.devShells.${system}
            // pyFlake.devShells.${system};
        };
    in
    {
      packages = mapSystems (system: (mkSystemOutputs system).packages);
      apps = mapSystems (system: (mkSystemOutputs system).apps);
      devShells = mapSystems (system: (mkSystemOutputs system).devShells);
      formatter = mapSystems (system: (mkSystemOutputs system).formatter);
      # Used by `nix flake init -t <flake>`
      templates = {
        default = {
          path = ./templates/default;
          description = "Base flake";
        };
      };
    };
}
