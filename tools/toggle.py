#!/usr/bin/env python3
"""Close the monitor picker, or toggle the usual three-monitor span."""

import json
import subprocess
import sys


def run(*arguments):
    return subprocess.run(["hyprctl", *arguments], check=False, text=True, capture_output=True)


window = json.loads(run("-j", "activewindow").stdout or "{}")
if window.get("class") == "com.rparker.hyprstretch-picker":
    result = run("eval", "hl.dispatch(hl.dsp.window.close())")
else:
    outputs = sys.argv[1:] or ["DP-2", "DP-3", "DP-1"]
    result = run("hyprstretch", "toggle", *outputs)

if result.returncode:
    raise SystemExit(result.stderr or result.stdout)
