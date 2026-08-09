# monomsg IPC

`monomsg` reads and changes mono state through `dwl-ipc-unstable-v2`. It is
useful for status bars, shell scripts, and debugging.

The client connects to compositor selected by `WAYLAND_DISPLAY`.

## Build Location

After `make`:

```text
tools/monomsg/build/monomsg
```

`make install` installs it as `monomsg` under configured `PREFIX/bin`.

## Modes

`monomsg` has three main modes:

- `-g`: get current state, then exit
- `-w`: watch state changes
- `-s`: set state

Running get/watch without field flags selects all runtime fields.

```sh
monomsg -g
monomsg -w
```

## Read State

Common queries:

```sh
monomsg -O        # list output names
monomsg -T        # print number of tags
monomsg -L        # list available layouts
monomsg -P        # print compositor PID
monomsg -F        # print focused output
```

Select fields in get mode:

```sh
monomsg -g -o     # selected-monitor state
monomsg -g -t     # tag state
monomsg -g -l     # layout
monomsg -g -c     # focused client title and app ID
monomsg -g -m     # fullscreen state
monomsg -g -f     # floating state
```

Read one output by name:

```sh
monomsg -o HDMI-A-1 -g
monomsg -o eDP-1 -g -t -l
```

Without `-o OUTPUT`, state is printed for all outputs.

## Watch Changes

Watch all fields:

```sh
monomsg -w
```

Watch only tags and focused client:

```sh
monomsg -w -t -c
```

Watch output hotplug events:

```sh
monomsg -w -O
```

Output is line-oriented and flushed after each state frame, making it suitable
for a shell loop:

```sh
monomsg -w -t -l | while read -r line; do
	printf '%s\n' "$line"
done
```

## Change Layout

Select layout by name:

```sh
monomsg -s -l '[T]'
monomsg -s -l '><>'
```

Or use zero-based layout index:

```sh
monomsg -s -l 0
monomsg -s -l 1
```

Target one output:

```sh
monomsg -o HDMI-A-1 -s -l '[T]'
```

## Change Viewed Tags

Important: `monomsg` tag arguments use zero-based indices. Index `0` means
display tag 1, index `1` means display tag 2, and so on.

View tag 1:

```sh
monomsg -s -t 0
```

View tag 5:

```sh
monomsg -s -t 4
```

Prefix with `!` to replace current tag set without toggling tagset history:

```sh
monomsg -s -t '!0'
```

Tag-set expressions may append one operator per following tag index:

- `+`: add tag
- `-`: remove tag
- `^`: toggle tag

For simple scripts, exact single-tag commands above are clearest.

## Move Focused Client to Tag

Client tag arguments also use zero-based indices.

Move focused client to tag 3:

```sh
monomsg -s -c 2
```

Target one output's focused client:

```sh
monomsg -o HDMI-A-1 -s -c 2
```

Client-tag expressions support same `+`, `-`, and `^` suffix operators.

## Output Format

Most runtime lines begin with output name:

```text
HDMI-A-1 selmon 1
HDMI-A-1 layout [T]
HDMI-A-1 title Terminal
HDMI-A-1 appid foot
```

`tag` lines expose individual tag events. `tags` lines summarize occupied,
selected, focused-client, and urgent bitmasks.

Protocol output is intended for scripts; format may evolve with IPC protocol.

## Errors

`bad display` means `WAYLAND_DISPLAY` is unset, wrong, or inaccessible.

`bad dwl-ipc protocol` means connected compositor does not expose required IPC
protocol, or `monomsg` connected to another Wayland compositor.

Use this to verify target compositor PID:

```sh
monomsg -P
```
