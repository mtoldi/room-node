# Test plan

RoomNode is tested at two levels, both written in Python with pytest:

| Level | What is tested | Where it runs | Duration |
| --- | --- | --- | --- |
| **Module** (MT) | `roomcore`: controller, priority arrays, safety rules, MQTT encodings, Modbus protocol layer. The C sources are compiled into a host library and driven through `ctypes` with a simulated clock | Any PC, and CI on every push | < 1 s |
| **System** (ST) | A running node over the network: BACnet/IP, Modbus TCP and MQTT, against independent client implementations (bacpypes3, pymodbus, paho-mqtt) | PC on the node's subnet, with the hardware | ~1 min (demo build), ~6 min (normal build) |

A few behaviours that need hands on the hardware are covered by manual tests (MAN). Every test traces back to a requirement below; test function names carry the test ID (`test_mt03_...`, `test_st05_...`), so a report maps directly onto this plan.

## Requirements

| ID | Requirement |
| --- | --- |
| REQ-01 | Both sensors are sampled every second. A sensor is reported faulty after 3 consecutive failed reads and recovers on its own |
| REQ-02 | The TMP117 is the reference temperature. If it fails, the SHTC3 takes over and the fault is reported |
| REQ-03 | The slider sets the setpoint between 16 and 26 °C in 0.1 °C steps; ADC noise must not make the setpoint flicker |
| REQ-04 | The thermostat switches on below setpoint − 0.3 °C and off above setpoint + 0.3 °C |
| REQ-05 | The heating output and the setpoint are commandable: the highest filled priority level wins, relinquishing a level hands control to the next one |
| REQ-06 | Every change of the heating output is held for the minimum on/off time (5 min, 10 s in demo builds), also after boot and across the 49-day wrap of the millisecond clock |
| REQ-07 | Below 7 °C the heating is on regardless of mode and minimum off time; frost protection ends above 8 °C |
| REQ-08 | Without a valid temperature the heating is off immediately, regardless of all other rules |
| REQ-09 | Heating continuously on for 2 h raises an alarm bit without switching off |
| REQ-10 | An operator can override the setpoint at priority 8 (16–26 °C) and relinquish it; moving the slider by more than 0.3 °C ends the override |
| REQ-11 | An operator can force the heating on or off at priority 8 and return to automatic |
| REQ-12 | MQTT: retained state every 10 s and immediately after relevant changes; online/offline through a retained Last Will; automatic reconnection |
| REQ-13 | MQTT commands are validated; invalid payloads change nothing |
| REQ-14 | Modbus TCP: register map, FC03/04/06/16, exceptions 01/02/03/0B, all-or-nothing multiple writes |
| REQ-15 | BACnet/IP: the device answers Who-Is and serves ReadProperty, ReadPropertyMultiple and WriteProperty for its objects |
| REQ-16 | BACnet writes are accepted at priority 8 only; values outside the valid range are rejected |
| REQ-17 | All protocols report the same values |
| REQ-18 | The demo room model settles around the setpoint without short cycling |
| REQ-19 | The two temperature sensors agree within 1 °C |

## Module tests

Run: `pytest tests/module`. Preconditions: a C compiler (`gcc` or `cc`, or `CC` set), Python packages from `tests/requirements.txt`.

| ID | Requirement | Test | Expected result |
| --- | --- | --- | --- |
| MT-01 | REQ-04 | Step the temperature across both hysteresis thresholds | On below 20.7 °C, off above 21.3 °C (setpoint 21), no change in between |
| MT-02 | REQ-05, REQ-11 | Mode off/on/auto against a thermostat demand | Operator (8) beats thermostat (16); `auto` hands control back; invalid mode rejected |
| MT-03 | REQ-06 | Demand heat right after boot, then remove it; operator tries to break the hold | Output held off for 300 s after boot, held on for 300 s after switching; active priority 6 while held |
| MT-04 | REQ-07 | 5 °C with mode off, then 7.5 °C and 8.5 °C | On at priority 5 despite boot hold and mode off; held until above 8 °C; then minimum on time applies |
| MT-05 | REQ-08 | Both sensors invalid while frost protection is heating | Off within one cycle at priority 1, both fault bits set |
| MT-06 | REQ-02 | TMP117 invalid, SHTC3 at 18 °C | Regulated temperature is 18 °C, TMP117 fault bit set, control continues |
| MT-07 | REQ-03, REQ-10 | Writes of 26.5, 15.9, NaN and 23.5 °C; slider moved by 0.2 and 0.6 °C; slider mapping end points | Out-of-range and NaN rejected; override held for 0.2 °C, ended by 0.6 °C; 0/100 mV → 16 °C, 3000/3300 mV → 26 °C |
| MT-08 | REQ-09 | Heating on for 2 h − 10 s and 2 h + 10 s | Alarm bit only after 2 h; heating stays on |
| MT-09 | REQ-14 | FC04 all inputs, FC03 holdings, FC06 and FC16 writes, framing with unit 255 | Responses match expected bytes; transaction ID echoed; writes reach the controller |
| MT-10 | REQ-14 | 18 malformed or invalid requests, partly valid FC16, foreign unit ID, truncated frames | Exceptions 01/02/03/0B as specified; nothing applied; malformed frames rejected |
| MT-11 | REQ-14 | Negative, rounded, missing, clamped and 32-bit values | Two's complement, rounding, 0x8000 for "not available", clamping that keeps 0x8000 reserved |
| MT-12 | REQ-18 | 24 h of the demo configuration with the room model | After 1 h the temperature stays within setpoint ± 0.6 °C, 5–40 cycles per hour, no on-period shorter than 10 s |
| MT-13 | REQ-06 | Start 60 s before the millisecond counter wraps | Minimum time works across the wrap |
| MT-14 | REQ-03 | Slider resting on a rounding boundary with ±6 mV noise | Setpoint does not flicker; real movement still passes |
| MT-15 | REQ-12 | State JSON with valid, missing and extreme values; change detection | Valid JSON, `null` for missing values, buffer overflow reported; temperature drift alone does not trigger a publish |
| MT-16 | REQ-13 | Setpoint and mode payloads, including whitespace, garbage, NaN, oversize and non-terminated input | Only valid payloads parsed; only `len` bytes read |

## System tests

Run: `pytest tests/system`. Preconditions:

- Node flashed and on the network; `tests/system/config.toml` holds its address, node ID and BACnet device instance, and this PC's address on the node's subnet.
- MQTT broker reachable by the node and the PC.
- Firewall allows UDP 47808 (BACnet replies) on this PC.
- Every test leaves the node in auto mode without overrides (autouse fixture), so the order does not matter.

| ID | Requirement | Test | Expected result |
| --- | --- | --- | --- |
| ST-01 | REQ-15 | Directed and global Who-Is; Device properties | I-Am from the node's address; name, protocol revision 28 and Object_List of 7 objects |
| ST-02 | REQ-15, REQ-01 | ReadPropertyMultiple on AI 1–3 | Values in range, no fault flags, `no-fault-detected`, correct units |
| ST-03 | REQ-10, REQ-11, REQ-17 | BACnet setpoint write at priority 8 and relinquish; mode On via MSV 1 | Value visible in Modbus and MQTT; priority array shows level 8. Heating active once the minimum off time allows; Current_Command_Priority is 6 for one minimum on time after switching, then 8 |
| ST-04 | REQ-16 | Writes at priority 16, without priority, at priority 1, to an AI, out of range | `write-access-denied` or `value-out-of-range`; nothing changes |
| ST-05 | REQ-14, REQ-10 | Read all input registers; write HR 100 | Values in range; IR 3 and HR 100 follow; MQTT state shows the override |
| ST-06 | REQ-14 | Out-of-range writes and an invalid address | Exceptions 03 and 02; state unchanged |
| ST-07 | REQ-12 | Subscribe to the node | Retained `online`; a live state message within 15 s with all fields |
| ST-08 | REQ-11, REQ-13 | `cmd/mode on`; invalid commands | Heating on at priority 8 within the minimum off time; invalid commands change nothing |
| ST-09 | REQ-17 | Temperature over BACnet, Modbus and MQTT | Within 0.05 °C of each other |
| ST-10 | REQ-19 | TMP117 vs SHTC3 | Difference below 1 °C (skipped in demo builds, where the regulated temperature is the room model) |
| ST-11 | REQ-12 | A client with the node's MQTT ID takes over its session | The node reconnects on its own and publishes `online` within 30 s |

## Manual tests

Performed on the hardware with the serial monitor and the LED stick in view.

| ID | Requirement | Steps | Expected result |
| --- | --- | --- | --- |
| MAN-01 | REQ-01 | Unplug the SHTC3 easyC cable, wait 5 s, plug it back in | Last LED blinks red after 3 s; SHTC3 found again after reconnecting; status bit 1 clears |
| MAN-02 | REQ-08 | Unplug both sensors while heating | Whole stick blinks red, heating off at once, priority 1 |
| MAN-03 | REQ-03 | Move the slider end to end | Setpoint 16.0 → 26.0 °C, stable when resting |
| MAN-04 | REQ-04 | Demo build: raise the setpoint above the room temperature | Orange LEDs after at most 10 s, one per 0.5 °C below setpoint; off above setpoint + 0.3 °C |
| MAN-05 | REQ-12 | Power the node off while subscribed to `status` | `offline` about 22 s later (Last Will) |

## Reports

- `pytest tests/module --html=report-module.html --self-contained-html` — produced by CI on every push and kept as a build artifact.
- `pytest tests/system --html=report-system.html --self-contained-html` — run against the hardware; a run on the normal build is kept in [reports](reports/README.md).

## Traceability

| Requirement | Module | System | Manual |
| --- | --- | --- | --- |
| REQ-01 Sensor sampling and faults | | ST-02 | MAN-01 |
| REQ-02 TMP117 reference, SHTC3 fallback | MT-06 | | |
| REQ-03 Slider setpoint | MT-07, MT-14 | | MAN-03 |
| REQ-04 Hysteresis | MT-01 | | MAN-04 |
| REQ-05 Priority arrays | MT-02 | ST-03 | |
| REQ-06 Minimum on/off time | MT-03, MT-13 | ST-08 | |
| REQ-07 Frost protection | MT-04 | | |
| REQ-08 Fail-safe | MT-05 | | MAN-02 |
| REQ-09 Runtime alarm | MT-08 | | |
| REQ-10 Setpoint override | MT-07 | ST-03, ST-05 | |
| REQ-11 Mode | MT-02 | ST-03, ST-08 | |
| REQ-12 MQTT publishing and reconnection | MT-15 | ST-07, ST-11 | MAN-05 |
| REQ-13 MQTT command validation | MT-16 | ST-08 | |
| REQ-14 Modbus TCP | MT-09, MT-10, MT-11 | ST-05, ST-06 | |
| REQ-15 BACnet services and objects | | ST-01, ST-02 | |
| REQ-16 BACnet write rules | | ST-04 | |
| REQ-17 Cross-protocol consistency | | ST-03, ST-09 | |
| REQ-18 Demo room model | MT-12 | | |
| REQ-19 Sensor agreement | | ST-10 | |

Frost protection (REQ-07) and the runtime alarm (REQ-09) are verified at module level only: reproducing 5 °C or two hours of heating on the bench would add nothing the simulated clock does not already show.
