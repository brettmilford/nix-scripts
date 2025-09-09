{
  description = "Shell scripts";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop-sh:\\w]\$\\[\\033[0m\\] ";

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
          compare-dirs = pkgs.writeShellApplication {
            name = "compare-dirs";
            text = builtins.readFile ./compare-dirs.sh;
          };
          test-fail2ban = pkgs.writeShellApplication {
            name = "test-fail2ban";
            runtimeInputs = with pkgs; [
              curl
              jq
            ];
            text = builtins.readFile ./test-fail2ban.sh;
          };

        in
        {
          apps = {
            compare-dirs = {
              type = "app";
              program = "${compare-dirs}/bin/compare-dirs";
            };
            test-fail2ban = {
              type = "app";
              program = "${test-fail2ban}/bin/test-fail2ban";
            };
          };

          devShells = {
            shell = pkgs.mkShell {
              buildInputs = with pkgs; [
                shellcheck
                coreutils
                curl
                jq
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
