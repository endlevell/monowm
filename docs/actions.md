# Actions

`mono.action()` invokes compositor behavior from a Lua callback:

```lua
mono.bind("logo+f", function()
	mono.action("togglefullscreen")
end)
```

All arguments after action name are strings. Actions that expect numbers parse
them as decimal integers.

## Programs and Session

### `spawn`

Start a program. Each argument becomes one `argv` item; no shell expansion
occurs unless you explicitly start a shell.

```lua
mono.action("spawn", "foot")
mono.action("spawn", "wofi", "--show", "drun")
mono.action("spawn", "sh", "-c", "grim - | wl-copy")
```

### `killclient`

Ask focused client to close.

```lua
mono.action("killclient")
```

### `quit`

Terminate compositor cleanly.

```lua
mono.action("quit")
```

### `chvt`

Switch Linux virtual terminal. Requires direct DRM session.

```lua
mono.action("chvt", "2")
```

## Window State

### `togglefloating`

Toggle focused window between tiled and floating state.

```lua
mono.action("togglefloating")
```

### `togglefullscreen`

Toggle focused window fullscreen.

```lua
mono.action("togglefullscreen")
```

### `movecenter`

Center focused floating window on selected monitor. Tiled windows are ignored.

```lua
mono.action("movecenter")
```

### `moveresize`

Begin interactive mouse move or resize for window under pointer.

```lua
mono.action("moveresize", "move")
mono.action("moveresize", "resize")
```

Normally used from `buttons`, because operation follows pointer until button
release.

## Layout

### `togglegaps`

Enable or disable configured gaps on selected monitor.

```lua
mono.action("togglegaps")
```

### `setlayout`

Select layout by zero-based compiled layout index:

- `0`: floating (`"><>"`)
- `1`: dwindle (`"[T]"`)

```lua
mono.action("setlayout", "0")
mono.action("setlayout", "1")
```

Omitting index toggles between selected monitor's two layout slots.

### `focusdir`

Focus tiled window nearest given direction.

```lua
mono.action("focusdir", "left")
mono.action("focusdir", "right")
mono.action("focusdir", "up")
mono.action("focusdir", "down")
```

### `swapdir`

Move focused tiled window toward given direction by swapping/repositioning it
inside dwindle tree.

```lua
mono.action("swapdir", "left")
```

Accepted directions match `focusdir`.

## Tags

Tags use bitmasks. For tag number `n`, mask is `1 << (n - 1)`.

```lua
local tag_1 = tostring(1 << 0) -- "1"
local tag_2 = tostring(1 << 1) -- "2"
local tag_9 = tostring(1 << 8) -- "256"
```

### `view`

Show exact tag mask. String `"all"` shows all tags. Calling without argument
returns to previous tag set.

```lua
mono.action("view", "1")
mono.action("view", "all")
mono.action("view")
```

### `toggleview`

Add/remove tags in current view. Operation is ignored if it would leave no
visible tags.

```lua
mono.action("toggleview", "4")
```

### `tag`

Move focused window to exact tag mask.

```lua
mono.action("tag", "8")
```

### `toggletag`

Add/remove tags on focused window. Operation is ignored if it would leave
window with no tags.

```lua
mono.action("toggletag", "2")
```

### `viewshift`

View next or previous tag relative to lowest active tag.

```lua
mono.action("viewshift", "1")
mono.action("viewshift", "-1")
```

### `tagshift`

Move focused window to next or previous tag.

```lua
mono.action("tagshift", "1")
mono.action("tagshift", "-1")
```

## Monitors

### `focusmon`

Focus monitor in horizontal direction.

```lua
mono.action("focusmon", "left")
mono.action("focusmon", "right")
```

### `tagmon`

Send focused window to monitor in horizontal direction.

```lua
mono.action("tagmon", "left")
mono.action("tagmon", "right")
```

## Canvas

### `togglecanvas`

Toggle canvas mode for currently viewed tag mask.

```lua
mono.action("togglecanvas")
```

### `canvaspan`

Begin canvas pan. Normally bound to mouse button:

```lua
buttons = {
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

Pan ends when button is released.

See [Canvas Mode](canvas.md) for complete behavior.

## Binding Loop Example

```lua
local function bind_action(keys, action, ...)
	local args = { ... }
	mono.bind(keys, function()
		mono.action(action, table.unpack(args))
	end)
end

for tag = 1, 9 do
	local key = tostring(tag)
	local mask = tostring(1 << (tag - 1))
	bind_action("logo+" .. key, "view", mask)
	bind_action("logo+shift+" .. key, "tag", mask)
end
```
