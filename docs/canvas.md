# Canvas Mode

Canvas mode turns a tag from dwindle tiling into a pannable, zoomable surface.
Other tags can remain tiled.

## Basic Setup

Bind canvas toggle:

```lua
mono.bind("logo+c", function()
	mono.action("togglecanvas")
end)
```

Bind panning to mouse drag:

```lua
buttons = {
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

Zoom is built in: hold `logo` and use pointer scroll wheel while canvas tag is
active. Zoom range is `0.2` through `3.0`. World point under pointer stays
anchored while zoom changes.

## Using Canvas

1. Switch to desired tag.
2. Run `togglecanvas` binding.
3. Open or move windows around freely.
4. Hold middle button and drag to pan viewport.
5. Hold `logo` and scroll to zoom.
6. Run `togglecanvas` again to return windows to dwindle layout.

Canvas windows are not clamped to monitor boundaries. A window can remain far
outside current viewport until you pan back to it.

## World and Screen Coordinates

mono stores each canvas window in world coordinates. Monitor displays one view
of that world:

```text
screen position = monitor origin + (world position - pan offset) * zoom
screen size     = world size * zoom
```

Panning changes viewport offset, not each window's world position. Zoom changes
displayed client geometry around monitor origin.

## Entering Canvas

When a visible tag enters canvas mode:

- tiled windows become floating
- current window positions become world positions
- current sizes become world sizes
- new windows appear near pointer position
- normal monitor boundary clamping is disabled

## Leaving Canvas

When canvas mode is disabled, affected windows return to tiled state and are
inserted into dwindle layout. Their stored world positions remain available if
canvas is enabled again later.

## Per-Tag and Per-Monitor State

Canvas enabled/disabled state is stored per tag. Viewport pan and zoom are
currently stored per monitor, so canvas tags on same monitor share viewport.

Example:

- tag 1 can be dwindle
- tag 2 can be canvas
- tag 3 can be canvas
- tags 2 and 3 share monitor's current pan and zoom values

## Multi-Tag Views

`togglecanvas` operates on currently viewed tag mask. If multiple tags are
visible, all selected tag bits toggle together. For predictable independent
canvas workspaces, switch to one tag before toggling canvas.

## Optical Zoom

mono keeps each application at native canvas size and scales its rendered scene
buffers, subsurfaces, popups, and borders. Applications do not reflow while
zooming: terminal rows, browser layouts, and UI geometry remain stable.

Fractional zoom uses bilinear filtering. User-initiated window resize still
changes native application size, then current optical scale is reapplied.

## Recommended Bindings

```lua
mono.bind("logo+c", function()
	mono.action("togglecanvas")
end)

mono.bind("logo+shift+c", function()
	mono.action("movecenter")
end)

buttons = {
	{ mods = { "logo" }, button = "left", action = "moveresize", args = { "move" } },
	{ mods = { "logo" }, button = "right", action = "moveresize", args = { "resize" } },
	{ mods = {}, button = "middle", action = "canvaspan" },
}
```

`movecenter` helps recover focused floating window visible but awkwardly placed.
It does not find windows completely outside viewport.
