#!/usr/bin/env python3
"""Close the monitor picker, or toggle the horizontal monitor span."""

import json
import os
import signal
import subprocess
import sys

PICKER_CLASS = "com.rparker.hyprstretch-picker"


def run(*arguments):
    return subprocess.run(["hyprctl", *arguments], check=False, text=True, capture_output=True)


clients = json.loads(run("-j", "clients").stdout or "[]")
picker = next((window for window in clients if window.get("class") == PICKER_CLASS), None)
if picker and picker.get("pid", -1) > 0:
    os.kill(picker["pid"], signal.SIGTERM)
else:
    result = run("hyprstretch", "toggle", *sys.argv[1:])
    if result.returncode:
        raise SystemExit(result.stderr or result.stdout)
