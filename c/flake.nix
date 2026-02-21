{
  description = "C apps";
  nixConfig.bash-prompt = "\\n\\[\\033[1;32m\\][nix-develop-c:\\w]$\\[\\033[0m\\] ";

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

          sm-proc = pkgs.stdenv.mkDerivation {
            pname = "sm-proc";
            version = "1.0.0";

            src = ./sm-proc;

            nativeBuildInputs = with pkgs; [
              cmake
              pkg-config
            ];

            buildInputs = with pkgs; [
              curl
              libconfig
              pcre2
              libxlsxwriter
              cjson
              poppler
            ];

            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DBUILD_TESTS=ON"
            ];

            installPhase = ''
              mkdir -p $out/bin
              mkdir -p $out/share/sm-proc/examples
              
              # Install executable
              cp sm-proc $out/bin/sm-proc
              
              # Install example config
              cp ../examples/sm-proc.cfg $out/share/sm-proc/examples/ || true
            '';

            doCheck = true;

            meta = with pkgs.lib; {
              description = "Statement processor";
              homepage = "https://github.com/user/nix-scripts";
              license = licenses.mit;
              maintainers = [ ];
              platforms = platforms.unix;
            };
          };
        in
        {
          packages = {
            inherit sm-proc;
          };
          
          apps = {
            sm-proc = {
              type = "app";
              program = "${sm-proc}/bin/sm-proc";
            };
          };

          devShells = {
            c = pkgs.mkShell {
              buildInputs = with pkgs; [
                cmake
                pkg-config
                curl
                libconfig
                pcre2
                libxlsxwriter
                cjson
              ];
            };
          };
        };
    in
    {
      packages = mapSystems (system: (mkSystemOutputs system).packages);
      apps = mapSystems (system: (mkSystemOutputs system).apps);
      devShells = mapSystems (system: (mkSystemOutputs system).devShells);
    };
}
