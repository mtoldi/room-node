"""System tests against a running node. IDs match docs/test-plan.md."""

import time

import pytest
from bacpypes3.primitivedata import Null, Real

from conftest import CONFIG, NODE_ID, STATUS_SIMULATION, Mqtt, s16

STATE_FIELDS = {"t_ref", "t_aux", "rh", "setpoint", "heating", "priority", "mode", "status", "uptime"}
PRIORITIES = {0, 1, 5, 6, 8, 16}
OBJECTS = ["analog-input,1", "analog-input,2", "analog-input,3", "analog-value,1", "binary-output,1",
           "multi-state-value,1"]


def wait_until(check, timeout, interval=0.5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if (result := check()):
            return result
        time.sleep(interval)
    pytest.fail(f"condition not met within {timeout} s")


def choice(value):
    """Value of a BACnet CHOICE (PriorityValue, OptionalUnsigned); None for NULL."""
    return None if value._choice == "null" else getattr(value, value._choice)


def test_st01_bacnet_discovery(bacnet):
    i_ams = bacnet.who_is(bacnet.device, bacnet.device)
    assert [str(i.iAmDeviceIdentifier) for i in i_ams] == [f"device,{bacnet.device}"]
    assert str(i_ams[0].pduSource) == CONFIG["node"]["host"]
    assert any(str(i.iAmDeviceIdentifier) == f"device,{bacnet.device}" for i in bacnet.who_is()), \
        "answers an unrestricted Who-Is too"

    device = f"device,{bacnet.device}"
    assert bacnet.read(device, "object-name") == f"RoomNode-{NODE_ID}"
    assert bacnet.read(device, "protocol-revision") == 28
    assert [str(o) for o in bacnet.read(device, "object-list")] == [device] + OBJECTS
    assert bacnet.read(device, "object-list", 0) == len(OBJECTS) + 1


def test_st02_bacnet_read_property_multiple(bacnet):
    spec = []
    for obj in OBJECTS[:3]:
        spec += [obj, ["present-value", "status-flags", "reliability", "units"]]
    values = {(str(o), str(p)): v for o, p, _, v in bacnet.read_multiple(spec)}

    for obj, low, high in (("analog-input,1", 0, 40), ("analog-input,2", 0, 40), ("analog-input,3", 0, 100)):
        assert low <= values[(obj, "present-value")] <= high, obj
        assert list(values[(obj, "status-flags")]) == [0, 0, 0, 0], f"{obj} reports a fault"
        assert str(values[(obj, "reliability")]) == "no-fault-detected"
    assert str(values[("analog-input,3", "units")]) == "percent-relative-humidity"


def test_st03_bacnet_setpoint_reaches_all_protocols(bacnet, modbus, broker):
    assert bacnet.write("analog-value,1", "present-value", Real(23.5), priority=8) is None
    assert bacnet.read("analog-value,1", "present-value") == 23.5
    assert choice(bacnet.read("analog-value,1", "priority-array", 8)) == 23.5
    assert choice(bacnet.read("analog-value,1", "current-command-priority")) == 8

    assert modbus.inputs()[3] == 2350
    assert modbus.holding()[0] == 2350
    broker.wait_for("state", lambda s: s["setpoint"] == 23.5, live_only=True)

    assert bacnet.write("analog-value,1", "present-value", Null(()), priority=8) is None
    assert modbus.holding()[0] == 0x8000, "relinquished"
    assert choice(bacnet.read("analog-value,1", "current-command-priority")) is None


def test_st03_bacnet_mode_drives_heating_priority(bacnet, modbus, min_switch_s):
    assert bacnet.write("multi-state-value,1", "present-value", 3) is None  # On
    assert modbus.holding()[1] == 2
    # Up to one minimum off time until the output may switch on, then the new
    # state is latched at priority 6 for one minimum on time before 8 shows
    wait_until(lambda: str(bacnet.read("binary-output,1", "present-value")) == "active",
               timeout=min_switch_s + 5)
    wait_until(lambda: choice(bacnet.read("binary-output,1", "current-command-priority")) == 8,
               timeout=min_switch_s + 5)
    assert str(choice(bacnet.read("binary-output,1", "priority-array", 8))) == "1"  # active
    assert bacnet.read("binary-output,1", "minimum-on-time") == min_switch_s


def test_st04_bacnet_write_rules(bacnet, modbus):
    before = modbus.inputs()[3]
    assert "write-access-denied" in bacnet.write("analog-value,1", "present-value", Real(22.0), priority=16)
    assert "write-access-denied" in bacnet.write("analog-value,1", "present-value", Real(22.0)), \
        "no priority means 16, which belongs to the thermostat"
    assert "write-access-denied" in bacnet.write("binary-output,1", "present-value", Null(()), priority=1)
    assert "write-access-denied" in bacnet.write("analog-input,1", "present-value", Real(10.0), priority=8)
    assert "value-out-of-range" in bacnet.write("analog-value,1", "present-value", Real(30.0), priority=8)
    assert "value-out-of-range" in bacnet.write("multi-state-value,1", "present-value", 4)
    assert modbus.inputs()[3] == before
    assert modbus.holding() == [0x8000, 0], "nothing was written"


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


def test_st09_consistency_across_protocols(broker, modbus, bacnet):
    state = broker.wait_for("state", live_only=True)
    t_modbus = s16(modbus.inputs()[0]) / 100
    t_bacnet = bacnet.read("analog-input,1", "present-value")
    assert abs(state["t_ref"] - t_modbus) <= 0.05
    assert abs(t_bacnet - t_modbus) <= 0.05


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
