"""System tests against a running node. IDs match docs/test-plan.md."""

import time

import pytest

from conftest import NODE_ID, STATUS_SIMULATION, Mqtt, s16

STATE_FIELDS = {"t_ref", "t_aux", "rh", "setpoint", "heating", "priority", "mode", "status", "uptime"}
PRIORITIES = {0, 1, 5, 6, 8, 16}


def wait_until(check, timeout, interval=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (result := check()):
            return result
        time.sleep(interval)
    pytest.fail(f"condition not met within {timeout} s")


def test_st05_modbus_registers(modbus, broker):
    t_ref, t_aux, rh, setpoint, heating, status, priority, up_hi, up_lo = modbus.inputs()
    assert 0 <= s16(t_ref) / 100 <= 40
    assert 0 <= s16(t_aux) / 100 <= 40
    assert 0 <= rh / 100 <= 100
    assert 16 <= s16(setpoint) / 100 <= 26
    assert heating in (0, 1)
    assert priority in PRIORITIES
    assert (up_hi << 16 | up_lo) > 0

    assert not modbus.write(100, 2250).isError()
    assert modbus.inputs()[3] == 2250
    assert modbus.holding()[0] == 2250
    state = broker.wait_for("state", lambda s: s["setpoint"] == 22.5, live_only=True)
    assert state["status"] & 64, "override bit set"


def test_st06_modbus_rejects_out_of_range(modbus):
    before = modbus.inputs()[3]
    for address, value in ((100, 5000), (100, 1500), (101, 3)):
        r = modbus.write(address, value)
        assert r.isError() and r.exception_code == 3, (address, value)
    r = modbus.client.read_input_registers(9, count=1)
    assert r.isError() and r.exception_code == 2
    assert modbus.inputs()[3] == before, "state unchanged"


def test_st07_mqtt_state(broker):
    assert broker.wait_for("status") == "online"
    state = broker.wait_for("state", live_only=True, timeout=15)
    assert set(state) == STATE_FIELDS
    assert state["mode"] in ("auto", "off", "on")


def test_st08_mode_on_via_mqtt(broker, modbus, min_switch_s):
    broker.publish("cmd/mode", "on")
    # The heating may be latched off at priority 6 for up to the minimum off time
    wait_until(lambda: modbus.inputs()[4] == 1, timeout=min_switch_s + 5)
    wait_until(lambda: modbus.inputs()[6] == 8, timeout=min_switch_s + 5)
    state = broker.wait_for("state", lambda s: s["priority"] == 8, live_only=True)
    assert state["heating"] is True and state["mode"] == "on"


def test_st08_invalid_mqtt_commands_change_nothing(broker, modbus):
    before = modbus.inputs()[3]
    for topic, payload in (("cmd/setpoint", "50"), ("cmd/setpoint", "abc"), ("cmd/mode", "heat")):
        broker.publish(topic, payload)
    time.sleep(1)
    assert modbus.inputs()[3] == before
    assert modbus.holding() == [0x8000, 0]


def test_st09_consistency_modbus_mqtt(broker, modbus):
    state = broker.wait_for("state", live_only=True)
    t_ref = s16(modbus.inputs()[0]) / 100
    assert abs(state["t_ref"] - t_ref) <= 0.05


def test_st10_sensor_agreement(modbus):
    t_ref, t_aux, *_rest = modbus.inputs()
    if modbus.inputs()[5] & STATUS_SIMULATION:
        pytest.skip("t_ref is the room model in demo builds")
    assert abs(s16(t_ref) - s16(t_aux)) / 100 < 1.0


def test_st11_mqtt_reconnect(broker):
    """A client with the node's ID takes over its session, which disconnects
    the node. It must come back on its own and announce itself online."""
    assert broker.wait_for("status") == "online"
    impostor = Mqtt(client_id=f"roomnode-{NODE_ID}")
    try:
        broker.wait_for("status", lambda s: s == "online", timeout=30, live_only=True)
    finally:
        impostor.close()
