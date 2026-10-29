# Configuration

mono uses Lua 5.4. Default path:

```text
~/.config/mono/config.lua
```

Use `mono -c path/to/config.lua` for another file. Start from
[`examples/config.lua`](../examples/config.lua).

## Configuration Style

Use Lua as Lua: variables, functions, loops, modules, conditionals. `mono.config`
sets base options; builders append ordered rules, monitor rules, buttons, and
startup commands. Existing global-table configuration remains supported.

```lua
local terminal = os.getenv("TERMINAL") or "foot"
local tag = function(n) return 1 << (n - 1) end

mono.config {
	sloppy_focus = true,
	bypass_surface_visibility = false,
	log_level = "error",
	appearance = {
		gaps = 10,
		inner_border_px = 2,
		outer_border_px = 2,
		root_color = 0x222222ff,
	},
	input = {
		repeat_rate = 40,
		repeat_delay = 250,
		tap_to_click = true,
		scroll_method = "2fg",
		click_method = "button_areas",
		accel_profile = "adaptive",
	},
}

mono.monitor { name = "eDP-1", layout = "dwindle", scale = 2, x = 0, y = 0 }
mono.monitor { name = nil, layout = "dwindle", scale = 1, x = -1, y = -1 }
mono.rule { app_id = "org.gimp.GIMP", floating = true, monitor = -1 }
mono.autostart("waybar")
mono.bind("logo+1", mono.view(tag(1)))
mono.bind("logo+return", mono.spawn(terminal))
```

`mono.config` accepts `sloppy_focus`, `bypass_surface_visibility`, `log_level`,
`appearance`, `animations`, `input`, `rules`, `monitors`, `keybinds`, `buttons`, and
`autostart`. Omitted fields keep their existing Lua global value/default.

## Base Options

- `sloppy_focus`: focus window on pointer entry.
- `bypass_surface_visibility`: count hidden-surface idle inhibitors.
- `log_level`: `"silent"`, `"error"`, `"info"`, `"debug"`.
- Colors: `0xRRGGBBAA`, or a `"#RRGGBB"` / `"#RRGGBBAA"` string.
- `appearance.smart_gaps = true`: removes gaps with one tiled window.
- Input strings: `scroll_method` is `"2fg"`, `"edge"`, `"button"`; `click_method`
  is `"button_areas"` or `"clickfinger"`; `accel_profile` is `"adaptive"` or `"flat"`.

Input values apply when a device appears and again on every config reload.

## Animations

```lua
animations = {
	enabled = true, -- false snaps everything into place
	speed = 1.0,    -- 2 = twice as fast, 0.5 = half speed
}
```

Timing follows the end-4 Hyprland setup, with cubic bezier curves:

| Animation | Duration | Curve |
|---|---|---|
| Window open: pop in from 80% and fade in | 300 ms | emphasized decel (0.05, 0.7, 0.1, 1) |
| Window close: shrink to 90% and fade out | 200 ms | emphasized decel |
| Window move: layout changes, fullscreen, drops | 300 ms | emphasized decel |
| Workspace switch: slide sideways | 700 ms | menu decel (0.1, 1, 0, 1) |
| Canvas zoom toward each scroll step | 300 ms | standard decel (0, 0, 0, 1) |

A quick canvas pan keeps gliding after release. Interactive moves and resizes
follow the pointer without delay. Both settings reload live.

## Ordered Builders

```lua
mono.monitor { name = "HDMI-A-1", layout = "><>", scale = 1, x = -1, y = -1 }
mono.rule { app_id = "firefox", tags = 1 << 1, floating = false, monitor = 0 }
mono.button { mods = { "logo" }, button = "left", action = "moveresize", args = { "move" } }
mono.autostart("mako")
```

- Monitor rules: first matching output-name substring wins; `name=nil` matches all.
  `x=-1,y=-1` enables automatic placement.
- Window rules: all matching `app_id`/`title` substring rules apply; tag masks combine.
  `1 << 0` is tag 1. `monitor` is zero-based; `-1` keeps selected monitor.
- Button modifiers: `shift`, `ctrl`, `alt`, `logo`, `mod2`, `mod3`, `mod5`.
  Button values: `left`, `middle`, `right`, numeric Linux input code string.
- Autostart runs through `/bin/sh -c`, in order, after socket/backend startup. Reload
  runs only commands added since the previous config; existing ones never rerun.
  When mono exits, it stops the programs it autostarted, so daemons such as
  `pipewire` do not linger into the next login. Programs that detach into
  their own session are left alone.

Builders create their target array if absent. C parser limits still apply: 64 rules,
16 monitor rules, 256 keybinds/buttons, 32 autostart commands.

## Bindings / Actions

`mono.bind(combo, callback)` accepts ordinary callbacks and action factories:

```lua
mono.bind("logo+f", mono.toggle_fullscreen())
mono.bind("logo+h", mono.focus("left"))
mono.bind("logo+shift+l", mono.swap("right"))
mono.bind("logo+space", mono.spawn("wofi", "--show", "drun"))

mono.bind("logo+shift+return", function()
	mono.action("spawn", os.getenv("TERMINAL") or "foot")
end)
```

Factories: `spawn`, `kill_client`, `toggle_floating`, `toggle_fullscreen`,
`toggle_gaps`, `quit`, `view`, `toggle_view`, `tag`, `toggle_tag`, `view_shift`,
`tag_shift`, `focus`, `swap`, `focus_monitor`, `move_to_monitor`, `layout`,
`move_resize`, `toggle_canvas`, `canvas_pan`, `move_center`, `vt`.

Factory arguments become action strings; numbers are converted to strings. Use
`mono.action(name, ...)` inside callbacks for dynamic behavior. Binding combos are
`modifier+...+XKB-keysym`, case-insensitive. See [Actions](actions.md) for action
arguments.

## Legacy Compatibility

Global tables remain valid and can coexist with the builder API:

```lua
appearance = { gaps = 10 }
rules = { { app_id = "foot", floating = true } }
mono.bind("logo+return", function() mono.action("spawn", "foot") end)
```

Do not define a builder-managed list globally *after* appending builders: replacing
`rules`, `monitors`, `buttons`, or `autostart` discards earlier builder entries.

## Modules

```lua
local base = os.getenv("XDG_CONFIG_HOME") or (os.getenv("HOME") .. "/.config")
package.path = base .. "/mono/?.lua;" .. package.path
require("settings.appearance")
require("bindings.keys")
```

Modules share globals and the `mono` API. Define base tables before modules that
append builders.

## Hot Reload

mono watches the active config with inotify. Valid reload replaces runtime config
and callbacks; errors retain the prior config. Everything applies without a restart:

- bindings, mouse bindings, rules for new windows, and later action reads
- input settings (key repeat, libinput options) on connected devices
- gaps; switching between zero and non-zero gaps turns them on or off everywhere,
  otherwise each monitor keeps its `togglegaps` state
- border sizes and colors on existing windows, root and fullscreen backgrounds
- monitor rules: a monitor whose matching rule changed gets its scale, transform,
  position, and layout re-applied; unchanged rules leave runtime layout switches alone
- autostart: newly added commands run; existing ones do not rerun, and removed
  ones keep running until stopped by hand
