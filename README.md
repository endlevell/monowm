# mono

A tiling Wayland compositor built on wlroots, with a per-tag toggleable infinite canvas.

Most tiling WMs give you one thing: a fixed grid you resize windows inside of.
mono still does that — dwindle tiling, gaps, floating windows, the usual — but
any tag can independently switch into **canvas mode**: windows go free-floating
on an unbounded 2D plane, and you pan/zoom around it with the mouse instead of
being confined to your monitor's pixel count. Tag 1 can stay a normal tiled
workspace while tag 2 is a literal open-ended surface for spatial hoarding —
reference images, scratch terminals, whatever doesn't want to live inside a
fixed grid.

## Features

- **Dwindle tiling** with cursor-aware splits — new windows open on whichever
  side of the split your cursor is actually positioned over, not a fixed
  default direction
- **Canvas mode, per tag** — toggle any tag into a mouse-pannable, zoomable
  infinite plane; other tags keep tiling normally
- **Lua configuration** with live hot-reload (edit `config.lua`, save, done —
  no restart)
- **IPC via `monomsg`** — a standalone client for scripting/status-bar
  integration, decoupled from the compositor itself
- Double borders (inner/outer, independently colored) per client state
  (normal / focused / urgent)
- Tag-shift and view-shift bindings for relative tag navigation, not just
  absolute jumps
- XWayland support

## Dependencies

- `libinput`
- `wayland`
- `wlroots-0.20` (built with the libinput backend)
- `xkbcommon`
- `lua5.4`
- `wayland-protocols` (build-time only)
- `pkg-config` (build-time only)

Install these (plus `-devel`/`-dev` packages where your distro splits them),
then:

```sh
make
sudo make install   # optional, installs to /usr/local/bin by default
```

## Configuration

Copy the example config before your first run — mono won't start without one:

```sh
mkdir -p ~/.config/mono
cp examples/config.lua ~/.config/mono/config.lua
```

Edit `~/.config/mono/config.lua` to taste. See the file itself for the full
option reference — appearance, input, monitor rules, autostart, keybinds, and
mouse buttons are all configured there.

### Canvas mode

Toggle canvas mode on the active tag, pan with a mouse drag, zoom with
`logo+scroll`. Bind it however you like in `config.lua`:

```lua
{ mods = { "logo" }, key = "c", action = "togglecanvas" },
```

```lua
buttons = {
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

Windows on a canvas-mode tag are always floating and have no position
boundary — they exist wherever you last placed them in world space, and
panning just changes which slice of that space your monitor is currently
looking at.

## Project layout
```
config/ - compile-time defaults (config.h)
examples/ - example user config.lua
include/mono/ - public headers
protocols/ - wayland protocol XML + generated bindings
src/ compositor - source
tools/monomsg/ - standalone IPC client
```

## Roadmap

- [ ] True optical zoom (scale rendered buffers instead of resizing clients)
- [ ] Separate trackpad scroll from mouse wheel scroll
- [ ] Configurable keymap
- [ ] Custom config path via CLI flag, with `/etc/mono/config.lua` fallback

## Credits

mono began as a fork of a dwl-based project which itself draws from
[dwl](https://codeberg.org/dwl/dwl), [dwm](https://dwm.suckless.org/), and
borrows its ext-workspace protocol implementation from
[MangoWM](https://github.com/mangowm/mango) — see `licenses/` for the full
attribution chain. The canvas concept draws inspiration from
[vxwm](https://codeberg.org/wh1tepearl/vxwm), and also Tsukasa's
[swindle](https://gitea.hgdump.net/tsukasa/swindle) for the lua parser and, further upstream,
[hevel](https://github.com/AlbertoBSD/hevel).

## License

MIT
