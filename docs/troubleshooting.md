# Troubleshooting

## mono Cannot Find Configuration

Typical error:

```text
failed to load config from '.../mono/config.lua'
```

Install starter config:

```sh
mkdir -p ~/.config/mono
cp examples/config.lua ~/.config/mono/config.lua
```

If `XDG_CONFIG_HOME` is set, place file there instead:

```text
$XDG_CONFIG_HOME/mono/config.lua
```

Test another file explicitly:

```sh
mono -c /absolute/path/config.lua
```

## Lua Error on Startup or Reload

mono reports context and Lua error on standard error:

```text
mono config [load]: ...
```

Common causes:

- missing comma inside table
- unmatched `end`, `}`, or `)`
- unknown module in `require()`
- incorrect module search path
- calling variable before defining it

On hot-reload failure, mono keeps previous working config. Fix file and save it
again.

For modules, confirm `package.path` contains `~/.config/mono/?.lua`. Module
`bindings.keys` maps to `~/.config/mono/bindings/keys.lua`.

## Unknown Key or Modifier

Errors resemble:

```text
mono config: unknown key '...', skipping bind
mono config: unknown modifier '...', ignoring
```

Use XKB keysym names such as `Return`, `space`, `XF86AudioMute`, `bracketleft`.
Names are case-insensitive.

Supported modifier names:

```text
shift ctrl alt logo mod2 mod3 mod5
```

## Binding Does Nothing

Check these points:

- callback uses supported action from [Actions](actions.md)
- action arguments are strings
- modifier combination matches active keyboard state
- program used by `spawn` exists in `PATH`
- binding was not replaced by another identical binding earlier in config

Run mono with debug logging:

```sh
mono -d
```

## Input or Monitor Change Did Not Reload

Hot reload updates config and callbacks, but existing hardware objects retain
some setup-time values.

Restart mono after changing:

- input options
- monitor scale, transform, position, or initial layout
- existing window border width/color
- root background color
- autostart commands

## XDG_RUNTIME_DIR Must Be Set

Wayland needs per-user runtime directory. Normal login through PAM/systemd sets
it automatically.

Do not run mono through `sudo`. Start it as logged-in user from TTY or display
manager session.

Inspect value:

```sh
printf '%s\n' "$XDG_RUNTIME_DIR"
```

Expected form often resembles `/run/user/1000`.

## Build Fails at pkg-config

Errors mentioning missing `pkg-config`, package `.pc` files, or
`wayland-scanner` indicate missing development dependencies.

Nix users:

```sh
nix develop
make
```

Other distributions need packages listed in
[Getting Started](getting-started.md). Confirm wlroots package exposes
`wlroots-0.20` to `pkg-config`.

## Generated Protocol Error

`make clean` removes generated Wayland protocol sources. Next `make` regenerates
them using `wayland-scanner`.

If scanner cannot find XML files, install `wayland-protocols` development
package and verify `pkg-config --variable=pkgdatadir wayland-protocols` works.

## XWayland Applications Do Not Start

XWayland support is enabled by default in `config.mk`:

```make
XWAYLAND = -DXWAYLAND
XLIBS = xcb xcb-icccm
```

Build needs XCB headers and wlroots built with XWayland support. mono continues
without XWayland if server initialization fails; check standard error.

To build without XWayland, comment those two assignments and uncomment empty
ones in `config.mk`.

## Canvas Window Is Missing

Canvas windows may exist outside current viewport.

- pan in opposite direction using configured canvas drag
- zoom out with `logo+scroll`
- disable canvas to return affected windows to dwindle layout

Canvas pan/zoom currently belong to monitor, shared by its canvas tags.

## monomsg Says bad display

Run `monomsg` inside mono session so `WAYLAND_DISPLAY` points to mono socket.

Check:

```sh
printf '%s\n' "$WAYLAND_DISPLAY"
monomsg -P
```

If command runs under systemd service, import Wayland environment after
compositor starts.

## Memory Appears Very High

Process monitors may show virtual memory (`VIRT` or `VSZ`) rather than physical
RAM. GPU drivers reserve large virtual mappings, especially NVIDIA.

Useful metrics:

- RSS: currently resident physical pages
- PSS: shared pages divided among processes
- private dirty: memory owned and modified by process
- VIRT/VSZ: address space reservation, not actual RAM use

Inspect mappings:

```sh
pmap -x "$(pgrep -x mono)"
```

Use total RSS/PSS, not virtual total, when evaluating compositor memory use.

## Report a Bug

Include:

- mono version or commit
- wlroots version
- GPU and driver
- relevant config section
- exact reproduction steps
- shortest decisive error line
- whether issue also occurs with `examples/config.lua`
