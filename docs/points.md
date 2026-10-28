# Data points

Every interface reads the same snapshot of the room and writes through the same controller functions, so values and validation rules are identical across protocols.

| Point | Modbus | MQTT `state` field | BACnet | Access |
| --- | --- | --- | --- | --- |
| Regulated temperature | IR 0 | `t_ref` | AI 1 (planned) | R |
| SHTC3 temperature | IR 1 | `t_aux` | AI 2 (planned) | R |
| Relative humidity | IR 2 | `rh` | AI 3 (planned) | R |
| Active setpoint | IR 3 | `setpoint` | AV 1 (planned) | R |
| Setpoint override (priority 8) | HR 100 | `cmd/setpoint` | AV 1 @ 8 (planned) | R/W |
| Heating output | IR 4 | `heating` | BO 1 (planned) | R |
| Mode | HR 101 | `mode`, `cmd/mode` | MSV 1 (planned) | R/W |
| Active heating priority | IR 6 | `priority` | BO 1 Priority_Array (planned) | R |
| Status bits | IR 5 | `status` | Status_Flags, Reliability (planned) | R |
| Uptime | IR 7–8 | `uptime` | — | R |

## Modbus TCP

Port 502, unit ID 1 (255 is accepted too; other unit IDs get exception 0x0B). Up to 4 simultaneous clients; a connection idle for 60 s is closed. Function codes 03, 04, 06 and 16.

Scaled values are signed or unsigned 16-bit integers multiplied by 100. `0x8000` (−32768) means "not available": a missing sensor in an input register, or no override in holding register 100.

### Input registers (FC04)

| Address | Name | Format |
| --- | --- | --- |
| 0 | Regulated temperature (TMP117, SHTC3 fallback, or room model in demo builds) | int16, °C × 100 |
| 1 | SHTC3 temperature | int16, °C × 100 |
| 2 | Relative humidity | uint16, % × 100 |
| 3 | Active setpoint | int16, °C × 100 |
| 4 | Heating output | 0 / 1 |
| 5 | Status bits (see below) | bit field |
| 6 | Priority controlling the heating output | 1, 5, 6, 8, 16; 0 = relinquish default |
| 7 | Uptime, high word | uint32 seconds, big-endian word order |
| 8 | Uptime, low word | |

### Holding registers (FC03, FC06, FC16)

| Address | Name | Format |
| --- | --- | --- |
| 100 | Setpoint override at priority 8 | int16, °C × 100, 1600–2600. Write `0x8000` to relinquish. Reads `0x8000` when no override is active |
| 101 | Mode | 0 auto, 1 off, 2 on |

### Exceptions

| Code | When |
| --- | --- |
| 01 Illegal function | Any function code other than 03, 04, 06, 16 |
| 02 Illegal data address | Any register outside the tables above, including writes to input registers |
| 03 Illegal data value | Quantity 0 or above the protocol limit, byte count mismatch, setpoint outside 1600–2600, mode outside 0–2 |
| 0B Gateway target failed | Unit ID other than 1 or 255 |

A rejected request changes nothing. A multiple-register write (FC16) is validated completely before any value is applied.

## Status bits

Identical in Modbus IR 5 and the MQTT `status` field.

| Bit | Value | Meaning |
| --- | --- | --- |
| 0 | 1 | TMP117 fault |
| 1 | 2 | SHTC3 fault |
| 2 | 4 | MQTT offline |
| 3 | 8 | Simulation (demo build) |
| 4 | 16 | Frost protection active |
| 5 | 32 | Heating on for more than 2 h |
| 6 | 64 | Setpoint overridden by an operator |

MQTT topics and the state message are described in [mqtt.md](mqtt.md).
