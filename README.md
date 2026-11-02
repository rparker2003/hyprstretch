# Hyprstretch

**Global fullscreen** and **partial multi-monitor fullscreen** for Hyprland—stretch any window across all (or a chosen subset) of your monitors without floating it. Super easy!

Hyprland doesn't include built-in global fullscreen across multiple monitors yet, and while I looked into [Hyprspan](https://github.com/gondhijagapathi/hyprspan), it didn't do everything I wanted—so I built **Hyprstretch**! It works with every Wayland and XWayland application I've tested so far, including sim-racing games like *Assetto Corsa* across triple monitors, ultrawide workflows, and terminals or apps that don't support multi-monitor spanning on their own.

### Modes: Global Fullscreen vs. Partial Fullscreen

- **Global Fullscreen (Default Span):** Stretches the focused window across your entire horizontal monitor row (or your configured default monitors) in one keypress, and restores its exact original tiled/floating geometry when toggled off.
- **Partial Fullscreen (GUI Monitor Picker):** Want a window stretched across just 2 of your 3 monitors, or want to exclude an extra TV/side display? Open the visual monitor picker (`tools/picker.py`), click the physical monitor tiles you want to span, and click **Apply**:

![Hyprstretch monitor selector](images/monitor-picker.png)

Full disclosure: This was created entirely by AI (Codex + Gemini). It ran an extensive set of tests it created, then loaded the plugin. I manually tested everything, and it works how I expect!
I do not take responsibility for issues this plugin causes or configurations it may break; I can only say it works fine in my environment -- I don't see why it wouldn't work on others just as well, though.

## Install

Hyprstretch targets Hyprland 0.57 and needs matching headers, a C++26 compiler, `make`, `pkg-config`, and Hyprland's development dependencies. The optional GUI picker requires Python 3, GTK 4, and PyGObject (`python-gobject`).

```sh
git clone https://github.com/rparker52003/hyprstretch.git
cd hyprstretch
make
hyprctl plugin load "$PWD/build/hyprstretch.so"
```

## Configure Keybindings

Hyprstretch does not hardcode any keybindings, so it never collides with your existing config. Below are the recommended bindings (`SUPER + G` for instant global fullscreen toggle, and `SUPER + SHIFT + G` for the interactive monitor picker).

### Option A: Standard `hyprland.conf`

```ini
# Load the plugin on startup
exec-once = hyprctl plugin load /home/you/src/hyprstretch/build/hyprstretch.so

# Auto-detect all monitors in the active window's horizontal row:
bind = SUPER, G, exec, python3 /home/you/src/hyprstretch/tools/toggle.py

# Or specify exact monitor interfaces (order does not matter; sorted left-to-right automatically):
# bind = SUPER, G, exec, python3 /home/you/src/hyprstretch/tools/toggle.py DP-1 DP-2 DP-3
# bind = SUPER, G, exec, hyprctl hyprstretch toggle HDMI-A-1 DP-1

# Open the visual monitor picker for partial/custom spans:
bind = SUPER SHIFT, G, exec, python3 /home/you/src/hyprstretch/tools/picker.py
```

### Option B: Hyprland Lua Config (`hyprland.lua`)

```lua
local hyprstretch = "/home/you/src/hyprstretch"

-- Auto-detect all monitors in the active window's horizontal row:
hl.bind("SUPER + G",
    hl.dsp.exec_cmd("python3 " .. hyprstretch .. "/tools/toggle.py"),
    { description = "Toggle global fullscreen span or close monitor picker" })

-- Or specify exact monitor interfaces (e.g. triple DP setup or dual HDMI+DP):
-- hl.bind("SUPER + G",
--     hl.dsp.exec_cmd("python3 " .. hyprstretch .. "/tools/toggle.py DP-1 DP-2 DP-3"),
--     { description = "Toggle span across DP-1, DP-2, and DP-3" })

hl.bind("SUPER + SHIFT + G",
    hl.dsp.exec_cmd("python3 " .. hyprstretch .. "/tools/picker.py"),
    { description = "Open monitor picker for partial/custom span" })
```

> **Note on Defaults:** If you do not pass monitor names to `toggle.py` (or `hyprctl hyprstretch toggle`), Hyprstretch automatically detects and spans all enabled monitors sharing the active window's horizontal row, height, and scale. If you want to restrict `Super+G` to specific monitors, pass them as arguments (`toggle.py DP-2 DP-3 DP-1`).

## How to Use

- **Toggle Global Fullscreen (`Super+G`):** Stretches the focused window across your horizontal monitor row. Press `Super+G` again to restore the window to its original single-monitor tiled or floating state.
- **Pick Specific Monitors (`Super+Shift+G`):** Opens the visual monitor layout with the active window's current monitor pre-selected.
  - Click additional monitors to span across a custom subset and click **Apply**.
  - Click **Restore** (or press `Super+G` afterward) to return the window to normal.
  - Press `Escape`, `Super+Shift+G`, or `Super+G` while the picker is open to dismiss it without changes.

See [`docs/API.md`](docs/API.md) for direct `hyprctl hyprstretch` CLI commands and native `hl.plugin.hyprstretch.*` Lua functions.

## Limits

Selected monitors must form one contiguous horizontal row and share a logical height and scale. Include the window's current monitor. Pinned, grouped, and special-workspace windows are unsupported. XWayland requires `xwayland:force_zero_scaling` off. Rebuild after Hyprland updates.

## Checks

```sh
make
make test
make build/window-fixture
python3 tests/picker_smoke.py
python3 tests/integration.py
```

The integration checks use a nested compositor and do not control the parent session. MIT license. Developed by Ryan Parker with AI assistance.

## References

- [hyprwm/Hyprland#1660](https://github.com/hyprwm/Hyprland/issues/1660) — Upstream global fullscreen feature request
- [Hyprspan](https://github.com/gondhijagapathi/hyprspan) — Multi-monitor fullscreen plugin reference
