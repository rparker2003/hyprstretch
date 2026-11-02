#!/usr/bin/env python3
"""Check that the selector opens once and a second invocation closes it."""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

project = Path(__file__).resolve().parents[1]


def instances():
    return json.loads(subprocess.check_output(["hyprctl", "-j", "instances"], text=True))


with tempfile.TemporaryDirectory(prefix="hyprstretch-picker-") as directory:
    root = Path(directory)
    config = root / "session.lua"
    config.write_text('''hl.monitor({output="WAYLAND-1", disabled=true})
hl.monitor({output="LEFT", mode="1280x720@60", position="-1280x0", scale=1})
hl.monitor({output="CENTER", mode="1280x720@60", position="0x0", scale=1})
hl.monitor({output="RIGHT", mode="1280x720@60", position="1280x0", scale=1})
hl.config({animations={enabled=false}, ecosystem={no_update_news=true,no_donation_nag=true}})
''')
    before = {entry["instance"] for entry in instances()}
    environment = dict(os.environ)
    environment.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
    environment.pop("DISPLAY", None)
    log = (root / "hyprland.log").open("w")
    compositor = subprocess.Popen(["Hyprland", "--config", str(config)], env=environment,
                                  stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    picker = None
    fixture = None
    try:
        deadline = time.monotonic() + 12
        entry = None
        while time.monotonic() < deadline:
            entry = next((item for item in instances() if item["instance"] not in before and item["pid"] == compositor.pid), None)
            if entry:
                break
            time.sleep(0.1)
        assert entry, "nested Hyprland did not start"
        environment["HYPRLAND_INSTANCE_SIGNATURE"] = entry["instance"]
        environment["WAYLAND_DISPLAY"] = entry["wl_socket"]

        def ctl(*args):
            return subprocess.check_output(["hyprctl", *args], env=environment, text=True).strip()

        for name in ("LEFT", "CENTER", "RIGHT"):
            ctl("output", "create", "headless", name)
        plugin = root / "hyprstretch.so"
        plugin.write_bytes((project / "build/hyprstretch.so").read_bytes())
        assert ctl("plugin", "load", str(plugin)) == "ok"
        fixture = subprocess.Popen(["kitty", "-o", "linux_display_server=wayland", "--title", "picker-smoke"],
                                   env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        def find_client(title):
            return next((client for client in json.loads(ctl("-j", "clients")) if client["title"] == title), None)

        def wait(predicate):
            until = time.monotonic() + 8
            while time.monotonic() < until:
                value = predicate()
                if value:
                    return value
                time.sleep(0.1)
            raise AssertionError("timed out waiting for picker state")

        window = wait(lambda: find_client("picker-smoke"))
        address = window["address"]
        picker = subprocess.Popen(["python3", str(project / "tools/picker.py")], env=environment,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        dialog = wait(lambda: next((client for client in json.loads(ctl("-j", "clients")) if client["class"] == "com.rparker.hyprstretch-picker"), None))
        assert dialog["floating"], "picker window did not float"
        subprocess.run(["python3", str(project / "tools/picker.py")], env=environment, check=True, timeout=8)
        wait(lambda: not any(client["class"] == "com.rparker.hyprstretch-picker" for client in json.loads(ctl("-j", "clients"))) and True)
        assert find_client("picker-smoke")["address"] == address
        assert "manual sessions: 0" in ctl("hyprstretch")
        picker.wait(timeout=5)

        picker = subprocess.Popen(["python3", str(project / "tools/picker.py")], env=environment,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
        wait(lambda: next((client for client in json.loads(ctl("-j", "clients")) if client["class"] == "com.rparker.hyprstretch-picker"), None))
        subprocess.run(["python3", str(project / "tools/toggle.py")], env=environment, check=True, timeout=8)
        wait(lambda: not any(client["class"] == "com.rparker.hyprstretch-picker" for client in json.loads(ctl("-j", "clients"))) and True)
        assert "manual sessions: 0" in ctl("hyprstretch")
        picker.wait(timeout=5)
        print("PASS selector opens once; Super+Shift+G and Super+G each close it")
    finally:
        if picker and picker.poll() is None:
            picker.terminate()
            picker.wait(timeout=5)
        if fixture and fixture.poll() is None:
            fixture.terminate()
            fixture.wait(timeout=5)
        compositor.terminate()
        try:
            compositor.wait(timeout=10)
        except subprocess.TimeoutExpired:
            compositor.kill()
            compositor.wait()
        log.close()
