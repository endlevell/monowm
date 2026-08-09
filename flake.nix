{
  description = "mono: tiled by default, infinite by choice";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
      {
        packages.default = pkgs.stdenv.mkDerivation rec {
          pname = "mono";
          version = "0.1";

          src = self;

          nativeBuildInputs = with pkgs; [
            pkg-config
            wayland-scanner
            wayland-protocols
          ];

          buildInputs = with pkgs; [
            wayland
            wlroots_0_20
            pixman
            libxkbcommon
            libinput
            lua5_4
            libxcb
            libxcb-wm
          ];

          enableParallelBuilding = true;

          postPatch = ''
            substituteInPlace Makefile \
              --replace '$(DESTDIR)/etc/mono' '$(DESTDIR)$(PREFIX)/etc/mono'
          '';

          makeFlags = [ "PREFIX=${placeholder "out"}" ];

          meta = with pkgs.lib; {
            description = "A lightweight Wayland compositor with dwindle tiling and infinite canvas workspaces";
            license = with licenses; [ gpl3Only mit isc ];
            platforms = platforms.linux;
            mainProgram = "mono";
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [ pkg-config gdb ];
        };

        apps.default = flake-utils.lib.mkApp {
          drv = self.packages.${system}.default;
        };
      });
}
