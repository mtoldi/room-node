"""Module tests for the Modbus TCP protocol layer (roomcore/modbus.c).
Requests and expected responses are written out byte by byte. IDs match
docs/test-plan.md."""

import ctypes
import struct

import pytest

from conftest import Points, points

OVERRIDE = 1 << 6

WRITE_SETPOINT = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_float)
RELINQUISH = ctypes.CFUNCTYPE(None, ctypes.c_void_p)
SET_MODE = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_int)


class Map(ctypes.Structure):
    _fields_ = [
        ("points", ctypes.POINTER(Points)),
        ("write_setpoint", WRITE_SETPOINT),
        ("relinquish_setpoint", RELINQUISH),
        ("set_mode", SET_MODE),
        ("ctx", ctypes.c_void_p),
    ]


class Server:
    """roomcore's Modbus layer with a fake controller that records writes."""

    def __init__(self, lib, **point_values):
        self.lib = lib
        self.calls = []
        self.points = points(**point_values)
        # Keep references to the callbacks so they are not garbage collected
        self._cb = (
            WRITE_SETPOINT(lambda ctx, c: self.calls.append(("setpoint", round(c, 2))) or 0),
            RELINQUISH(lambda ctx: self.calls.append(("relinquish",))),
            SET_MODE(lambda ctx, m: self.calls.append(("mode", m)) or 0),
        )
        self.map = Map(ctypes.pointer(self.points), *self._cb, None)

    def request(self, pdu: bytes, unit=1, tid=0x1234) -> bytes:
        """Sends one PDU in an MBAP frame and returns the response PDU."""
        frame = struct.pack(">HHHB", tid, 0, len(pdu) + 1, unit) + pdu
        resp = self.raw(frame)
        rtid, proto, length, runit = struct.unpack(">HHHB", resp[:7])
        assert (rtid, proto, runit) == (tid, 0, unit)
        assert length == len(resp) - 6
        return resp[7:]

    def raw(self, frame: bytes) -> bytes:
        out = ctypes.create_string_buffer(260)
        n = self.lib.mb_tcp_handle(ctypes.byref(self.map), frame, len(frame), out, len(out))
        assert n >= 0, f"frame rejected ({n})"
        return out.raw[:n]


def read(fc, addr, qty):
    return struct.pack(">BHH", fc, addr, qty)


def regs(*values):
    return b"".join(struct.pack(">H", v & 0xFFFF) for v in values)


@pytest.fixture
def server(lib):
    lib.mb_tcp_handle.argtypes = [ctypes.POINTER(Map), ctypes.c_char_p, ctypes.c_size_t,
                                  ctypes.c_char_p, ctypes.c_size_t]
    lib.mb_tcp_frame_length.argtypes = [ctypes.c_char_p]
    return lambda **kw: Server(lib, **kw)


# --- MT-09: valid requests -------------------------------------------------

def test_mt09_read_input_registers(server):
    s = server()
    assert s.request(read(0x04, 0, 9)) == bytes([0x04, 18]) + regs(
        2184,          # 0 t_ref 21.84
        2210,          # 1 t_aux 22.10
        4720,          # 2 rh 47.2
        2200,          # 3 setpoint 22.0
        1,             # 4 heating
        0,             # 5 status
        16,            # 6 priority
        0x0001,        # 7 uptime 86400 = 0x00015180, high word first
        0x5180,        # 8 uptime low word
    )
    assert s.request(read(0x04, 3, 1)) == bytes([0x04, 2]) + regs(2200)


def test_mt09_read_holding_registers(server):
    assert server().request(read(0x03, 100, 2)) == bytes([0x03, 4]) + regs(0x8000, 0)
    s = server(setpoint=23.5, status=OVERRIDE, mode=2)
    assert s.request(read(0x03, 100, 2)) == bytes([0x03, 4]) + regs(2350, 2)


def test_mt09_write_single_register(server):
    s = server()
    pdu = struct.pack(">BHH", 0x06, 100, 2350)
    assert s.request(pdu) == pdu, "FC06 echoes the request"
    assert s.request(struct.pack(">BHH", 0x06, 100, 0x8000)) == struct.pack(">BHH", 0x06, 100, 0x8000)
    assert s.request(struct.pack(">BHH", 0x06, 101, 1))[0] == 0x06
    assert s.calls == [("setpoint", 23.5), ("relinquish",), ("mode", 1)]


def test_mt09_write_multiple_registers(server):
    s = server()
    pdu = struct.pack(">BHHB", 0x10, 100, 2, 4) + regs(2300, 2)
    assert s.request(pdu) == struct.pack(">BHH", 0x10, 100, 2)
    assert s.calls == [("setpoint", 23.0), ("mode", 2)]


def test_mt09_framing(server, lib):
    s = server()
    assert s.request(read(0x04, 4, 1), unit=255, tid=0xBEEF) == bytes([0x04, 2]) + regs(1)
    assert lib.mb_tcp_frame_length(struct.pack(">HHHB", 1, 0, 6, 1)) == 12
    assert lib.mb_tcp_frame_length(struct.pack(">HHHB", 1, 1, 6, 1)) == -1, "protocol id must be 0"
    assert lib.mb_tcp_frame_length(struct.pack(">HHHB", 1, 0, 1, 1)) == -1, "no PDU"
    assert lib.mb_tcp_frame_length(struct.pack(">HHHB", 1, 0, 300, 1)) == -1, "too long"


# --- MT-10: exceptions -----------------------------------------------------

@pytest.mark.parametrize("pdu, code", [
    (bytes([0x05, 0, 1, 0xFF, 0]), 0x01),            # write coil: not supported
    (bytes([0x2B, 0x0E, 1, 0]), 0x01),               # device identification: not supported
    (read(0x04, 9, 1), 0x02),                        # past the last input register
    (read(0x04, 8, 2), 0x02),                        # range runs past the end
    (read(0x03, 99, 1), 0x02),                       # before the first holding register
    (read(0x03, 101, 2), 0x02),
    (read(0x04, 0, 0), 0x03),                        # quantity 0
    (read(0x04, 0, 126), 0x03),                      # quantity above the spec limit
    (bytes([0x04, 0, 0]), 0x03),                     # truncated request
    (struct.pack(">BHH", 0x06, 102, 1), 0x02),
    (struct.pack(">BHH", 0x06, 0, 1), 0x02),         # input registers are read-only
    (struct.pack(">BHH", 0x06, 100, 2700), 0x03),    # 27.00 °C
    (struct.pack(">BHH", 0x06, 100, 1599), 0x03),    # 15.99 °C
    (struct.pack(">BHH", 0x06, 100, 0xFFFF), 0x03),  # -0.01 °C
    (struct.pack(">BHH", 0x06, 101, 3), 0x03),       # no mode 3
    (struct.pack(">BHHB", 0x10, 100, 2, 2) + regs(2300, 1), 0x03),  # byte count mismatch
    (struct.pack(">BHHB", 0x10, 100, 0, 0), 0x03),
    (struct.pack(">BHHB", 0x10, 101, 2, 4) + regs(1, 1), 0x02),
])
def test_mt10_exceptions(server, pdu, code):
    s = server()
    assert s.request(pdu) == bytes([pdu[0] | 0x80, code])
    assert s.calls == [], "a rejected request changes nothing"


def test_mt10_write_multiple_is_all_or_nothing(server):
    s = server()
    pdu = struct.pack(">BHHB", 0x10, 100, 2, 4) + regs(2300, 5)  # valid setpoint, invalid mode
    assert s.request(pdu) == bytes([0x90, 0x03])
    assert s.calls == [], "the valid setpoint must not be applied either"


def test_mt10_other_unit_ids(server):
    s = server()
    assert s.request(read(0x04, 0, 1), unit=7) == bytes([0x84, 0x0B])


def test_mt10_malformed_frames(server, lib):
    s = server()
    out = ctypes.create_string_buffer(260)
    frame = struct.pack(">HHHB", 1, 0, 6, 1) + read(0x04, 0, 1)
    handle = lambda f: lib.mb_tcp_handle(ctypes.byref(s.map), f, len(f), out, len(out))
    assert handle(frame) > 0
    assert handle(frame[:-1]) == -1, "length field and frame size disagree"
    assert handle(frame + b"\x00") == -1


# --- MT-11: scaling --------------------------------------------------------

@pytest.mark.parametrize("values, register, expected", [
    (dict(t_ref=-5.5), 0, 0xFDDA),                 # -550 as two's complement
    (dict(t_ref=21.847), 0, 2185),                 # rounded, not truncated
    (dict(t_ref_valid=False), 0, 0x8000),
    (dict(t_aux_valid=False), 1, 0x8000),
    (dict(t_aux_valid=False), 2, 0x8000),
    (dict(t_ref=400.0), 0, 32767),                 # clamped
    (dict(t_ref=-400.0), 0, 0x8001),               # clamped, 0x8000 stays reserved
    (dict(uptime_s=0xFFFFFFFF), 7, 0xFFFF),
    (dict(uptime_s=0xFFFFFFFF), 8, 0xFFFF),
    (dict(priority=0), 6, 0),
    (dict(status=0x48), 5, 0x48),
])
def test_mt11_scaling(server, values, register, expected):
    resp = server(**values).request(read(0x04, register, 1))
    assert resp == bytes([0x04, 2]) + regs(expected)
