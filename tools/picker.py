#!/usr/bin/env python3
"""Show a clickable physical monitor layout for the active window."""

import json
import os
import signal
import subprocess
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, GLib, Gtk

PICKER_CLASS = "com.rparker.hyprstretch-picker"


def query(*arguments):
    result = subprocess.run(["hyprctl", "-j", *arguments], check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def close_existing_picker():
    windows = query("clients")
    existing = next((window for window in windows if window.get("class") == PICKER_CLASS), None)
    if existing and existing.get("pid", -1) > 0:
        os.kill(existing["pid"], signal.SIGTERM)
        return True
    return False


def logical_size(monitor):
    scale = monitor.get("scale") or 1.0
    width = monitor["width"] / scale
    height = monitor["height"] / scale
    if monitor.get("transform", 0) % 2 == 1:
        width, height = height, width
    return width, height


class Picker(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="com.rparker.hyprstretch-picker")
        self.window_info = query("activewindow")
        if not self.window_info.get("address"):
            raise ValueError("no active window to stretch")
        all_monitors = [m for m in query("monitors") if not m.get("disabled", False) and m.get("dpmsStatus", True)]
        anchor = next((m for m in all_monitors if m["id"] == self.window_info.get("monitor")), None)
        if anchor:
            _, anchor_h = logical_size(anchor)
            self.monitors = [
                m for m in all_monitors
                if abs(m["y"] - anchor["y"]) < 0.01
                and abs(logical_size(m)[1] - anchor_h) < 0.01
                and abs(m["scale"] - anchor["scale"]) < 0.001
            ]
        else:
            self.monitors = all_monitors
        if not self.monitors:
            raise ValueError("no compatible monitors found")
        self.monitors.sort(key=lambda monitor: monitor["x"])
        self.selected = {monitor["name"] for monitor in self.monitors if monitor["id"] == self.window_info.get("monitor")}
        self.adjusted = False
        GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, signal.SIGTERM, self.on_close_or_escape)

    def do_activate(self):
        style = Gtk.CssProvider()
        style.load_from_data(b"""
            .hyprstretch-picker {
                background-color: #121216;
                color: #f2f2f5;
                border: 1px solid rgba(255, 255, 255, 0.28);
                border-radius: 12px;
            }
            .hyprstretch-picker button.monitor-tile {
                background-image: none;
                background-color: #222228;
                color: #9a9aa6;
                border: 1px solid rgba(255, 255, 255, 0.16);
                border-radius: 10px;
                font-weight: 500;
            }
            .hyprstretch-picker button.monitor-tile:checked {
                background-image: none;
                background-color: #f2f2f7;
                color: #111114;
                border: 2px solid #ffffff;
                font-weight: 700;
            }
            .hyprstretch-picker button.picker-action {
                background-image: none;
                background-color: rgba(38, 38, 44, 0.95);
                color: #d8d8e0;
                border: 1px solid rgba(255, 255, 255, 0.2);
                border-radius: 8px;
                padding: 6px 14px;
            }
            .hyprstretch-picker button.picker-action.suggested-action {
                background-image: none;
                background-color: #f2f2f5;
                color: #111114;
                border: 1px solid #ffffff;
                font-weight: 700;
            }
        """)
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), style, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
        window = Gtk.ApplicationWindow(application=self, title="Stretch across monitors")
        window.set_modal(True)
        window.set_resizable(False)
        window.add_css_class("hyprstretch-picker")
        outer = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=18)
        outer.set_margin_top(22)
        outer.set_margin_bottom(18)
        outer.set_margin_start(22)
        outer.set_margin_end(22)
        heading = Gtk.Label(label="Choose monitors")
        heading.add_css_class("title-2")
        outer.append(heading)
        outer.append(Gtk.Label(label="Press 1–9 (or click) to toggle, A to select all • Enter or Esc to apply"))

        min_x = min(monitor["x"] for monitor in self.monitors)
        min_y = min(monitor["y"] for monitor in self.monitors)
        max_x = max(monitor["x"] + logical_size(monitor)[0] for monitor in self.monitors)
        max_y = max(monitor["y"] + logical_size(monitor)[1] for monitor in self.monitors)
        scale = min(620 / (max_x - min_x), 310 / (max_y - min_y))
        layout = Gtk.Fixed()
        layout.set_size_request(round((max_x - min_x) * scale), round((max_y - min_y) * scale))
        self.buttons = {}
        self.ordered_buttons = []
        for index, monitor in enumerate(self.monitors, start=1):
            mon_w, mon_h = logical_size(monitor)
            label = f"[{index}]  {monitor['name']}" if index <= 9 else monitor["name"]
            button = Gtk.ToggleButton(label=label)
            button.add_css_class("monitor-tile")
            button.set_size_request(max(82, round(mon_w * scale)), max(68, round(mon_h * scale)))
            button.set_active(monitor["name"] in self.selected)
            button.connect("toggled", self.on_toggle, monitor["name"])
            layout.put(button, round((monitor["x"] - min_x) * scale), round((monitor["y"] - min_y) * scale))
            self.buttons[monitor["name"]] = button
            self.ordered_buttons.append(button)
        outer.append(layout)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        actions.set_halign(Gtk.Align.END)
        cancel = Gtk.Button(label="Cancel")
        cancel.add_css_class("picker-action")
        cancel.connect("clicked", lambda *_: self.quit())
        apply = Gtk.Button(label="Apply")
        apply.add_css_class("picker-action")
        apply.add_css_class("suggested-action")
        apply.connect("clicked", self.apply_selection)
        actions.append(cancel)
        actions.append(apply)
        outer.append(actions)
        window.set_child(outer)
        window.set_default_widget(apply)
        keys = Gtk.EventControllerKey()
        keys.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        keys.connect("key-pressed", self.on_key_pressed)
        window.add_controller(keys)
        window.present()

    def on_close_or_escape(self, *_):
        if self.adjusted and self.selected:
            self.apply_selection()
        else:
            self.quit()
        return GLib.SOURCE_REMOVE

    def on_key_pressed(self, controller, keyval, keycode, state):
        if keyval == Gdk.KEY_Escape:
            self.on_close_or_escape()
            return True
        if keyval in (Gdk.KEY_Return, Gdk.KEY_KP_Enter, Gdk.KEY_ISO_Enter):
            self.apply_selection()
            return True
        if Gdk.KEY_1 <= keyval <= Gdk.KEY_9:
            index = keyval - Gdk.KEY_1
            if index < len(self.ordered_buttons):
                button = self.ordered_buttons[index]
                button.set_active(not button.get_active())
            return True
        if Gdk.KEY_KP_1 <= keyval <= Gdk.KEY_KP_9:
            index = keyval - Gdk.KEY_KP_1
            if index < len(self.ordered_buttons):
                button = self.ordered_buttons[index]
                button.set_active(not button.get_active())
            return True
        if keyval in (Gdk.KEY_a, Gdk.KEY_A):
            self.adjusted = True
            for button in self.ordered_buttons:
                button.set_active(True)
            return True
        return False

    def on_toggle(self, button, name):
        self.adjusted = True
        if button.get_active():
            self.selected.add(name)
        else:
            self.selected.discard(name)

    def apply_selection(self, *_):
        if len(self.selected) < 1:
            return
        result = subprocess.run(["hyprctl", "hyprstretch", "stretch", f'window={self.window_info["address"]}', *sorted(self.selected)],
                                check=False, text=True, capture_output=True)
        if result.returncode == 0 and result.stdout.strip().endswith("ok"):
            self.quit()
            return
        dialog = Gtk.AlertDialog(message=result.stdout.strip() or result.stderr.strip() or "Could not stretch this window")
        dialog.show(self.get_active_window())


if __name__ == "__main__":
    try:
        if close_existing_picker():
            sys.exit(0)
        Picker().run(sys.argv[:1])
    except (subprocess.CalledProcessError, KeyError, ValueError) as error:
        print(f"Hyprstretch picker: {error}", file=sys.stderr)
        sys.exit(1)
