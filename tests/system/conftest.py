"""Hardware-in-the-loop system tests: they talk to a real node over the
network. Not run in CI. Settings are in config.toml."""

import json
import queue
import time
import tomllib
import uuid
from pathlib import Path

import paho.mqtt.client as mqtt
import pytest
from pymodbus.client import ModbusTcpClient

CONFIG = tomllib.loads((Path(__file__).parent / "config.toml").read_text())
NODE_ID = CONFIG["node"]["id"]
PREFIX = f"roomnode/{NODE_ID}"

STATUS_SIMULATION = 1 << 3


class Mqtt:
    """Test client that records every message under roomnode/<id>/."""

    def __init__(self, client_id=None):
        self.messages = queue.Queue()
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                                  client_id=client_id or f"pytest-{uuid.uuid4().hex[:8]}")
        self.client.on_message = lambda c, u, msg: self.messages.put((msg.topic, msg.payload.decode(), msg.retain))
        self.client.connect(CONFIG["broker"]["host"], CONFIG["broker"]["port"])
        self.client.subscribe(f"{PREFIX}/#", qos=1)
        self.client.loop_start()

    def publish(self, subtopic, payload):
        self.client.publish(f"{PREFIX}/{subtopic}", payload, qos=1).wait_for_publish(5)

    def wait_for(self, subtopic, predicate=lambda payload: True, timeout=15, live_only=False):
        """Next message on the subtopic that satisfies predicate. State
        payloads are decoded from JSON."""
        deadline = time.monotonic() + timeout
        while (left := deadline - time.monotonic()) > 0:
            try:
                topic, payload, retained = self.messages.get(timeout=left)
            except queue.Empty:
                break
            if topic != f"{PREFIX}/{subtopic}" or (live_only and retained):
                continue
            value = json.loads(payload) if subtopic == "state" else payload
            if predicate(value):
                return value
        pytest.fail(f"no matching message on {subtopic} within {timeout} s")

    def close(self):
        self.client.loop_stop()
        self.client.disconnect()


class Modbus:
    def __init__(self):
        self.client = ModbusTcpClient(CONFIG["node"]["host"], port=502, timeout=3)
        assert self.client.connect(), f"no Modbus TCP server at {CONFIG['node']['host']}:502"

    def inputs(self):
        r = self.client.read_input_registers(0, count=9)
        assert not r.isError(), r
        return r.registers

    def holding(self):
        r = self.client.read_holding_registers(100, count=2)
        assert not r.isError(), r
        return r.registers

    def write(self, address, value):
        return self.client.write_register(address, value)

    def close(self):
        self.client.close()


def s16(value):
    return value - 0x10000 if value & 0x8000 else value


@pytest.fixture
def modbus():
    client = Modbus()
    yield client
    client.close()


@pytest.fixture
def broker():
    client = Mqtt()
    yield client
    client.close()


@pytest.fixture(autouse=True)
def restore_node():
    """Every test leaves the node in auto mode without a setpoint override,
    so the order of the tests does not matter."""
    yield
    client = Modbus()
    client.client.write_registers(100, [0x8000, 0])
    client.close()


@pytest.fixture
def min_switch_s(modbus):
    """Minimum on/off time of the running firmware: 10 s in demo builds."""
    return 10 if modbus.inputs()[5] & STATUS_SIMULATION else 300
