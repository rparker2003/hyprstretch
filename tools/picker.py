#!/usr/bin/env python3
"""Show a clickable physical monitor layout for the active window."""

import json
import subprocess
import sys

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gtk


def query(*arguments):
    result = subprocess.run(["hyprctl", "-j", *arguments], check=True, text=True, capture_output=True)
    return json.loads(result.stdout)


def close_existing_picker():
    windows = query("clients")
    existing = next((window for window in windows if window.get("class") == "com.rparker.hyprstretch-picker"), None)
    if existing:
        subprocess.run(["hyprctl", "eval", "hl.dispatch(hl.dsp.window.close())"], check=True)
        return True
    return False


class Picker(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="com.rparker.hyprstretch-picker")
        self.window_info = query("activewindow")
        self.monitors = query("monitors")
        self.selected = {monitor["name"] for monitor in self.monitors if monitor["id"] == self.window_info.get("monitor")}

    def do_activate(self):
        style = Gtk.CssProvider()
        style.load_from_data(b"""
            .hyprstretch-picker {
                background-color: alpha(@theme_bg_color, 0.94);
                color: @theme_fg_color;
                border: 1px solid @theme_selected_bg_color;
                border-radius: 12px;
            }
            .hyprstretch-picker button.monitor-tile {
                background-color: alpha(@theme_base_color, 0.86);
                color: @theme_text_color;
                border: 1px solid @borders;
                border-radius: 10px;
            }
            .hyprstretch-picker button.monitor-tile:checked {
                background-color: alpha(@theme_selected_bg_color, 0.88);
                color: @theme_selected_fg_color;
                border: 2px solid @theme_selected_bg_color;
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
        outer.append(Gtk.Label(label="Click monitors to include them in the span"))

        min_x = min(monitor["x"] for monitor in self.monitors)
        min_y = min(monitor["y"] for monitor in self.monitors)
        max_x = max(monitor["x"] + monitor["width"] for monitor in self.monitors)
        max_y = max(monitor["y"] + monitor["height"] for monitor in self.monitors)
        scale = min(620 / (max_x - min_x), 310 / (max_y - min_y))
        layout = Gtk.Fixed()
        layout.set_size_request(round((max_x - min_x) * scale), round((max_y - min_y) * scale))
        self.buttons = {}
        for monitor in self.monitors:
            button = Gtk.ToggleButton(label=monitor["name"])
            button.add_css_class("monitor-tile")
            button.set_size_request(max(82, round(monitor["width"] * scale)), max(68, round(monitor["height"] * scale)))
            button.set_active(monitor["name"] in self.selected)
            button.connect("toggled", self.on_toggle, monitor["name"])
            layout.put(button, round((monitor["x"] - min_x) * scale), round((monitor["y"] - min_y) * scale))
            self.buttons[monitor["name"]] = button
        outer.append(layout)

        actions = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=10)
        actions.set_halign(Gtk.Align.END)
        cancel = Gtk.Button(label="Cancel")
        cancel.connect("clicked", lambda *_: self.quit())
        apply = Gtk.Button(label="Apply")
        apply.add_css_class("suggested-action")
        apply.connect("clicked", self.apply_selection)
        actions.append(cancel)
        actions.append(apply)
        outer.append(actions)
        window.set_child(outer)
        keys = Gtk.EventControllerKey()
        keys.connect("key-pressed", self.on_key_pressed)
        window.add_controller(keys)
        window.present()

    def on_key_pressed(self, controller, keyval, keycode, state):
        if keyval == Gdk.KEY_Escape:
            self.quit()
            return True
        return False

    def on_toggle(self, button, name):
        if button.get_active():
            self.selected.add(name)
        else:
            self.selected.discard(name)

    def apply_selection(self, *_):
        if len(self.selected) < 2:
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
