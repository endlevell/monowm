# Lua Configuration Builder API Design

## Goal

Add a code-driven Lua API without breaking existing global-table configurations. Keep monowm-specific semantics; Hyprland is only a usability reference.

## Compatibility

Existing globals remain supported: `appearance`, `input`, `rules`, `monitors`, `buttons`, `autostart`, `keybinds`, `sloppy_focus`, `bypass_surface_visibility`, `log_level`.

`mono.config { ... }` assigns supplied section tables to those globals before current C parsers consume them. Omitted sections retain Lua defaults/current globals.

## New Lua API

- `mono.config { appearance=..., input=..., rules=..., monitors=..., buttons=..., autostart=..., keybinds=... }`
- `mono.rule { app_id=..., title=..., tags=..., floating=..., monitor=... }`
- `mono.monitor { name=..., layout=..., mfact=..., nmaster=..., scale=..., transform=..., x=..., y=... }`
- `mono.button { mods=..., button=..., action=..., args=... }`
- `mono.autostart(command)`
- Existing `mono.bind(combo, callback)` and `mono.action(name, ...)` remain.
- Action factories return callbacks suitable for `mono.bind`: `mono.spawn(...)`, plus factories for all existing named actions. Factories only capture action name/string arguments; dispatch remains C-owned.

Builders append to global arrays. If absent, each initializes its array. Invalid required builder arguments raise ordinary Lua argument errors. Capacity remains enforced by C parser limits.

## Data Flow

```text
config.lua
  mono.config / builders / action factories
  Lua global tables and callback registry
  existing parse_* functions
  Config
  key/button/input/output/window policy
```

`mono.config` does not create a second C configuration representation. It preserves the parser's existing validation/default behavior.

## Documentation / Verification

Migrate `examples/config.lua` and `docs/configuration.md` to builder API. Retain a legacy compatibility example in documentation. Add a small parser-focused test executable/config fixture if existing build conventions permit; otherwise verify by building and loading a temporary builder config.

## Scope

No dynamic layout DSL, no Lua access to raw wlroots objects, no new dependency, no change to hot-reload policy. No generated protocol edits.
