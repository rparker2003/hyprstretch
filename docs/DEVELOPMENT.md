# Development and references

Ryan Parker specified the behavior and directed an AI-assisted implementation
using OpenAI Codex. The source, tests, build files, and documentation in this tree
were written during that rewrite. Changes remain subject to Ryan's review.

An earlier local prototype directly modified Hyprspan. That prototype, including
its original license notices and Git history, is preserved separately rather
than included here. Hyprspan was inspected during research and helped explain
the problem; this project does not claim that its development was uninfluenced
by that research. Its automatic X11 fullscreen-monitor request functionality is
not part of this implementation.

## Engineering references

- [Hyprland issue #1660](https://github.com/hyprwm/Hyprland/issues/1660): upstream
  discussion on global fullscreen / multi-monitor fullscreen spanning in Hyprland.
- [Hyprspan](https://github.com/gondhijagapathi/hyprspan): background on
  application-requested X11 multi-monitor fullscreen and the initial prototype.
- [Hyprland 0.57 plugin API](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/plugins/PluginAPI.hpp):
  hook registration, IPC commands, and Lua function registration.
- [Hyprland fullscreen controller](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/managers/fullscreen/FullscreenController.cpp)
  and [fullscreen types](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/managers/fullscreen/FullscreenTypes.hpp):
  separate compositor and client states. The `TRANSFER` context is documented as
  “Move compositor-owned internal state without changing client-owned state.”
- [Hyprland renderer](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/render/Renderer.hpp):
  rendering context and monitor-versus-workspace scene entry points.
- [Hyprland monitor positioning](https://github.com/hyprwm/Hyprland/blob/v0.57.0/src/state/MonitorPositionController.cpp):
  the distinction between desktop and XWayland coordinates.

The implementation calls these installed Hyprland interfaces; their source is
not vendored into this repository. No Hyprspan files or commits are imported.

## Structure

- `Layout`: monitor-region validation independent of Hyprland runtime state.
- `Engine`: explicit stretch sessions, saved restore state, secondary-monitor
  leases, IPC commands, Lua functions, and lifecycle events.
- `Hooks`: the version-specific bridge to Hyprland rendering, fullscreen queries,
  focus, geometry, and XWayland coordinate arrangement.
- `Plugin`: runtime compatibility check and ownership of the engine.
- `tests`: new geometry tests, an XCB window fixture, and an isolated integration
  runner for native Wayland and XWayland behavior.

The monitor picker is a separate GTK client in `tools/picker.py`. It captures the
active window, displays the physical monitor arrangement, and calls the plugin's
runtime command API. `tests/picker_smoke.py` checks that the picker opens once and
both configured shortcuts close it.
