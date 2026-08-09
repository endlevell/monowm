# Optical Canvas Zoom Design

## Goal

Canvas zoom must scale rendered window content without asking applications to
resize. Text, terminal grids, web layouts, and application UI remain unchanged
while compositor presents them at another visual scale.

Zoom stays anchored under mouse cursor: world point below cursor before scroll
remains below cursor after scroll.

## Current Behavior

`canvas_layout()` multiplies client geometry by monitor zoom and calls
`resize()`. That sends new dimensions to client. Applications reflow and redraw
for every zoom step.

wlroots 0.20 can scale individual `wlr_scene_buffer` nodes through
`wlr_scene_buffer_set_dest_size()`, but scene trees have no scale transform.
Client scene contains main surface, subsurfaces, popups, and border rectangles,
so mono must transform whole subtree consistently.

## Rendering Model

Client keeps native canvas size in `world_width` and `world_height`. This is
size application renders at. Canvas zoom changes only presentation:

```text
visual position = monitor origin + (world position - pan offset) * zoom
visual size     = native world size * zoom
```

`canvas_layout()` will:

1. Position client root scene node at transformed visual position.
2. Keep client protocol geometry at native world size.
3. Walk client scene subtree.
4. Scale each buffer destination size.
5. Scale each descendant node offset from client root.
6. Scale border rectangles and offsets.

Buffer scaling uses bilinear filtering for fractional zoom.

## Scene State

Each client needs enough state to restore and reapply transform without
accumulating rounding errors:

- current applied visual scale
- a per-client list keyed by scene node pointer containing native descendant
  position and native buffer destination size

Native values always remain source of truth. Repeated zoom computes from native
values, never from previous scaled values.

Surface commits can add, remove, resize, or reposition subsurfaces. Transform
must refresh after commit while client is visible on canvas. Before refresh,
restore previous native state, discard snapshot, let committed scene geometry
become new native state, then snapshot and scale again. Remove snapshot entries
when their scene nodes are destroyed; discard full list on client unmap.

## Mouse-Anchored Zoom

Before changing zoom, convert cursor screen position into world position:

```text
world = pan + (cursor - monitor origin) / old zoom
```

After choosing new zoom, solve pan so same world point maps to same cursor:

```text
new pan = world - (cursor - monitor origin) / new zoom
```

Clamp zoom to existing `0.2` through `3.0` range.

## Input Mapping

Scene hit testing sees scaled destination buffers, but surface-local coordinates
must use client native coordinate space.

For canvas client, pointer coordinates sent to application divide transformed
client-local coordinates by zoom. Non-canvas clients and layer surfaces remain
unchanged.

Interactive move uses visual cursor position converted into world coordinates.
Interactive resize converts visual drag size into native world size, asks client
to resize once for user resize, then reapplies current optical scale.

## Entering and Leaving Canvas

Entering canvas:

- capture current client geometry as native world geometry
- switch visible windows to floating scene layer
- apply optical transform

Leaving canvas:

- clear buffer destination scaling
- restore native descendant positions
- restore texture filter defaults where needed
- return affected clients to tiled state
- let dwindle send normal native resize

No scaled scene state may leak into tiled, floating, fullscreen, or another
monitor mode.

## Fullscreen and Popups

Fullscreen bypasses canvas transform and uses normal monitor geometry.

Popups and subsurfaces inherit client visual scale by transformed descendant
offsets and destination sizes. Popup placement remains relative to native
surface coordinates.

XWayland clients use same scene-buffer transform. Unmanaged XWayland surfaces
remain outside canvas transformation.

## Damage and Frames

Use wlroots scene APIs for buffer destination size and node position changes so
scene damage is generated normally. Existing `wlr_scene_output_commit()` path
remains unchanged.

Clients receive normal frame callbacks. Zoom itself must not schedule client
resize configure events.

## Failure Handling

If scene buffer has no backing buffer or dimensions, skip scaling until next
surface commit. Never use zero destination dimensions; clamp visual dimensions
to at least one pixel.

If client leaves canvas during active move/resize, cancel interactive mode and
restore unscaled scene before arranging.

## Verification

Manual checks:

1. Open terminal in canvas; zoom does not change rows or columns.
2. Open browser; zoom does not trigger responsive reflow.
3. Cursor remains over same content point through zoom.
4. Pointer clicks match visible controls at `0.2`, `0.5`, `1.5`, and `3.0`.
5. Subsurface-heavy apps and menus scale and receive input correctly.
6. Move and resize work at non-`1.0` zoom.
7. Secondary monitors keep correct origin and pointer anchor.
8. Disable canvas; windows tile at scale `1.0` without stale offsets.
9. Toggle fullscreen in canvas; fullscreen stays native and fills output.
10. Repeated zoom cycles do not drift due to accumulated rounding.

Code checks should isolate coordinate conversion and mouse-anchor math into
small pure helpers with assertions for monitor origin, pan, and zoom cases.

## Scope

Included:

- optical scaling for canvas client surfaces, subsurfaces, popups, and borders
- mouse-anchored zoom
- pointer coordinate correction
- move, resize, fullscreen, canvas-exit restoration

Excluded:

- pinch gesture support
- per-tag pan and zoom storage
- animation between zoom levels
- configurable filter mode
