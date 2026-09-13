"""Verify the exported tray menu in an isolated D-Bus/Xvfb session."""

import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
xml = """<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<method name="RegisterStatusNotifierHost"><arg type="s" direction="in"/></method>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
<signal name="StatusNotifierItemRegistered"><arg type="s"/></signal>
</interface></node>"""
registered = []


def method(connection, sender, path, iface, name, args, invocation):
    if name == "RegisterStatusNotifierItem":
        value = args.unpack()[0]
        registered.append(
            (sender, value) if value.startswith("/") else (value, "/StatusNotifierItem")
        )
    invocation.return_value(None)


def prop(connection, sender, path, iface, name):
    return {
        "RegisteredStatusNotifierItems": GLib.Variant("as", []),
        "IsStatusNotifierHostRegistered": GLib.Variant("b", True),
        "ProtocolVersion": GLib.Variant("i", 0),
    }[name]


info = Gio.DBusNodeInfo.new_for_xml(xml)
bus.register_object("/StatusNotifierWatcher", info.interfaces[0], method, prop, None)
owner = Gio.bus_own_name_on_connection(
    bus, "org.kde.StatusNotifierWatcher", Gio.BusNameOwnerFlags.NONE, None, None
)
context = GLib.MainContext.default()


def pump_until(predicate, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        while context.pending():
            context.iteration(False)
        if predicate():
            return
        time.sleep(0.02)
    raise AssertionError("Tray did not register in time")


pump_until(
    lambda: bus.call_sync(
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "NameHasOwner",
        GLib.Variant("(s)", ("org.kde.StatusNotifierWatcher",)),
        None,
        Gio.DBusCallFlags.NONE,
        1000,
        None,
    ).unpack()[0]
)
with tempfile.TemporaryDirectory(prefix="audiosync-tray-") as temp:
    env = dict(os.environ, XDG_CONFIG_HOME=temp, HOME=temp)
    p = subprocess.Popen(
        [str(Path(sys.argv[1]).resolve()), "watch"],
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        pump_until(lambda: bool(registered))
        service, path = registered[0]
        props = bus.call_sync(
            service,
            path,
            "org.freedesktop.DBus.Properties",
            "GetAll",
            GLib.Variant("(s)", ("org.kde.StatusNotifierItem",)),
            None,
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        ).unpack()[0]
        menu_path = props["Menu"]
        layout = bus.call_sync(
            service,
            menu_path,
            "com.canonical.dbusmenu",
            "GetLayout",
            GLib.Variant("(iias)", (0, -1, ["label"])),
            None,
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        ).unpack()
        labels = []
        ids = {}

        def walk(item):
            if isinstance(item, GLib.Variant):
                item = item.unpack()
            ident, properties, children = item
            if ident != 0 and "label" in properties:
                labels.append(properties["label"])
                ids[properties["label"]] = ident
            for child in children:
                walk(child)

        walk(layout[1])
        assert labels == ["Sync now", "Pause auto-sync", "Start at login", "Exit"], (
            labels
        )
        bus.call_sync(
            service,
            menu_path,
            "com.canonical.dbusmenu",
            "Event",
            GLib.Variant("(isvu)", (ids["Exit"], "clicked", GLib.Variant("i", 0), 0)),
            None,
            Gio.DBusCallFlags.NONE,
            2000,
            None,
        )
        out, err = p.communicate(timeout=10)
        assert p.returncode == 0, (out, err)
        print(
            "PASS: tray registered, exact menu has no Open GUI action, Exit shuts down cleanly"
        )
    finally:
        if p.poll() is None:
            p.terminate()
            p.communicate(timeout=10)
Gio.bus_unown_name(owner)
