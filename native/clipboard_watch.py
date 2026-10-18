#!/usr/bin/python3
"""MIT. Event-driven X11/Xwayland selection changes, no clipboard timer polling."""

import ctypes as C
import os, select, struct, subprocess, sys, time, fcntl

LIMIT = 65536
track_only = "--track-only" in sys.argv
if track_only:
    lock = os.open(
        "/run/plb/clipboard-tracker.lock",
        os.O_WRONLY | os.O_CREAT | os.O_CLOEXEC | os.O_NOFOLLOW,
        0o600,
    )
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        raise SystemExit(0)
x = C.CDLL("libX11.so.6")
fix = C.CDLL("libXfixes.so.3")
libc = C.CDLL(None)
x.XOpenDisplay.argtypes = [C.c_char_p]
x.XOpenDisplay.restype = C.c_void_p
x.XDefaultRootWindow.argtypes = [C.c_void_p]
x.XDefaultRootWindow.restype = C.c_ulong
x.XInternAtom.argtypes = [C.c_void_p, C.c_char_p, C.c_int]
x.XInternAtom.restype = C.c_ulong
x.XFlush.argtypes = [C.c_void_p]
x.XNextEvent.argtypes = [C.c_void_p, C.c_void_p]
x.XConnectionNumber.argtypes = [C.c_void_p]
x.XConnectionNumber.restype = C.c_int
x.XPending.argtypes = [C.c_void_p]
x.XPending.restype = C.c_int
fix.XFixesQueryExtension.argtypes = [C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int)]
fix.XFixesSelectSelectionInput.argtypes = [C.c_void_p, C.c_ulong, C.c_ulong, C.c_ulong]
libc.inotify_init1.argtypes = [C.c_int]
libc.inotify_init1.restype = C.c_int
libc.inotify_add_watch.argtypes = [C.c_int, C.c_char_p, C.c_uint32]
notify = libc.inotify_init1(os.O_NONBLOCK | os.O_CLOEXEC)
if notify < 0 or libc.inotify_add_watch(notify, b"/tmp/.X11-unix", 0x100 | 0x80) < 0:
    raise SystemExit(2)
connections = {}
last = None


def attach():
    # These are the only two X servers created by the Bridge GUI.
    for name in (":1", ":0"):
        if name in connections or not os.path.exists("/tmp/.X11-unix/X" + name[1:]):
            continue
        d = x.XOpenDisplay(name.encode())
        if not d:
            continue
        base = C.c_int()
        error = C.c_int()
        if not fix.XFixesQueryExtension(d, C.byref(base), C.byref(error)):
            continue
        fix.XFixesSelectSelectionInput(
            d, x.XDefaultRootWindow(d), x.XInternAtom(d, b"CLIPBOARD", 0), 7
        )
        x.XFlush(d)
        connections[name] = (d, base.value, x.XConnectionNumber(d))


def selection(name):
    global last
    if track_only:
        # Metadata only. Manual mode never reads clipboard contents in background.
        pending = "/run/plb/clipboard-source.pending"
        with open(pending, "w") as f:
            f.write(name)
        os.replace(pending, "/run/plb/clipboard-source")
        return
    env = dict(os.environ, DISPLAY=name)
    p = subprocess.Popen(
        ["xclip", "-selection", "clipboard", "-out"],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        env=env,
    )
    deadline = time.monotonic() + 3
    data = bytearray()
    ok = True
    try:
        while True:
            wait = deadline - time.monotonic()
            if wait <= 0 or not select.select([p.stdout], [], [], wait)[0]:
                ok = False
                break
            part = os.read(p.stdout.fileno(), min(4096, LIMIT + 1 - len(data)))
            if not part:
                break
            data.extend(part)
            if len(data) > LIMIT:
                ok = False
                break
        if p.poll() is None:
            try:
                p.wait(timeout=max(0.01, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                ok = False
        if ok and p.returncode == 0:
            value = bytes(data)
            value.decode("utf-8", "strict")
            if value != last:
                sys.stdout.buffer.write(struct.pack("!I", len(value)) + value)
                sys.stdout.buffer.flush()
                last = value
    except UnicodeError:
        pass
    except BrokenPipeError:
        raise SystemExit(0)
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        p.stdout.close()


attach()
event = (C.c_long * 24)()
while True:
    handled = False
    for name, (display, base, _) in list(connections.items()):
        while x.XPending(display):
            x.XNextEvent(display, C.byref(event))
            handled = True
            if C.cast(C.byref(event), C.POINTER(C.c_int))[0] == base:
                selection(name)
    if handled:
        continue
    ready, _, _ = select.select([notify] + [item[2] for item in connections.values()], [], [])
    if notify in ready:
        try:
            os.read(notify, 8192)
        except BlockingIOError:
            pass
        attach()
