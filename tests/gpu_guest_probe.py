"""Run inside the VM: plb-gpu-run python3 gpu_guest_probe.py.
Checks actual GPU command execution/readback, not only an advertised renderer.
"""

import ctypes as c
import os

os.environ["EGL_PLATFORM"] = "surfaceless"
e = c.CDLL("libEGL.so.1")
g = c.CDLL("libGLESv2.so.2")
P, I = c.c_void_p, c.c_int


def bind(lib, name, result, *args):
    method = getattr(lib, name)
    method.restype = result
    method.argtypes = args
    return method


get_display = bind(e, "eglGetDisplay", P, P)
initialize = bind(e, "eglInitialize", I, P, c.POINTER(I), c.POINTER(I))
choose = bind(e, "eglChooseConfig", I, P, c.POINTER(I), c.POINTER(P), I, c.POINTER(I))
bind_api = bind(e, "eglBindAPI", I, c.c_uint)
create_surface = bind(e, "eglCreatePbufferSurface", P, P, P, c.POINTER(I))
create_context = bind(e, "eglCreateContext", P, P, P, P, c.POINTER(I))
make_current = bind(e, "eglMakeCurrent", I, P, P, P, P)
get_string = bind(g, "glGetString", c.c_char_p, c.c_uint)
clear_color = bind(g, "glClearColor", None, c.c_float, c.c_float, c.c_float, c.c_float)
clear = bind(g, "glClear", None, c.c_uint)
read_pixels = bind(g, "glReadPixels", None, I, I, I, I, c.c_uint, c.c_uint, P)
finish = bind(g, "glFinish", None)
get_error = bind(g, "glGetError", c.c_uint)
destroy_context = bind(e, "eglDestroyContext", I, P, P)
destroy_surface = bind(e, "eglDestroySurface", I, P, P)
terminate = bind(e, "eglTerminate", I, P)

display = get_display(None)
major, minor = I(), I()
assert initialize(display, c.byref(major), c.byref(minor)), "EGL initialization failed"
attributes = (I * 11)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3038)
config, count = P(), I()
assert choose(display, attributes, c.byref(config), 1, c.byref(count)) and count.value
assert bind_api(0x30A0)
surface = create_surface(display, config, (I * 5)(0x3057, 4, 0x3056, 4, 0x3038))
context = create_context(display, config, None, (I * 3)(0x3098, 2, 0x3038))
assert surface and context and make_current(display, surface, surface, context)
renderer = get_string(0x1F01).decode()
print("RENDERER:", renderer, flush=True)
assert "virgl" in renderer, "Hardware forwarding was not selected"
clear_color(0.25, 0.5, 0.75, 1.0)
clear(0x4000)
finish()
pixels = (c.c_ubyte * 4)()
read_pixels(0, 0, 1, 1, 0x1908, 0x1401, pixels)
assert get_error() == 0
actual = list(pixels)
assert all(abs(a - b) <= 1 for a, b in zip(actual, (64, 128, 191, 255))), actual
print("GPU_READBACK_PASS:", actual, flush=True)
assert make_current(display, None, None, None)
destroy_context(display, context)
destroy_surface(display, surface)
terminate(display)
