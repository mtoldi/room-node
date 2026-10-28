"""Module tests for the MQTT encodings in roomcore/points.c. IDs match docs/test-plan.md."""

import ctypes
import json

import pytest

AUTO, OFF, ON = 0, 1, 2
INVALID, VALUE, RELINQUISH = 0, 1, 2


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
                  setpoint=22.0, heating=True, priority=16, mode=AUTO, status=0, uptime_s=86400)
    values.update(kw)
    return Points(**values)


@pytest.fixture
def to_json(lib):
    lib.rc_points_to_json.argtypes = [ctypes.POINTER(Points), ctypes.c_char_p, ctypes.c_size_t]

    def encode(p, size=256):
        buf = ctypes.create_string_buffer(size)
        n = lib.rc_points_to_json(ctypes.byref(p), buf, size)
        return n, buf.value.decode()

    return encode


def test_mt15_state_json(to_json):
    n, text = to_json(points())
    assert n == len(text)
    assert json.loads(text) == {
        "t_ref": 21.84, "t_aux": 22.1, "rh": 47.2, "setpoint": 22.0, "heating": True,
        "priority": 16, "mode": "auto", "status": 0, "uptime": 86400,
    }

    msg = json.loads(to_json(points(t_ref_valid=False, t_aux_valid=False, mode=ON, heating=False))[1])
    assert msg["t_ref"] is None and msg["t_aux"] is None and msg["rh"] is None
    assert msg["mode"] == "on" and msg["heating"] is False

    msg = json.loads(to_json(points(t_ref=-5.5, uptime_s=0xFFFFFFFF))[1])
    assert msg["t_ref"] == -5.5 and msg["uptime"] == 4294967295

    n, _ = to_json(points(), size=40)
    assert n == -1, "a too small buffer is reported, never overrun"


def test_mt15_change_detection(lib):
    lib.rc_points_changed.argtypes = [ctypes.POINTER(Points), ctypes.POINTER(Points)]
    lib.rc_points_changed.restype = ctypes.c_bool
    changed = lambda a, b: lib.rc_points_changed(ctypes.byref(a), ctypes.byref(b))

    base = points()
    assert not changed(base, points(t_ref=22.3, rh=50.0, uptime_s=5)), "drift alone waits"
    for kw in (dict(heating=False), dict(priority=8), dict(mode=OFF), dict(status=1), dict(setpoint=22.1)):
        assert changed(base, points(**kw)), kw


@pytest.mark.parametrize("payload, expected, value", [
    (b"21.5", VALUE, 21.5),
    (b" 22 \n", VALUE, 22.0),
    (b"null", RELINQUISH, None),
    (b"50", VALUE, 50.0),  # parsed; the range check belongs to rc_write_setpoint
    (b"", INVALID, None),
    (b"abc", INVALID, None),
    (b"21.5C", INVALID, None),
    (b"nan", INVALID, None),
    (b"inf", INVALID, None),
    (b"1" * 40, INVALID, None),
])
def test_mt16_setpoint_command(lib, payload, expected, value):
    lib.rc_parse_setpoint.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_float)]
    out = ctypes.c_float(-1)
    assert lib.rc_parse_setpoint(payload, len(payload), ctypes.byref(out)) == expected
    if value is not None:
        assert out.value == pytest.approx(value)


@pytest.mark.parametrize("payload, expected, mode", [
    (b"auto", VALUE, AUTO),
    (b"off", VALUE, OFF),
    (b" on\r\n", VALUE, ON),
    (b"ON", INVALID, None),
    (b"heat", INVALID, None),
    (b"", INVALID, None),
])
def test_mt16_mode_command(lib, payload, expected, mode):
    lib.rc_parse_mode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_int)]
    out = ctypes.c_int(-1)
    assert lib.rc_parse_mode(payload, len(payload), ctypes.byref(out)) == expected
    if mode is not None:
        assert out.value == mode


def test_mt16_payload_not_terminated(lib):
    """MQTT payloads are not NUL-terminated: only len bytes may be read."""
    lib.rc_parse_mode.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_int)]
    out = ctypes.c_int(-1)
    assert lib.rc_parse_mode(b"offline", 3, ctypes.byref(out)) == VALUE
    assert out.value == OFF
