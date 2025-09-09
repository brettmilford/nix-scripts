{
  description = "Python scripts";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop-py:\\w]$\\[\\033[0m\\] ";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
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

          immich-season-organiser = pkgs.writers.writePython3 "immich-season-organiser" {
            libraries = with pkgs.python3Packages; [ requests ];
            flakeIgnore = [ "E501 W503" ];
          } (builtins.readFile ./immich-season-organiser.py);

          immich-download-trashed = pkgs.writers.writePython3 "immich-download-trashed" {
            libraries = with pkgs.python3Packages; [ requests ];
            flakeIgnore = [ "E501" ];
          } (builtins.readFile ./immich-download-trashed.py);
        in
        {
          apps = {
            immich-season-organiser = {
              type = "app";
              program = "${immich-season-organiser}";
            };
            immich-download-trashed = {
              type = "app";
              program = "${immich-download-trashed}";
            };
          };

          devShells = {
            python = pkgs.mkShell {
              venvDir = "./.venv";
              buildInputs = with pkgs; [
                python312Packages.python
                python312Packages.venvShellHook
                python312Packages.flake8
                python312Packages.black
                python312Packages.pytest
              ];
            };
          };
        };
    in
    {
      apps = mapSystems (system: (mkSystemOutputs system).apps);
      devShells = mapSystems (system: (mkSystemOutputs system).devShells);
    };
}
