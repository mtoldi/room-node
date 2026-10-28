# RoomNode

[![build](https://github.com/mtoldi/room-node/actions/workflows/build.yml/badge.svg)](https://github.com/mtoldi/room-node/actions/workflows/build.yml)
[![license: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

Open-source room controller for building automation: an ESP32 node that measures room climate, runs a thermostat with BACnet-style command priorities and exposes its data over BACnet/IP, Modbus TCP, MQTT and HTTPS.

> Work in progress. Implemented so far: sensor drivers, FreeRTOS tasks, the controller (`roomcore`) with its host-side module tests, setpoint slider, LED indicator, Wi-Fi, MQTT, Modbus TCP and hardware-in-the-loop system tests. Next: BACnet/IP.

## Hardware

| Part | Role |
| --- | --- |
| Soldered Dasduino ConnectPlus (ESP32) | Room controller |
| Soldered TMP117 breakout | Reference temperature (±0.1 °C), I²C 0x49 |
| Soldered SHTC3 breakout | Auxiliary temperature and humidity, I²C 0x70 |
| Soldered slider potentiometer | Local setpoint, 16–26 °C |
| Soldered Smart LED WS2812B Stick10 | Heating output indicator |

See [docs/wiring.md](docs/wiring.md) for the connections.

## Control logic

The heating output is a commandable point with a BACnet priority array: the highest filled level wins.

| Priority | Source | Rule |
| --- | --- | --- |
| 1 | Fail-safe | No valid temperature → off |
| 5 | Frost protection | Below 7 °C → on, released above 8 °C |
| 6 | Minimum on/off time | Every change is latched for 5 min (10 s in demo builds) |
| 8 | Operator | Mode on/off from BACnet, Modbus or MQTT |
| 16 | Thermostat | ±0.3 °C hysteresis around the setpoint |

So frost protection and the fail-safe override the minimum on/off time, while the operator and the thermostat cannot. The setpoint is commandable too: its relinquish default follows the slider, a remote write at priority 8 overrides it, and moving the slider by more than 0.3 °C hands control back to the room.

## Interfaces

- **Modbus TCP**: own server implementation (port 502, FC03/04/06/16, proper exception responses). Register map in [docs/points.md](docs/points.md).
- **MQTT**: retained state JSON, online/offline Last Will, setpoint and mode commands. See [docs/mqtt.md](docs/mqtt.md).

## Build and flash

Requires [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) v6.0.3. ESP-IDF does not support spaces in paths, so keep both ESP-IDF and this repository in a path without spaces.

Network settings (Wi-Fi, MQTT broker, node ID) are read from `firmware/node/sdkconfig.defaults.local`, which is not committed. Copy `sdkconfig.defaults.local.example` and fill it in; without it the node runs offline.

```sh
cd firmware/node
idf.py set-target esp32   # first time only
idf.py build
idf.py -p COM3 flash monitor
```

The demo build shortens the minimum switching time to 10 s and regulates on a simulated room temperature, so the control loop can be watched within minutes:

```sh
idf.py -B build-demo -D SDKCONFIG=build-demo/sdkconfig \
       -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.demo" -p COM3 flash monitor
```

## Tests

The controller logic lives in `firmware/node/components/roomcore`, plain C without ESP-IDF dependencies. The module tests compile it as a host library and drive it from pytest through `ctypes`, with a simulated clock, so a full day of operation runs in a fraction of a second. They run in CI on every push.

```sh
python -m venv .venv && .venv/bin/pip install -r tests/requirements.txt   # Windows: .venv\Scripts\
.venv/bin/pytest tests/module -v
```

## Design notes

- **Logic separated from the RTOS.** Tasks only do I/O; every decision is made in `roomcore`, which takes time as a parameter. This makes it testable on a PC and keeps timing correct across the 49-day wrap of the millisecond counter.
- **Own sensor drivers.** The TMP117 is read register by register with every I²C transfer checked and the device ID verified. A typical Arduino library returns 0 °C on a bus error and rewrites the sensor EEPROM on every boot. The SHTC3 uses the measurement command without clock stretching: in normal mode the sensor would hold SCL for up to 12 ms, at the edge of the ESP32 I²C timeout. The task sleeps during the conversion instead, so the bus stays free.
- **One owner per resource.** `sensor_task` is the only task touching the I²C bus. The controller and the shared state sit behind one mutex, so control cycles and commands from any protocol are serialised.
- **Safe start.** The output starts off and is held off for the minimum off time, so a node stuck in a reset loop cannot switch the heating rapidly. A task that stops feeding the watchdog resets the node.

## License

MIT
