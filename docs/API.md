# Runtime command API

Monitor names and window selections are runtime arguments. There are no built-in
keybindings, user-specific outputs, profiles, or application names in the plugin.

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
  retained when changing the selection.
- `restore` restores a target; it is harmless when that target is already restored.
- `status` reports the plugin version, monitor coordinates, and active sessions.
  This diagnostic text is not a versioned JSON schema.

Omitting `window=ADDRESS` targets the focused application. Supply an `address` from
`hyprctl -j clients` to control an application even after a selector takes focus.
Both `0x1234` and `address:0x1234` forms are accepted. A closed or unknown target
returns an error; the plugin does not silently choose another application.

Omitting outputs chooses monitors sharing the target monitor's top edge, logical
height and scale. Explicit names are preferable when extra displays should be
excluded. `hyprctl -j monitors` is the source of output names. Invalid selections
are rejected before changing the application's geometry. The command replies
`ok` on success and `error: ...` on failure.

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
    hl.plugin.hyprstretch.toggle({ outputs = { "LEFT", "CENTER", "RIGHT" } })
end, { description = "Stretch or restore" })
```

Put plugin calls inside the keybinding callback so configuration can load before
the plugin starts. The Lua API does not use Hyprland's discontinued legacy
dispatcher-registration interface.

## Keybindings and integrations

An executable command is a simple Lua binding that works without any plugin
functions in the config parser:

```lua
hl.bind("SUPER + G", hl.dsp.exec_cmd("hyprctl hyprstretch toggle DP-2 DP-3 DP-1"),
        { description = "Toggle stretch" })
```

External selectors should record the selected application's address, gather
output names, and call `stretch window=ADDRESS OUTPUT ...`. Applications using
`subprocess` should pass an argument list rather than interpolate untrusted
values into a shell command.

The optional `tools/picker.py` selector is a separate client of this API. Bind it
with `SUPER + SHIFT + G`; it captures the active window, draws the physical monitor
layout with that window's monitor selected, then sends the chosen outputs to
`hyprstretch stretch`. Reopening the picker closes it. Super+G closes it while open
and otherwise runs the three-monitor toggle.
