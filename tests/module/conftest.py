"""Builds roomcore as a host shared library and wraps it for the module tests."""

import ctypes
import os
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "firmware" / "node" / "components" / "roomcore"
BUILD = Path(__file__).resolve().parent / "build"

TICK_MS = 200  # control_task period on the node
MIN_SWITCH_MS = 5 * 60 * 1000


def _build_library() -> Path:
    cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
    if not cc:
        pytest.exit("No C compiler found; set CC", returncode=2)
    BUILD.mkdir(exist_ok=True)
    out = BUILD / ("roomcore.dll" if sys.platform == "win32" else "libroomcore.so")
    cmd = [
        cc, "-shared", "-fPIC", "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-static-libgcc", f"-I{SRC / 'include'}", "-o", str(out),
        *map(str, sorted(SRC.glob("*.c"))), "-lm",
    ]
    subprocess.run(cmd, check=True)
    return out


class Inputs(ctypes.Structure):
    _fields_ = [
        ("t_tmp117", ctypes.c_float),
        ("tmp117_valid", ctypes.c_bool),
        ("t_shtc3", ctypes.c_float),
        ("shtc3_valid", ctypes.c_bool),
        ("pot_mv", ctypes.c_int),
    ]


class Points(ctypes.Structure):
    _fields_ = [
        ("t_ref", ctypes.c_float),
        ("t_ref_valid", ctypes.c_bool),
        ("t_aux", ctypes.c_float),
        ("t_aux_valid", ctypes.c_bool),
        ("rh", ctypes.c_float),
        ("setpoint", ctypes.c_float),
        ("heating", ctypes.c_bool),
        ("priority", ctypes.c_int),
        ("mode", ctypes.c_int),
        ("status", ctypes.c_uint16),
        ("uptime_s", ctypes.c_uint32),
    ]


def points(**kw):
    values = dict(t_ref=21.84, t_ref_valid=True, t_aux=22.1, t_aux_valid=True, rh=47.2,
                  setpoint=22.0, heating=True, priority=16, mode=0, status=0, uptime_s=86400)
    values.update(kw)
    return Points(**values)


def mv_for(celsius: float) -> int:
    """Potentiometer voltage that maps to the given setpoint."""
    return round(100 + (celsius - 16.0) / 10.0 * 2900)


class RoomCore:
    """One controller instance with its own simulated clock and inputs."""

    def __init__(self, lib, min_switch_ms=MIN_SWITCH_MS, simulation=False, start_ms=0):
        self.lib = lib
        self.buf = ctypes.create_string_buffer(lib.rc_size())
        self.now = start_ms
        self.inputs = Inputs(21.0, True, 21.0, True, mv_for(21.0))
        lib.rc_init(self.buf, min_switch_ms, simulation, ctypes.c_uint32(start_ms))

    def set(self, t=None, t_aux=None, tmp_ok=None, sht_ok=None, setpoint=None, pot_mv=None):
        """Changes inputs; t also sets the SHTC3 value unless t_aux is given."""
        if pot_mv is not None:
            self.inputs.pot_mv = pot_mv
        if t is not None:
            self.inputs.t_tmp117 = t
            self.inputs.t_shtc3 = t if t_aux is None else t_aux
        elif t_aux is not None:
            self.inputs.t_shtc3 = t_aux
        if tmp_ok is not None:
            self.inputs.tmp117_valid = tmp_ok
        if sht_ok is not None:
            self.inputs.shtc3_valid = sht_ok
        if setpoint is not None:
            self.inputs.pot_mv = mv_for(setpoint)
        return self

    def run(self, seconds, tick_ms=TICK_MS):
        for _ in range(max(1, round(seconds * 1000 / tick_ms))):
            self.now = (self.now + tick_ms) & 0xFFFFFFFF
            self.lib.rc_step(self.buf, ctypes.byref(self.inputs), ctypes.c_uint32(self.now))
        return self

    def write_setpoint(self, celsius):
        return self.lib.rc_write_setpoint(self.buf, ctypes.c_float(celsius))

    def relinquish_setpoint(self):
        self.lib.rc_relinquish_setpoint(self.buf)

    def set_mode(self, mode):
        return self.lib.rc_set_mode(self.buf, mode)

    heating = property(lambda self: self.lib.rc_heating(self.buf))
    priority = property(lambda self: self.lib.rc_heating_priority(self.buf))
    setpoint = property(lambda self: round(self.lib.rc_setpoint(self.buf), 2))
    t_ref = property(lambda self: self.lib.rc_t_ref(self.buf))
    t_ref_valid = property(lambda self: self.lib.rc_t_ref_valid(self.buf))
    status = property(lambda self: self.lib.rc_status(self.buf))


@pytest.fixture(scope="session")
def lib():
    lib = ctypes.CDLL(str(_build_library()))
    lib.rc_size.restype = ctypes.c_size_t
    lib.rc_init.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_bool, ctypes.c_uint32]
    lib.rc_step.argtypes = [ctypes.c_void_p, ctypes.POINTER(Inputs), ctypes.c_uint32]
    lib.rc_write_setpoint.argtypes = [ctypes.c_void_p, ctypes.c_float]
    lib.rc_set_mode.argtypes = [ctypes.c_void_p, ctypes.c_int]
    for name in ("rc_heating", "rc_t_ref_valid"):
        getattr(lib, name).restype = ctypes.c_bool
    for name in ("rc_setpoint", "rc_t_ref", "rc_pot_setpoint"):
        getattr(lib, name).restype = ctypes.c_float
    lib.rc_status.restype = ctypes.c_uint16
    lib.rc_pot_to_celsius.argtypes = [ctypes.c_float]
    lib.rc_pot_to_celsius.restype = ctypes.c_float
    return lib


@pytest.fixture
def make(lib):
    """Factory: make(min_switch_ms=..., simulation=..., start_ms=...)."""
    return lambda **kw: RoomCore(lib, **kw)
