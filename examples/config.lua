-- monowm configuration. This is normal Lua: compose tables, loops, functions.

local terminal = os.getenv("TERMINAL") or "foot"
local launcher = { "wofi", "--show", "drun" }
local tag = function(n) return 1 << (n - 1) end

mono.config {
	sloppy_focus = true,
	bypass_surface_visibility = false,
	log_level = "error",

	appearance = {
		gaps = 10,
		smart_gaps = false,
		inner_border_px = 2,
		outer_border_px = 2,
		root_color = 0x222222ff,
		inner_border_color = 0x555555ff,
		outer_border_color = 0x111111ff,
		inner_focus_color = 0xeeeeeeff,
		outer_focus_color = 0x555555ff,
		inner_urgent_color = 0xff5555ff,
		outer_urgent_color = 0x991111ff,
		fullscreen_bg = 0x000000ff,
	},

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
	},
}

-- First matching output rule wins. `name = nil` is the catch-all.
mono.monitor {
	name = nil, layout = "dwindle", mfact = 0.55, nmaster = 1,
	scale = 1.0, transform = 0, x = -1, y = -1,
}

-- Rules append in order. app_id and title are substring matches.
mono.rule { app_id = "org.gimp.GIMP", floating = true, monitor = -1 }
mono.rule { title = "Picture-in-Picture", floating = true, monitor = -1 }

-- Commands run after the Wayland socket is live.
for _, command in ipairs {
	"dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP",
	"systemctl --user import-environment WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP",
	"waybar",
} do
	mono.autostart(command)
end

mono.button { mods = { "logo" }, button = "left", action = "moveresize", args = { "move" } }
mono.button { mods = { "logo" }, button = "right", action = "moveresize", args = { "resize" } }
mono.button { mods = { "logo" }, button = "middle", action = "togglefloating" }
mono.button { mods = {}, button = "middle", action = "canvaspan" }

-- Action factories make common bindings declarative.
mono.bind("logo+return", mono.spawn(terminal))
mono.bind("logo+space", mono.spawn(table.unpack(launcher)))
mono.bind("logo+q", mono.kill_client())
mono.bind("logo+shift+e", mono.quit())
mono.bind("logo+f", mono.toggle_fullscreen())
mono.bind("logo+v", mono.toggle_floating())
mono.bind("logo+g", mono.toggle_gaps())
mono.bind("logo+c", mono.toggle_canvas())
mono.bind("logo+shift+c", mono.move_center())

for direction, key in pairs { left = "h", down = "j", up = "k", right = "l" } do
	mono.bind("logo+" .. key, mono.focus(direction))
	mono.bind("logo+shift+" .. key, mono.swap(direction))
end

mono.bind("logo+bracketleft", mono.view_shift(-1))
mono.bind("logo+bracketright", mono.view_shift(1))
mono.bind("logo+shift+bracketleft", mono.tag_shift(-1))
mono.bind("logo+shift+bracketright", mono.tag_shift(1))
mono.bind("logo+comma", mono.focus_monitor("left"))
mono.bind("logo+period", mono.focus_monitor("right"))
mono.bind("logo+shift+comma", mono.move_to_monitor("left"))
mono.bind("logo+shift+period", mono.move_to_monitor("right"))

for n = 1, 9 do
	mono.bind("logo+" .. n, mono.view(tag(n)))
	mono.bind("logo+ctrl+" .. n, mono.toggle_view(tag(n)))
	mono.bind("logo+shift+" .. n, mono.tag(tag(n)))
	mono.bind("logo+ctrl+shift+" .. n, mono.toggle_tag(tag(n)))
end

-- Use callbacks when behavior genuinely needs Lua logic.
mono.bind("logo+shift+return", function()
	mono.action("spawn", terminal)
end)
