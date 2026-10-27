{
  description = "Thirdeye AESOP engine and Qt launcher";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    cli11 = { url = "github:CLIUtils/CLI11/v2.6.2"; flake = false; };
    wildmidi = { url = "github:Mindwerks/wildmidi/0a888289ea4d1d1934559dbce38695fcef48eb75"; flake = false; };
    googletest = { url = "github:google/googletest/v1.15.2"; flake = false; };
  };

  outputs = inputs@{ self, nixpkgs, ... }:
    let
      forAllSystems = nixpkgs.lib.genAttrs [ "x86_64-linux" "aarch64-linux" ];
    in {
      packages = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in rec {
          thirdeye = pkgs.stdenv.mkDerivation {
            pname = "thirdeye";
            version = "0.90.0";
            src = self;
            nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
            buildInputs = with pkgs; [ sdl3 openal libuuid qt6.qtbase ];
            postPatch = ''
              substituteInPlace CMakeLists.txt \
                --replace-fail 'INSTALL(DIRECTORY "''${Thirdeye_BINARY_DIR}/resources" DESTINATION "''${DATADIR}" )' "" \
                --replace-fail 'INSTALL(FILES "''${Thirdeye_BINARY_DIR}/launcher.qss" DESTINATION "''${DATADIR}/resources" )' ""
            '';
            cmakeFlags = [
              "-DCMAKE_BUILD_TYPE=Release"
              "-DDPKG_PROGRAM=OFF"
              "-DBINDIR=${placeholder "out"}/bin"
              "-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON"
              "-DCMAKE_INSTALL_RPATH=${placeholder "out"}/lib"
              "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"
              "-DFETCHCONTENT_SOURCE_DIR_CLI11=${inputs.cli11}"
              "-DFETCHCONTENT_SOURCE_DIR_WILDMIDI=${inputs.wildmidi}"
              "-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=${inputs.googletest}"
            ];
            doCheck = true;
            preCheck = ''
              export LD_LIBRARY_PATH="$PWD/_deps/wildmidi-build/src''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
            '';
            postInstall = ''
              install -Dm644 ../files/bytecode.def "$out/bin/bytecode.def"
            '';
            meta = {
              description = "Engine recreation for Eye of the Beholder III and Dungeon Hack";
              homepage = "https://github.com/psi29a/thirdeye";
              license = pkgs.lib.licenses.gpl3Only;
              platforms = pkgs.lib.platforms.linux;
              mainProgram = "thirdeye-launcher";
            };
          };
          default = thirdeye;
        });
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.thirdeye ];
            packages = with pkgs; [ git gdb ];
            cmakeFlags = builtins.filter
              (flag: pkgs.lib.hasPrefix "-DFETCHCONTENT" flag)
              self.packages.${system}.thirdeye.cmakeFlags;
          };
        });
    };
}
