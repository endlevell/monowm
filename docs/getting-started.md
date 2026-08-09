# Getting Started

This guide covers building mono, installing a configuration, and starting the
compositor for the first time.

## Requirements

mono currently targets Linux and wlroots 0.20. Required packages:

- a C11 compiler and `make`
- `pkg-config`
- `wayland` and `wayland-protocols`
- `wlroots-0.20` with its libinput backend
- `libinput`
- `xkbcommon`
- `lua5.4`
- `xcb` and `xcb-icccm` when XWayland support is enabled

Distribution package names vary. Development headers may be separate packages
ending in `-dev` or `-devel`.

## Build

From the repository root:

```sh
make
```

Build output stays in two directories:

- `build/mono`: compositor
- `tools/monomsg/build/monomsg`: IPC command-line client

Remove generated files and build output with:

```sh
make clean
```

### Nix

The repository provides a development shell containing build dependencies:

```sh
nix develop
make
```

Build directly through the flake with:

```sh
nix build
```

## Install

The default prefix is `/usr/local`:

```sh
sudo make install
```

Override it when needed:

```sh
sudo make PREFIX=/usr make install
```

Installation adds `mono` and `monomsg` to `PREFIX/bin`. Example configuration
files are installed under `/etc/mono`.

## Create User Configuration

mono requires a readable Lua configuration. Install the provided starter:

```sh
mkdir -p ~/.config/mono
cp examples/config.lua ~/.config/mono/config.lua
```

When `XDG_CONFIG_HOME` is set, mono reads
`$XDG_CONFIG_HOME/mono/config.lua`. Otherwise it reads
`~/.config/mono/config.lua`.

Choose a terminal and launcher that exist on your system before starting. The
starter uses `foot`, `wofi`, `waybar`, and systemd environment import commands.
Missing autostart programs print errors but do not prevent mono from running.

## Start mono

From a TTY or display manager session:

```sh
mono
```

When running the uninstalled build:

```sh
./build/mono
```

`XDG_RUNTIME_DIR` must exist. A normal login session usually provides it.

Available command-line options:

```text
mono [-v] [-d] [-s startup-command] [-c config-file]
```

- `-c path`: load another configuration file
- `-d`: enable wlroots debug logging
- `-s command`: run a shell command after Wayland starts
- `-v`: print version and exit

Example using a temporary configuration:

```sh
./build/mono -c ./examples/config.lua
```

## First Controls

Default bindings come from your Lua file, not compiled code. Starter bindings
include:

- `logo+Return`: open terminal
- `logo+Space`: open launcher
- `logo+Q`: close focused window
- `logo+1` through `logo+9`: view tag
- `logo+H/J/K/L`: focus by direction
- `logo+C`: toggle canvas mode
- middle-drag: pan canvas
- `logo+mouse-wheel`: zoom canvas
- `logo+Shift+E`: exit mono

Read [Configuration](configuration.md) to customize these controls.

## Next Guides

- [Configuration](configuration.md)
- [Actions](actions.md)
- [Canvas Mode](canvas.md)
- [monomsg IPC](monomsg.md)
- [Troubleshooting](troubleshooting.md)
