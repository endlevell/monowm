-- Starter configuration for mono.
-- Values use Lua syntax, so normal expressions, loops, and functions work.

-- General behavior ---------------------------------------------------------

sloppy_focus = true
bypass_surface_visibility = false
log_level = "error" -- "silent", "error", "info", or "debug"

-- Window decoration --------------------------------------------------------

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
}

-- Keyboard and pointer -----------------------------------------------------
-- Input options apply when devices are created. Restart mono after changing
-- this section.

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
	scroll_method = "2fg", -- "2fg", "edge", or "button"
	click_method = "button_areas", -- "button_areas" or "clickfinger"
	accel_profile = "adaptive", -- "adaptive" or "flat"
	accel_speed = 0.0, -- libinput range: -1.0 through 1.0
}

-- Output setup -------------------------------------------------------------
-- First matching rule wins. Omit name for a catch-all rule. Position -1, -1
-- lets wlroots place that output automatically.

monitors = {
	-- Example override; keep specific rules above the catch-all rule.
	-- { name = "eDP-1", layout = "dwindle", mfact = 0.50, nmaster = 1,
	--   scale = 2.0, transform = 0, x = 0, y = 0 },

	{ name = nil, layout = "dwindle", mfact = 0.55, nmaster = 1,
	  scale = 1.0, transform = 0, x = -1, y = -1 },
}

-- Window placement ---------------------------------------------------------
-- app_id and title use substring matching. tags is a bitmask: tag 1 is 1,
-- tag 2 is 2, tag 3 is 4, and so on. monitor = -1 keeps current monitor.

rules = {
	-- { app_id = "org.gimp.GIMP", floating = true, monitor = -1 },
	-- { title = "Picture-in-Picture", floating = true, monitor = -1 },
	-- { app_id = "firefox", tags = 1 << 1, floating = false, monitor = -1 },
}

-- Session programs ---------------------------------------------------------
-- Commands start in order after Wayland socket becomes available. Remove any
-- command not installed on your system.

autostart = {
	"dbus-update-activation-environment --systemd WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP",
	"systemctl --user import-environment WAYLAND_DISPLAY XDG_SESSION_TYPE XDG_CURRENT_DESKTOP",
	"waybar",
}

-- Mouse bindings -----------------------------------------------------------
-- Canvas mode uses middle-drag without modifiers. logo+scroll changes zoom.

buttons = {
	{ mods = { "logo" }, button = "left", action = "moveresize", args = { "move" } },
	{ mods = { "logo" }, button = "right", action = "moveresize", args = { "resize" } },
	{ mods = { "logo" }, button = "middle", action = "togglefloating" },
	{ mods = {}, button = "middle", action = "canvaspan" },
}

-- Key bindings -------------------------------------------------------------
-- bind_action covers direct actions. Use mono.bind() directly when callback
-- needs custom Lua logic.

local terminal = "foot"
local launcher = { "wofi", "--show", "drun" }

local function bind_action(keys, action, ...)
	local args = { ... }
	mono.bind(keys, function()
		mono.action(action, table.unpack(args))
	end)
end

local function bind_spawn(keys, command)
	mono.bind(keys, function()
		mono.action("spawn", table.unpack(command))
	end)
end

bind_spawn("logo+return", { terminal })
bind_spawn("logo+space", launcher)

bind_action("logo+q", "killclient")
bind_action("logo+shift+e", "quit")
bind_action("logo+f", "togglefullscreen")
bind_action("logo+v", "togglefloating")
bind_action("logo+g", "togglegaps")

-- Canvas belongs to current tag. Toggle with logo+c, pan using middle-drag,
-- zoom using logo+mouse-wheel, center focused floating window with shift+c.
bind_action("logo+c", "togglecanvas")
bind_action("logo+shift+c", "movecenter")

-- Focus and rearrange dwindle tiles by direction.
for _, direction in ipairs({ "left", "down", "up", "right" }) do
	local keys = ({ left = "h", down = "j", up = "k", right = "l" })[direction]
	bind_action("logo+" .. keys, "focusdir", direction)
	bind_action("logo+shift+" .. keys, "swapdir", direction)
end

-- Move through tags without reaching for number keys.
bind_action("logo+bracketleft", "viewshift", "-1")
bind_action("logo+bracketright", "viewshift", "1")
bind_action("logo+shift+bracketleft", "tagshift", "-1")
bind_action("logo+shift+bracketright", "tagshift", "1")

-- Focus adjacent output or send focused window there.
bind_action("logo+comma", "focusmon", "left")
bind_action("logo+period", "focusmon", "right")
bind_action("logo+shift+comma", "tagmon", "left")
bind_action("logo+shift+period", "tagmon", "right")

-- Number-row bindings select tags, combine tags, and move windows.
for tag = 1, 9 do
	local key = tostring(tag)
	local mask = tostring(1 << (tag - 1))
	bind_action("logo+" .. key, "view", mask)
	bind_action("logo+ctrl+" .. key, "toggleview", mask)
	bind_action("logo+shift+" .. key, "tag", mask)
	bind_action("logo+ctrl+shift+" .. key, "toggletag", mask)
end

-- Splitting this file -------------------------------------------------------
-- Large configurations can be regular Lua modules. Example layout:
--
--   ~/.config/mono/config.lua
--   ~/.config/mono/settings/input.lua
--   ~/.config/mono/settings/appearance.lua
--   ~/.config/mono/bindings/keys.lua
--
-- config.lua can load those files like this:
--
--   local base = os.getenv("XDG_CONFIG_HOME")
--       or (os.getenv("HOME") .. "/.config")
--   package.path = base .. "/mono/?.lua;" .. package.path
--   require("settings.input")
--   require("settings.appearance")
--   require("bindings.keys")
--
-- Modules share the same Lua globals and mono API. A module can assign input,
-- appearance, rules, monitors, buttons, or autostart; binding modules can call
-- mono.bind() and mono.action() exactly as this file does.
