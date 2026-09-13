"""Exercise the real CLI with an isolated config and no real destination."""

import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

exe = str(Path(sys.argv[1]).resolve())
fixture = str(Path(sys.argv[2]).resolve())
with tempfile.TemporaryDirectory(prefix="audiosync-cli-") as temp:
    root = Path(temp)
    env = dict(os.environ, APPDATA=temp, XDG_CONFIG_HOME=temp, HOME=temp)
    env.pop("DISPLAY", None)
    env.pop("WAYLAND_DISPLAY", None)
    if os.name != "nt":
        env["DBUS_SESSION_BUS_ADDRESS"] = "unix:path=/nonexistent-audiosync-test-bus"

    def run(*args, expected=0, input=None):
        p = subprocess.run(
            [exe, *args],
            input=input,
            capture_output=True,
            text=True,
            encoding="utf-8",
            env=env,
            timeout=20,
        )
        assert p.returncode == expected, (args, p.returncode, p.stdout, p.stderr)
        return p.stdout

    config = root / "audiosync" / "config.json"
    initial = json.loads(run("status", "--json"))
    assert not initial["watcher_running"] and not initial["sync_running"]
    assert initial["last_sync"] is None
    assert not config.parent.exists(), "read-only status created configuration files"
    assert "Usage:" in run()
    assert "watch" in run("--help")
    assert run("-V") == run("--version")
    assert run("--version").startswith("audiosync ")
    for command in ("devices", "sync", "watch", "status", "logs", "update"):
        assert run("help", command) == run(command, "-h")
    for command in ("add", "set", "list", "show", "rename", "remove"):
        assert run("help", "profile", command) == run("profile", command, "--help")
    assert "Usage:" in run("-qh")
    run("unknown", expected=2)
    run("devices", "-d", expected=2)
    run("watch", "--unknown", expected=2)
    run("update", "now", expected=2)

    class UpdateHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            body = b'{"tag_name":"v9.9.9"}'
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), UpdateHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    env["AUDIOSYNC_UPDATE_API"] = f"http://127.0.0.1:{server.server_port}"
    try:
        assert "9.9.9" in run("update", "--check")
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
        del env["AUDIOSYNC_UPDATE_API"]
    assert Path(run("config-path").strip()) == config
    run("devices")
    assert isinstance(json.loads(run("--json", "devices")), list)
    run("list")
    run("sync", expected=3)
    source = root / "Music source"
    source.mkdir()
    name = "Player \u65e5\u672c\u8a9e caf\u00e9"
    serial = 'offline-test-"serial'
    run("add", name, "--serial", serial, "--local", str(source))
    saved = json.loads(config.read_text(encoding="utf-8"))
    device = saved["devices"][0]
    assert device["name"] == name and device["serial"] == serial
    assert device["mode"] == "incremental" and device["format"] == "keep"
    assert device["source_type"] == "local"
    assert Path(device["local_path"]).is_absolute()
    assert name in run("list")
    before = config.read_bytes()
    for args in [
        ("add", name, "--serial", serial, "--local", str(source)),
        ("set", name, "--target", "../escape"),
        ("set", name, "--format", "invalid"),
        ("set", name, "--layout", "unknown"),
        ("set", name, "--serial", "x" * 128),
        ("set", name, "--local", str(source), "--url", "https://example.invalid"),
        ("set", name, "--adapter", "missing"),
        ("set", name, "--wat", "value"),
        ("remove", "missing"),
    ]:
        run(*args, expected=2)
        assert config.read_bytes() == before, args
    run(
        "set",
        name,
        "--target",
        "Audio/Albums",
        "--layout",
        "B",
        "--format",
        "mp3",
        "--mirror",
    )
    device = json.loads(config.read_text())["devices"][0]
    assert device["target_subdir"] == "Audio/Albums" and device["mode"] == "mirror"
    assert device["layout"] == "B" and device["format"] == "mp3"
    run("set", name, "--no-mirror")
    run("sync", name, expected=3)
    run("sync", "missing", expected=3)
    run("sync", "-p" + name, expected=3)
    run("sync", "--profile=" + name, expected=3)
    assert name in run("logs", "-n20")
    assert json.loads(run("profile", "show", name, "--json"))["name"] == name
    assert json.loads(run("profiles", "ls", "--json"))[0]["name"] == name
    assert "password" not in run("profile", "show", name, "--json")
    run("profile", "rename", name, "Renamed")
    name = "Player"
    run("profile", "rename", "Renamed", name)
    run("-q", "profile", "set", "--format=keep", name)
    assert Path(run("config", "path").strip()) == config
    assert not run("logs", "-n0")
    run("logs", "--lines=-1", expected=2)
    run("logs", "--lines=abc", expected=2)
    run("logs", "--path", "-f", expected=2)
    run("sync", "--json", expected=2)
    run("profile", "set", name, "--mirror=true", expected=2)
    run("profile", "set", name, "--format", expected=2)
    run("profile", "add", "--serial=other", "--local=" + str(source), "--", "-Literal")
    assert (
        json.loads(run("profile", "show", "--json", "--", "-Literal"))["name"]
        == "-Literal"
    )
    run("profile", "rm", "--", "-Literal")
    saved = json.loads(config.read_text())
    saved["devices"][0].update(
        source_type="remote",
        base_url="https://example.invalid",
        username="test",
        password_enc="unavailable-token",
    )
    config.write_text(json.dumps(saved), encoding="utf-8")
    run("set", name, "--target", "Music")
    migrated = json.loads(config.read_text())["devices"][0]
    assert migrated["source_type"] == "audiostream"
    assert migrated["password_enc"] == "unavailable-token"
    assert "unavailable-token" not in run("list")
    run("profile", "set", name, "--password", input="should-not-be-read", expected=1)
    run("profile", "set", name, "--password", "--password-stdin", expected=2)
    if os.name == "nt":
        run("set", name, "--password-stdin", input="test-secret-not-real\n")
        assert "test-secret-not-real" not in config.read_text()
        assert "test-secret-not-real" not in run("list")
        run("set", name, "--format", "flac")
    else:
        before = config.read_bytes()
        run("set", name, "--password-stdin", input="test-secret-not-real\n", expected=1)
        assert config.read_bytes() == before
    before = config.read_bytes()
    run(
        "add",
        "bad adapter",
        "--serial",
        serial,
        "--url",
        "https://example.invalid",
        "--username",
        "test",
        "--adapter",
        "missing",
        expected=2,
    )
    assert config.read_bytes() == before
    run(
        "add",
        "adapter",
        "--serial",
        serial,
        "--url",
        "https://example.invalid",
        "--username",
        "test",
        "--adapter",
        "audiostream",
    )
    assert json.loads(run("show", "adapter", "--json"))["source_type"] == "audiostream"
    run("remove", "adapter")
    run("remove", name)
    assert json.loads(config.read_text()) == {"devices": []}
    for bad in ("not json", "{}", '{"devices":[1]}', "[]"):
        config.write_text(bad, encoding="utf-8")
        run("add", "new", "--serial", serial, "--local", str(source), expected=1)
        run("sync", expected=1)
        assert config.read_text() == bad
    config.write_text('{"devices":[]}', encoding="utf-8")
    flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    p = subprocess.Popen(
        [exe, "watch", "--no-tray"],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        creationflags=flags,
    )
    try:
        time.sleep(1)
        assert p.poll() is None
        run("watch", "--no-tray", expected=1)
        assert json.loads(run("status", "--json"))["watcher_running"]
        run("list")
        run("add", "offline", "--serial", serial, "--local", str(source))
        run("remove", "offline")
        if os.name == "nt":
            from ctypes import wintypes

            user32 = ctypes.WinDLL("user32", use_last_error=True)
            callback_type = ctypes.WINFUNCTYPE(
                wintypes.BOOL, wintypes.HWND, wintypes.LPARAM
            )
            user32.PostMessageW.argtypes = [
                wintypes.HWND,
                wintypes.UINT,
                wintypes.WPARAM,
                wintypes.LPARAM,
            ]

            @callback_type
            def close_window(hwnd, unused):
                pid = wintypes.DWORD()
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
                if pid.value == p.pid:
                    user32.PostMessageW(hwnd, 0x0010, 0, 0)
                return True

            user32.EnumWindows(close_window, 0)
        else:
            p.terminate()
        out, err = p.communicate(timeout=10)
        assert p.returncode == 0, (out, err)
    finally:
        if p.poll() is None:
            p.kill()
            p.communicate()

    # Simulate an active transfer to verify status attachment, busy locks, and final results.
    transfer = subprocess.Popen(
        [fixture],
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    try:
        deadline = time.monotonic() + 10
        snapshot = None
        while time.monotonic() < deadline:
            snapshot = json.loads(run("status", "--json"))
            if (
                snapshot["sync_running"]
                and snapshot["last_sync"]
                and snapshot["last_sync"]["bytes"] == 512
            ):
                break
            time.sleep(0.05)
        else:
            raise AssertionError(("no live progress", snapshot, transfer.poll()))
        assert snapshot["last_sync"]["file"] == "Album/song.flac"
        assert (
            snapshot["last_sync"]["file_index"] == 1
            and snapshot["last_sync"]["file_total"] == 2
        )
        assert "50%" in run("status")
        status_file = config.parent / "status.json"
        before = status_file.read_bytes()
        run("sync", expected=1)
        assert status_file.read_bytes() == before, "busy sync overwrote live progress"
        follower = subprocess.Popen(
            [exe, "status", "-w", "--json"],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            encoding="utf-8",
        )
        try:
            # Wait for the status stream on a reader thread so a failure cannot hang the suite.
            import concurrent.futures

            pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
            line = pool.submit(follower.stdout.readline)
            assert json.loads(line.result(timeout=5))["sync_running"]
        finally:
            follower.terminate()
            follower.communicate(timeout=5)
            pool.shutdown(wait=True)
        out, err = transfer.communicate(input="\n", timeout=5)
        assert transfer.returncode == 0, (out, err)
        snapshot = json.loads(run("status", "--json"))
        assert not snapshot["sync_running"] and snapshot["last_sync"]["result"] == 0
        assert (
            snapshot["last_sync"]["new"] == 1 and snapshot["last_sync"]["skipped"] == 1
        )
        assert "fixture: transfer complete" in run("logs", "-n1")
    finally:
        if transfer.poll() is None:
            transfer.kill()
            transfer.communicate()
    # Follow an append and then a rotated log without losing the new file's first line.
    logfile = Path(run("logs", "--path").strip())
    logfile.write_text("first\nsecond\nthird\n", encoding="utf-8")
    assert run("logs", "--lines=2") == "second\nthird\n"
    follower = subprocess.Popen(
        [exe, "logs", "-fn0"],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
    )
    pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
    try:
        time.sleep(0.5)
        with logfile.open("a", encoding="utf-8") as f:
            f.write("appended\n")
        assert pool.submit(follower.stdout.readline).result(timeout=5) == "appended\n"
        logfile.replace(logfile.with_suffix(".rotated"))
        logfile.write_text("rotated\n", encoding="utf-8")
        assert pool.submit(follower.stdout.readline).result(timeout=5) == "rotated\n"
    finally:
        follower.terminate()
        follower.communicate(timeout=5)
        pool.shutdown(wait=True)
print(
    "PASS: CLI conventions, profiles, JSON, credentials, watcher, live progress, locks, and log following"
)
