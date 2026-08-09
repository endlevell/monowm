# Configuration

mono uses Lua 5.4 for runtime configuration. Default location:

```text
~/.config/mono/config.lua
```

Use `mono -c path/to/config.lua` to load another file.

Start with [`examples/config.lua`](../examples/config.lua). It contains a
complete configuration and comments for common settings.

## General Options

```lua
sloppy_focus = true
bypass_surface_visibility = false
log_level = "error"
```

- `sloppy_focus`: focus window when pointer enters it
- `bypass_surface_visibility`: let idle inhibitors work even when their surface
  is not visible
- `log_level`: `"silent"`, `"error"`, `"info"`, or `"debug"`

## Appearance

Colors use `0xRRGGBBAA` notation.

```lua
appearance = {
	inner_border_px = 2,
	outer_border_px = 2,
	gaps = 10,
	smart_gaps = false,
	root_color = 0x222222ff,
	inner_border_color = 0x555555ff,
	outer_border_color = 0x111111ff,
	inner_focus_color = 0xeeeeeeff,
	outer_focus_color = 0x555555ff,
	inner_urgent_color = 0xff5555ff,
	outer_urgent_color = 0x991111ff,
	fullscreen_bg = 0x000000ff,
}
```

`smart_gaps = true` removes gaps when only one tiled window is visible.

## Input

```lua
input = {
	repeat_rate = 40,
	repeat_delay = 250,
	tap_to_click = true,
	tap_and_drag = true,
	drag_lock = false,
	natural_scrolling = false,
	disable_while_typing = true,
	left_handed = false,
	middle_button_emulation = false,
	scroll_method = "2fg",
	click_method = "button_areas",
	accel_profile = "adaptive",
	accel_speed = 0.0,
}
```

Accepted strings:

- `scroll_method`: `"2fg"`, `"edge"`, or `"button"`
- `click_method`: `"button_areas"` or `"clickfinger"`
- `accel_profile`: `"adaptive"` or `"flat"`
- `accel_speed`: number from `-1.0` to `1.0`

Input values apply when a device is created. Restart mono after changing them.

## Monitor Rules

Rules are checked from top to bottom. First matching rule wins. Keep specific
rules above the catch-all rule.

```lua
monitors = {
	{ name = "eDP-1", layout = "dwindle", mfact = 0.50, nmaster = 1,
	  scale = 2.0, transform = 0, x = 0, y = 0 },
	{ name = nil, layout = "dwindle", mfact = 0.55, nmaster = 1,
	  scale = 1.0, transform = 0, x = -1, y = -1 },
}
```

- `name`: output-name substring; `nil` or empty matches all outputs
- `layout`: `"dwindle"` or floating layout symbol `"><>"`
- `mfact`: reserved master-factor value
- `nmaster`: reserved master-count value
- `scale`: output scale
- `transform`: Wayland output transform number
- `x`, `y`: output position; `-1, -1` enables automatic placement

Monitor settings apply when an output is created. Restart mono or reconnect the
output after changing them.

## Window Rules

Rules match application ID and title using substring matching. Every matching
rule applies; tag masks are combined.

```lua
rules = {
	{ app_id = "org.gimp.GIMP", floating = true, monitor = -1 },
	{ title = "Picture-in-Picture", floating = true, monitor = -1 },
	{ app_id = "firefox", tags = 1 << 1, floating = false, monitor = 0 },
}
```

- `app_id`: Wayland app ID or XWayland class substring
- `title`: window-title substring
- `tags`: bitmask; `1 << 0` is tag 1, `1 << 1` is tag 2
- `floating`: start matching window floating
- `monitor`: zero-based monitor index; `-1` keeps selected monitor

## Autostart

```lua
autostart = {
	"waybar",
	"mako",
}
```

Commands execute through `/bin/sh -c` after Wayland socket and backend startup.
They start in order without blocking compositor event loop.

Autostart runs once at compositor startup. Hot reload does not rerun it.

## Mouse Bindings

Mouse bindings remain table-driven:

```lua
buttons = {
	{ mods = { "logo" }, button = "left", action = "moveresize", args = { "move" } },
	{ mods = { "logo" }, button = "right", action = "moveresize", args = { "resize" } },
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

Modifiers: `shift`, `ctrl`, `alt`, `logo`, `mod2`, `mod3`, `mod5`.

Buttons: `left`, `middle`, `right`, or numeric Linux input button code as a
string.

## Callback Key Bindings

Register key callbacks with `mono.bind()`:

```lua
mono.bind("logo+return", function()
	mono.action("spawn", "foot")
end)
```

Binding format is modifiers joined with `+`, followed by an XKB keysym name.
Names are case-insensitive.

Callbacks can contain normal Lua logic:

```lua
local terminal = os.getenv("TERMINAL") or "foot"

mono.bind("logo+return", function()
	mono.action("spawn", terminal)
end)
```

For repeated bindings, define a helper:

```lua
local function bind_action(keys, action, ...)
	local args = { ... }
	mono.bind(keys, function()
		mono.action(action, table.unpack(args))
	end)
end

bind_action("logo+f", "togglefullscreen")
bind_action("logo+h", "focusdir", "left")
```

See [Actions](actions.md) for every action and argument.

## Modular Configuration

Lua modules help organize a large setup:

```text
~/.config/mono/config.lua
~/.config/mono/settings/input.lua
~/.config/mono/settings/appearance.lua
~/.config/mono/bindings/keys.lua
```

Configure Lua search path in `config.lua`:

```lua
local base = os.getenv("XDG_CONFIG_HOME")
		or (os.getenv("HOME") .. "/.config")

package.path = base .. "/mono/?.lua;" .. package.path

require("settings.input")
require("settings.appearance")
require("bindings.keys")
```

File `settings/input.lua` can assign the global table directly:

```lua
input = {
	repeat_rate = 40,
	repeat_delay = 250,
	tap_to_click = true,
}
```

Module names use dots for directories: `require("settings.input")` loads
`settings/input.lua`.

## Hot Reload

mono watches active config file using inotify. Saving valid Lua replaces
runtime config and callbacks. Syntax/runtime errors keep previous config.

Changes effective immediately include key callbacks, mouse bindings, rules for
new windows, gaps, and values read during later actions.

Some existing objects retain setup-time values. Restart mono for input devices,
monitor rules, existing border sizes/colors, root background, and autostart.
