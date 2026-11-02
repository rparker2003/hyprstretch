# Runtime command API

Monitor names and window selections are runtime arguments. There are no built-in
keybindings, user-specific outputs, profiles, or application names in the plugin.

## Configuration

```ini
plugin:hyprstretch:outputs = DP-2 DP-3 DP-1
```

`plugin:hyprstretch:outputs` is an optional space-separated list of default monitor
names used when `toggle` or `stretch` is invoked without explicit `OUTPUT` arguments.
When left empty (the default), Hyprstretch automatically discovers all enabled
monitors sharing the target window's horizontal row (`y`) and logical height.

## Commands

```sh
hyprctl hyprstretch status
hyprctl hyprstretch toggle [window=ADDRESS] [OUTPUT ...]
hyprctl hyprstretch stretch [window=ADDRESS] [OUTPUT ...]
hyprctl hyprstretch restore [window=ADDRESS]
```

- `toggle` restores an already stretched target, or starts stretching it.
- `stretch` starts or updates a target's selection. Repeating the same request
  keeps it stretched; it does not toggle it off. Its original restore state is
  retained when changing the selection. A single monitor fullscreens the window on
  that monitor.
- `restore` restores a target; it is harmless when that target is already restored.
- `status` reports the plugin version, monitor coordinates, and active sessions.
  This diagnostic text is not a versioned JSON schema.

Omitting `window=ADDRESS` targets the focused application. Supply an `address` from
`hyprctl -j clients` to control an application even after a selector takes focus.
Both `0x1234` and `address:0x1234` forms are accepted. A closed or unknown target
returns an error; the plugin does not silently choose another application.

Omitting outputs uses `plugin:hyprstretch:outputs` if configured, or chooses enabled
monitors sharing the target monitor's top edge and logical height. Explicit
names are preferable when extra displays should be excluded. `hyprctl -j monitors`
is the source of output names. Invalid selections are rejected before changing the
application's geometry. The command replies `ok` on success and `error: ...` on
failure.

## Native Lua API

```lua
hl.plugin.hyprstretch.toggle({ outputs = { "LEFT", "CENTER", "RIGHT" } })
hl.plugin.hyprstretch.stretch({ window = "0x1234", outputs = { "LEFT", "CENTER" } })
hl.plugin.hyprstretch.restore({ window = "0x1234" })
```

Each function returns `success, error` (boolean and optional string). Omit the
options table to use the focused window and default monitor selection. Explicit
outputs must be a nonempty array. Invalid options return an error without
changing the window. Functions exist while the plugin is loaded.

```lua
hl.bind("SUPER + G", function()
    hl.plugin.hyprstretch.toggle()
end, { description = "Stretch or restore" })
```

Put plugin calls inside the keybinding callback so configuration can load before
the plugin starts. The Lua API does not use Hyprland's discontinued legacy
dispatcher-registration interface.

## Keybindings and integrations

An executable command works in both `hyprland.conf` and Lua configurations:

```ini
bind = SUPER, G, exec, hyprctl hyprstretch toggle
```

```lua
hl.bind("SUPER + G", hl.dsp.exec_cmd("hyprctl hyprstretch toggle"),
        { description = "Toggle stretch" })
```

External selectors should record the selected application's address, gather
output names, and call `stretch window=ADDRESS OUTPUT ...`. Applications using
`subprocess` should pass an argument list rather than interpolate untrusted
values into a shell command.

The optional `tools/picker.py` (`hyprstretch-picker`) selector is a separate client
of this API. Bind it with `SUPER + SHIFT + G`; it captures the active window, draws
the compatible horizontal monitor layout sorted left-to-right (`[1]`–`[9]`) with that
window's monitor selected, and lets the user toggle monitors with `1`–`9` (or click)
or select all with `A`. Pressing `Enter` (or `Escape` / reopening the picker after
making an adjustment) sends the chosen outputs to `hyprstretch stretch`; pressing
`Escape` or reopening the picker without making adjustments dismisses it without
changes. `tools/toggle.py` (`hyprstretch-toggle`) closes the picker while open and
otherwise toggles the span.
