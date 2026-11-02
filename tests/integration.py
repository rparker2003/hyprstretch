#!/usr/bin/env python3
"""Own test harness: all control commands are addressed to a newly spawned compositor."""

import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tempfile
import time

project = Path(__file__).resolve().parents[1]
checks = 0


def wait_for(description, predicate):
    global checks
    deadline = time.monotonic() + 7
    while time.monotonic() < deadline:
        if predicate():
            checks += 1
            print(f"PASS {description}", flush=True)
            return
        time.sleep(0.1)
    raise AssertionError(description)


def instances():
    return json.loads(subprocess.check_output(["hyprctl", "-j", "instances"], text=True))


with tempfile.TemporaryDirectory(prefix="stretch-integration-") as directory:
    root = Path(directory)
    configuration = root / "session.lua"
    configuration.write_text('''hl.monitor({output="WAYLAND-1", disabled=true})
hl.monitor({output="RIGHT", mode="1280x720@60", position="1280x0", scale=1})
hl.monitor({output="LEFT", mode="1280x720@60", position="-1280x0", scale=1})
hl.monitor({output="CENTER", mode="1280x720@60", position="0x0", scale=1})
hl.monitor({output="TV", mode="1920x1080@60", position="2560x0", scale=1})
hl.workspace_rule({workspace="1", monitor="CENTER", default=true, persistent=true})
hl.workspace_rule({workspace="2", monitor="LEFT", default=true, persistent=true})
hl.workspace_rule({workspace="3", monitor="RIGHT", default=true, persistent=true})
hl.workspace_rule({workspace="4", monitor="TV", default=true, persistent=true})
hl.config({animations={enabled=false}, xwayland={force_zero_scaling=false},
           ecosystem={no_update_news=true,no_donation_nag=true}, debug={disable_logs=false}})
''')
    parent_instances = {entry["instance"] for entry in instances()}
    nested_environment = dict(os.environ)
    nested_environment.pop("HYPRLAND_INSTANCE_SIGNATURE", None)
    nested_environment.pop("DISPLAY", None)
    log = (root / "compositor.log").open("w")
    compositor = subprocess.Popen(["Hyprland", "--config", str(configuration)], env=nested_environment,
                                  stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
    processes = []
    environment = None
    loaded = False
    plugin = root / "stretch.so"
    shutil.copy2(project / "build/hyprstretch.so", plugin)

    def ctl(*arguments, allow_error=False):
        if environment is None or environment["HYPRLAND_INSTANCE_SIGNATURE"] in parent_instances:
            raise RuntimeError("Refusing to control a pre-existing compositor")
        reply = subprocess.run(["hyprctl", *arguments], env=environment, capture_output=True, text=True)
        if reply.returncode and not allow_error:
            raise RuntimeError(f"{arguments}: {reply.stdout} {reply.stderr}")
        return reply.stdout.strip()

    def dispatch(expression):
        return ctl("dispatch", expression)

    def fixture(title):
        return next((window for window in json.loads(ctl("-j", "clients")) if window["title"] == title), {})

    def pixel(x, y):
        return subprocess.check_output(["grim", "-t", "ppm", "-g", f"{x},{y} 1x1", "-"], env=environment, stderr=subprocess.DEVNULL)[-3:]

    def blue_surface_visible(x):
        actual = pixel(x, 300)
        fixture_blue = (0x32, 0x75, 0xD7)
        return max(abs(actual[channel] - fixture_blue[channel]) for channel in range(3)) <= 40

    def surface_on_three_monitors():
        colors = [pixel(x, 300) for x in (-1000, 300, 1500)]
        # Software rendering can dither adjacent pixels by a few channel values.
        return all(max(color[channel] for color in colors) - min(color[channel] for color in colors) <= 4 for channel in range(3)) and colors[1][2] > colors[1][0] * 2

    try:
        candidates = []

        def discover():
            candidates[:] = [item for item in instances() if item["instance"] not in parent_instances and item["pid"] == compositor.pid]
            return bool(candidates)

        wait_for("isolated compositor starts", discover)
        instance = candidates[0]
        environment = dict(os.environ, HYPRLAND_INSTANCE_SIGNATURE=instance["instance"], WAYLAND_DISPLAY=instance["wl_socket"])
        environment.pop("DISPLAY", None)
        for output in ("RIGHT", "LEFT", "CENTER", "TV"):
            ctl("output", "create", "headless", output)
        wait_for("three monitors and a TV are ready", lambda: len(json.loads(ctl("-j", "monitors"))) == 4)
        response = ctl("plugin", "load", str(plugin))
        if response != "ok":
            raise RuntimeError(f"Plugin load failed: {response}")
        loaded = True
        wait_for("X11 coordinates follow desktop order", lambda: "output LEFT desktop -1280,0 X11 0,0" in ctl("hyprstretch"))
        for backend in ("wayland", "x11"):
            dispatch("hl.dsp.focus({workspace=1})")
            title = f"stretch-fixture-{backend}"
            if backend == "wayland":
                process = subprocess.Popen(["kitty", "-o", "linux_display_server=wayland", "-o", "background=#3275d7", "-o", "background_opacity=1.0", "--title", title],
                                           env=environment, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                processes.append(process)
            else:
                command = shlex.join([str(project / "build/window-fixture"), title])
                dispatch(f"hl.dsp.exec_cmd({json.dumps(command)})")
            wait_for(f"{backend} fixture maps", lambda: bool(fixture(title)))
            time.sleep(0.6)
            initial = fixture(title)
            assert not initial["floating"] and initial["xwayland"] == (backend == "x11")
            for workspace in (3, 2):
                dispatch(f"hl.dsp.focus({{workspace={workspace}}})")
            dispatch("hl.dsp.focus({workspace=1})")
            address = initial["address"]
            for selection in (("MISSING", "CENTER"), ("LEFT", "LEFT"), ("LEFT", "RIGHT"), ("LEFT", "TV")):
                response = ctl("hyprstretch", "stretch", f"window={address}", *selection, allow_error=True)
                assert response.startswith("error:"), response
            wait_for(f"{backend} invalid requests preserve geometry", lambda: fixture(title)["size"] == initial["size"])
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            wait_for(f"{backend} stretches without floating or changing client mode",
                     lambda: fixture(title)["at"] == [-1280, 0] and fixture(title)["size"] == [3840, 720]
                     and fixture(title)["fullscreenClient"] == initial["fullscreenClient"] and not fixture(title)["floating"])
            wait_for(f"{backend} surface appears on all selected monitors", surface_on_three_monitors)
            wait_for(f"{backend} TV is excluded", lambda: fixture(title)["size"] == [3840, 720] and "outputs=LEFT CENTER RIGHT" in ctl("hyprstretch"))
            dispatch("hl.dsp.cursor.move({x=-1000,y=300})")
            wait_for(f"{backend} pointer reaches the stretched application", lambda: json.loads(ctl("-j", "activewindow")).get("address") == address)
            dispatch("hl.dsp.focus({workspace=5})")
            wait_for(f"{backend} switching workspace on leased monitor releases that monitor",
                     lambda: not blue_surface_visible(-1000) and blue_surface_visible(300) and blue_surface_visible(1500))
            dispatch("hl.dsp.focus({workspace=1})")
            wait_for(f"{backend} refocusing stretched window reclaims released monitor", surface_on_three_monitors)
            dispatch("hl.dsp.focus({workspace=2})")
            wait_for(f"{backend} changing workspace keeps the full render size", lambda: fixture(title)["size"] == [3840, 720])
            assert ctl("hyprstretch", "restore", f"window={address}") == "ok"
            wait_for(f"{backend} explicit target restores even after focus changes", lambda: fixture(title)["size"] == initial["size"] and fixture(title)["fullscreen"] == initial["fullscreen"])
            wait_for(f"{backend} stretched surface clears from secondary monitors",
                     lambda: all(not blue_surface_visible(x) for x in (-1000, 1500)))
            dispatch("hl.dsp.focus({workspace=1})")
            assert ctl("hyprstretch", "stretch", f"window={address}", "CENTER") == "ok"
            wait_for(f"{backend} single-monitor selection fullscreens on that monitor",
                     lambda: fixture(title)["at"] == [0, 0] and fixture(title)["size"] == [1280, 720] and fixture(title)["fullscreen"] != initial["fullscreen"])
            assert ctl("hyprstretch", "restore", f"window={address}") == "ok"
            wait_for(f"{backend} restores from single-monitor fullscreen", lambda: fixture(title)["size"] == initial["size"])
            response = ctl("eval", f'local ok, err = hl.plugin.hyprstretch.stretch({{window="{address}", outputs={{"LEFT","CENTER"}}}}); assert(ok, err)')
            assert response == "ok", response
            wait_for(f"{backend} accepts a two-monitor selection", lambda: fixture(title)["size"] == [2560, 720])
            response = ctl("eval", 'local ok, err = hl.plugin.hyprstretch.stretch({outputs={}}); assert(not ok and type(err)=="string")')
            assert response == "ok", response
            wait_for(f"{backend} invalid Lua selection leaves the session unchanged", lambda: fixture(title)["size"] == [2560, 720])
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            wait_for(f"{backend} selection can change without rebuilding", lambda: fixture(title)["size"] == [3840, 720])
            assert ctl("reload") == "ok"
            response = ctl("eval", f'local ok, err = hl.plugin.hyprstretch.restore({{window="{address}"}}); assert(ok, err)')
            assert response == "ok", response
            wait_for(f"{backend} Lua controls survive a configuration reload", lambda: fixture(title)["size"] == initial["size"])
            dispatch('hl.dsp.window.float({action="set"})')
            wait_for(f"{backend} switches to floating state", lambda: fixture(title)["floating"])
            floating_size = fixture(title)["size"]
            floating_at = fixture(title)["at"]
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER") == "ok"
            wait_for(f"{backend} floating window stretches across selected monitors",
                     lambda: fixture(title)["at"] == [-1280, 0] and fixture(title)["size"] == [2560, 720])
            assert ctl("hyprstretch", "restore", f"window={address}") == "ok"
            wait_for(f"{backend} floating window restores its saved floating frame",
                     lambda: fixture(title)["floating"] and fixture(title)["size"] == floating_size and fixture(title)["at"] == floating_at)
            dispatch('hl.dsp.window.float({action="unset"})')
            wait_for(f"{backend} returns to tiled state", lambda: not fixture(title)["floating"] and fixture(title)["size"] == initial["size"])
            response = ctl("eval", f'local ok, err = hl.plugin.hyprstretch.stretch({{window="{address}", outputs={{"LEFT","CENTER","RIGHT"}}}}); assert(ok, err)')
            assert response == "ok", response
            ctl("output", "remove", "RIGHT")
            wait_for(f"{backend} disconnect restores the application", lambda: fixture(title)["fullscreen"] == initial["fullscreen"] and "manual sessions: 0" in ctl("hyprstretch"))
            ctl("output", "create", "headless", "RIGHT")
            wait_for("right monitor reconnects", lambda: len(json.loads(ctl("-j", "monitors"))) == 4)
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            ctl("output", "remove", "CENTER")
            wait_for(f"{backend} anchor monitor disconnect evacuates and restores application",
                     lambda: fixture(title)["fullscreen"] == initial["fullscreen"] and "manual sessions: 0" in ctl("hyprstretch"))
            ctl("output", "create", "headless", "CENTER")
            wait_for("center monitor reconnects", lambda: len(json.loads(ctl("-j", "monitors"))) == 4)
            dispatch("hl.dsp.focus({workspace=1})")
            assert ctl("eval", 'hl.monitor({output="LEFT", mode="2560x1440@144", position="-1280x0", scale=2})') == "ok"
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            wait_for(f"{backend} spans across mixed scale (2x HiDPI @ 144Hz + 1x @ 60Hz) with matching logical height",
                     lambda: fixture(title)["at"] == [-1280, 0] and fixture(title)["size"] == [3840, 720])
            assert ctl("hyprstretch", "restore", f"window={address}") == "ok"
            assert ctl("eval", 'hl.monitor({output="LEFT", mode="1280x720@60", position="-1280x0", scale=1})') == "ok"
            wait_for(f"{backend} restores after mixed-scale span", lambda: fixture(title)["size"] == initial["size"])
            assert ctl("hyprstretch", "toggle", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            wait_for(f"{backend} toggle starts a new session", lambda: fixture(title)["size"] == [3840, 720])
            assert ctl("hyprstretch", "toggle", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            wait_for(f"{backend} toggle restores", lambda: fixture(title)["size"] == initial["size"])
            assert ctl("hyprstretch", "stretch", f"window={address}") == "ok"
            wait_for(f"{backend} default selection excludes TV", lambda: fixture(title)["size"] == [3840, 720])
            assert ctl("eval", 'hl.config({plugin={hyprstretch={outputs="CENTER RIGHT"}}})') == "ok"
            assert ctl("hyprstretch", "stretch", f"window={address}") == "ok"
            wait_for(f"{backend} plugin:hyprstretch:outputs configures default span",
                     lambda: fixture(title)["at"] == [0, 0] and fixture(title)["size"] == [2560, 720])
            assert ctl("eval", 'hl.config({plugin={hyprstretch={outputs=""}}})') == "ok"
            ctl("plugin", "unload", str(plugin))
            loaded = False
            wait_for(f"{backend} unload restores geometry", lambda: fixture(title)["size"] == initial["size"])
            plugin = root / f"stretch-after-{backend}.so"
            shutil.copy2(project / "build/hyprstretch.so", plugin)
            ctl("plugin", "load", str(plugin))
            loaded = True
            assert ctl("hyprstretch", "stretch", f"window={address}", "LEFT", "CENTER", "RIGHT") == "ok"
            process_id = fixture(title)["pid"]
            executable = Path(f"/proc/{process_id}/exe").resolve()
            expected = Path(shutil.which("kitty")).resolve() if backend == "wayland" else (project / "build/window-fixture").resolve()
            if executable != expected:
                raise RuntimeError("Refusing to terminate an unexpected test process")
            os.kill(process_id, signal.SIGTERM)
            wait_for(f"{backend} closing the app clears state", lambda: not fixture(title) and "manual sessions: 0" in ctl("hyprstretch"))
        print(f"PASS {checks} integration checks", flush=True)
    except Exception:
        if environment is not None:
            print(ctl("hyprstretch", allow_error=True), flush=True)
            print(ctl("-j", "clients", allow_error=True), flush=True)
            print("sample pixels:", [pixel(x, 300).hex() for x in (-1000, 300, 1500)], flush=True)
            capture = project / "build/failed-integration"
            capture.mkdir(exist_ok=True)
            for output in ("LEFT", "CENTER", "RIGHT"):
                subprocess.run(["grim", "-o", output, str(capture / f"{output}.png")], env=environment, capture_output=True)
        raise
    finally:
        if loaded:
            ctl("plugin", "unload", str(plugin), allow_error=True)
        for process in processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=5)
        compositor.terminate()
        try:
            compositor.wait(timeout=10)
        except subprocess.TimeoutExpired:
            compositor.kill()
            compositor.wait()
        log.close()
