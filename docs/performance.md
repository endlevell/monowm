# Performance changes — 2026-11-01

The optimizations below preserve animations, Lua configuration, XWayland support,
native buffer crops, input mapping, and the fixes in the bug audit.

## Applied changes

| Area | Implementation |
| --- | --- |
| Transform allocation | Keep transform records while a transformed scene node exists. Separate restoring native geometry from releasing records. |
| Transform lookup | Use scene-node addons for lookup and destruction instead of scanning windows and their node lists. `node->data` retains its original owner. |
| Pan and steady frames | Cache native frame/content dimensions and border widths. When those remain valid, update the window root position without rebuilding its contents. |
| Zoom | Reuse native baselines for scale-only changes, applying the final transform directly instead of restoring every node to native size first. |
| Animation frames | Schedule affected outputs and evaluate animations at absolute time. Cross-output windows and snapshots still update on outputs where they are visible. Camera changes render canvas windows once per tick. |
| Configuration | Allocate binding arrays for actual entries. Lua callbacks need no command strings; legacy commands allocate only their supplied arguments. Keep the existing limits. |
| Config ownership | Parse into a fresh configuration, retain the old configuration/Lua state on failure, and transfer ownership on successful reload. Free replaced configurations and shutdown storage. |
| Status | Coalesce requests into one event-loop idle callback. Reuse one monitor summary across stdout, IPC subscribers, and workspace events. Initial IPC subscriptions still receive a complete current snapshot immediately. |
| Dwindle | Mark current tree membership in one pass and prune through direct leaf references. Preserve per-tag trees and the unmap-before-free invariant. |
| Easing | Calculate movement progress once per client render, reusing it for geometry and scale. The animation curves are unchanged. |
| Build/package | Use consistent optimization flags for both programs. Add stripped release binaries with separate debug symbols and a staged-install-compatible release installation target. |

Native commits, deferred scene refreshes, new surfaces, node destruction, border
changes, and explicit restoration invalidate the render cache. Workspace clipping
uses the full transform path. Viewport and rotation/flip regression tests remain
part of `make check`.

Live testing also exposed a `monomsg` completion race: flushing a setter and
immediately closing its connection could lose the request. Setters now send
once and wait for a server roundtrip before returning.

## Measurements

These are local CPU scene microbenchmarks against a saved copy of the tree after
the bug fixes and before optimization. Each timed case performs 1,000 updates;
scene code was compiled with `-O2`. The fixture uses CPU buffers and includes
borders, a surface tree, and an application buffer. The larger case adds 100
rectangles. Timings vary with the machine and workload.

| Measurement | Before | After |
| --- | ---: | ---: |
| Config C structure | 2,294,128 bytes | 58,752 bytes |
| Example config C storage, including owned binding/command allocations | 2,294,128 bytes | 61,904 bytes |
| Transform allocations/frees per 1,000 updates, small fixture | 11,000 / 11,000 | 0 / 0 after setup |
| Transform allocations/frees per 1,000 updates, larger fixture | 111,000 / 111,000 | 0 / 0 after setup |
| Small-fixture pan CPU time | 59.6 ms | 4.4 ms |
| Larger-fixture pan CPU time | 2,731.5 ms | 26.6 ms |
| Small-fixture zoom CPU time | 65.1 ms | 28.9 ms |
| Larger-fixture zoom CPU time | 2,762.8 ms | 290.3 ms |

Configuration figures exclude Lua, allocator overhead, shared libraries, and GPU
buffers. Allocation counters exclude shared-library allocations. Scene timings
are not desktop FPS measurements or predictions of total compositor CPU use.

The default remains `-O2` for speed. A separate `-Os` build was also evaluated;
size optimization can be selected through the existing build flags. LTO is not
required or enabled by default.

## Build, validate, and install

```sh
make
make check
make check-headless   # Python 3 and a working scenefx renderer are required
make performance     # CPU scene measurements and allocation assertions
make release
```

Release output:

- `build/release/mono` and `build/release/mono.debug`
- `tools/monomsg/build/release/monomsg` and `monomsg.debug`

The regular build retains debug information. Release generation is repeatable
and preserves the unstripped binaries. Install the stripped release with:

```sh
sudo make install-release
```

Then restart mono to use the installed binary. `DESTDIR` and `PREFIX` work for
both installation targets. Keep the `.debug` files when debugging a release.
Use a fresh build directory or `make clean` when changing compiler flags.

## Verification and limits

- Normal compositor, animation, parser, and IPC client regression suites pass.
- Live headless tests with zero and one output pass for regular and stripped
  release binaries.
- Builds/tests with XWayland disabled pass.
- AddressSanitizer and UndefinedBehaviorSanitizer pass for the regression
  suite with leak detection disabled for the scene fixture/external libraries.
- The parser ownership/reload tests also pass with leak detection enabled.
- A temporary staged release installation preserves binary contents and debug
  symbol output.

Physical monitor hotplug, real input devices, and mixed-refresh hardware still
need desktop validation.

The conditional snapshot-pruning proposal remains deferred: snapshots already
reference existing application buffers, and no GPU-memory spike was established
that justifies changing which snapshot parts are retained. No animation or
compatibility feature was disabled to obtain these measurements.
