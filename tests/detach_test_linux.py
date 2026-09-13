"""Verify that an attached watcher survives pressing d."""

import json
import os
import pty
import select
import signal
import subprocess
import sys
import tempfile
import time

exe = os.path.abspath(sys.argv[1])


def children(parent):
    found = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open(f"/proc/{name}/status", encoding="utf-8") as status:
                fields = dict(line.split(":", 1) for line in status if ":" in line)
            if int(fields["PPid"]) == parent:
                found.append(int(name))
        except (FileNotFoundError, KeyError, ValueError):
            pass
    return found


with tempfile.TemporaryDirectory(prefix="audiosync-detach-") as temp:
    env = os.environ | {"XDG_CONFIG_HOME": temp}
    log = subprocess.check_output([exe, "logs", "--path"], env=env, text=True).strip()
    subprocess.check_call([exe, "sync", "-d"], env=env)
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if os.path.exists(log) and "no configured devices" in open(log, encoding="utf-8").read():
            break
        time.sleep(0.05)
    else:
        raise AssertionError("sync -d did not finish in the background")

    parent, master = pty.fork()
    if parent == 0:
        os.execve(exe, [exe, "watch", "--no-tray"], env)

    child = None
    parent_waited = False
    try:
        output = b""
        deadline = time.monotonic() + 10
        while b"Press d to detach." not in output and time.monotonic() < deadline:
            readable, _, _ = select.select([master], [], [], 0.2)
            if readable:
                output += os.read(master, 4096)
        assert b"Press d to detach." in output, output

        child_pids = children(parent)
        assert len(child_pids) == 1, child_pids
        child = child_pids[0]
        os.write(master, b"d")
        _, status = os.waitpid(parent, 0)
        parent_waited = True
        assert os.waitstatus_to_exitcode(status) == 0
        os.kill(child, 0)

        state = json.loads(subprocess.check_output([exe, "status", "--json"], env=env))
        assert state["watcher_running"]
    finally:
        os.close(master)
        pids = ([] if parent_waited else [parent]) + ([] if child is None else [child])
        for pid in pids:
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                pass

    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        state = json.loads(subprocess.check_output([exe, "status", "--json"], env=env))
        if not state["watcher_running"]:
            break
        time.sleep(0.1)
    else:
        raise AssertionError("detached watcher did not stop")

print("PASS: pressing d detaches the watcher")
