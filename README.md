# mono

**Tiled by default, infinite by choice.**

A lightweight Wayland compositor with dwindle tiling and infinite canvas
workspaces.

mono gives each tag two ways to work. Keep normal dwindle tiling when you want
automatic structure, or switch that tag into canvas mode when you want windows
on an open, pannable, zoomable surface. Canvas state is independent per tag, so
tiled and spatial workspaces can coexist on the same monitor.

## Features

- Cursor-aware dwindle tiling and directional focus
- Drag-to-rearrange tiles with edge placement preview
- Per-tag infinite canvas mode with pan and zoom
- Floating, fullscreen, gaps, smart gaps, and double borders
- Lua 5.4 configuration with callback-driven key bindings
- Live config reload with previous config retained after Lua errors
- Window rules, monitor rules, input settings, and ordered autostart
- `monomsg` IPC client for scripts and status bars
- `ext-workspace-v1` integration
- Layer shell, session lock, idle inhibition, screencopy, and output management
- XWayland support

## Quick Start

Required libraries include wlroots 0.20, Wayland, libinput, xkbcommon, Lua 5.4,
and XCB when XWayland is enabled. Full package details live in
[Getting Started](docs/getting-started.md).

### Nix

```sh
nix develop
make
```

### Other Systems

Install development dependencies, then run:

```sh
make
sudo make install
```

Build output:

```text
build/mono
tools/monomsg/build/monomsg
```

Install starter configuration:

```sh
mkdir -p ~/.config/mono
cp examples/config.lua ~/.config/mono/config.lua
```

Review program names in config, then start mono from TTY or display manager:

```sh
mono
```

Use local build without installing:

```sh
./build/mono -c ./examples/config.lua
```

## Lua Configuration

Key bindings use callbacks:

```lua
mono.bind("logo+return", function()
	mono.action("spawn", "foot")
end)
```

Configuration supports normal Lua code, loops, helper functions, and modules.
Saving valid config replaces runtime callbacks and settings. Input devices,
monitor setup, existing decorations, and autostart still require restart after
relevant changes.

See [Configuration](docs/configuration.md) and the commented
[`examples/config.lua`](examples/config.lua).

## Canvas Mode

Toggle current tag between dwindle and canvas:

```lua
mono.bind("logo+c", function()
	mono.action("togglecanvas")
end)

buttons = {
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

While canvas is active:

- middle-drag pans viewport
- `logo+scroll` zooms
- windows can move beyond monitor boundaries
- disabling canvas returns affected windows to dwindle layout

Current zoom resizes client buffers rather than scaling rendered textures. See
[Canvas Mode](docs/canvas.md) for behavior and limitations.

## Documentation

- [Getting Started](docs/getting-started.md): dependencies, build, install,
  first launch
- [Configuration](docs/configuration.md): Lua options, callbacks, modules, hot
  reload
- [Actions](docs/actions.md): supported `mono.action()` names and arguments
- [Canvas Mode](docs/canvas.md): controls, coordinate model, limitations
- [monomsg IPC](docs/monomsg.md): query, watch, and change compositor state
- [Troubleshooting](docs/troubleshooting.md): config, build, runtime, XWayland,
  IPC, canvas, and memory issues

## Command Line

```text
mono [-v] [-d] [-s startup-command] [-c config-file]
```

- `-c path`: use custom config
- `-d`: enable debug logging
- `-s command`: run shell command after Wayland starts
- `-v`: print version

## Project Layout

```text
config/             compiled layout defaults
docs/               user documentation
examples/           starter Lua configuration
include/mono/       compositor headers and protocol integration
protocols/          Wayland protocol XML and generated bindings
src/                compositor source
tools/monomsg/      IPC command-line client
build/              compositor build output
```

## Roadmap

- [ ] True optical canvas zoom
- [ ] Separate trackpad scrolling from mouse-wheel canvas zoom
- [ ] Configurable keyboard layout/keymap
- [ ] System config fallback at `/etc/mono/config.lua`

## Credits

mono began from dwl-derived work and draws from
[dwl](https://codeberg.org/dwl/dwl), [dwm](https://dwm.suckless.org/),
[MangoWM](https://github.com/mangowm/mango),
[vxwm](https://codeberg.org/wh1tepearl/vxwm),
[swindle](https://gitea.hgdump.net/tsukasa/swindle), and
[hevel](https://github.com/AlbertoBSD/hevel). Preserve applicable upstream
attribution and license notices when redistributing.

## License

MIT. See [`LICENSE`](LICENSE).
