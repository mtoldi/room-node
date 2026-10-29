# Data points

Every interface reads the same snapshot of the room and writes through the same controller functions, so values and validation rules are identical across protocols.

| Point | Modbus | MQTT `state` field | BACnet | Access |
| --- | --- | --- | --- | --- |
| Regulated temperature | IR 0 | `t_ref` | AI 1 | R |
| SHTC3 temperature | IR 1 | `t_aux` | AI 2 | R |
| Relative humidity | IR 2 | `rh` | AI 3 | R |
| Active setpoint | IR 3 | `setpoint` | AV 1 | R |
| Setpoint override (priority 8) | HR 100 | `cmd/setpoint` | AV 1 @ 8 | R/W |
| Heating output | IR 4 | `heating` | BO 1 | R |
| Mode | HR 101 | `mode`, `cmd/mode` | MSV 1 | R/W |
| Active heating priority | IR 6 | `priority` | BO 1 Current_Command_Priority | R |
| Status bits | IR 5 | `status` | Status_Flags, Reliability | R |
| Uptime | IR 7–8 | `uptime` | — | R |

## BACnet/IP

UDP port 47808, device instance 1001 (`CONFIG_ROOMNODE_BACNET_INSTANCE`), device name `RoomNode-<id>`. Protocol revision 28, no segmentation, max APDU 1476. Services: Who-Is / I-Am, ReadProperty, ReadPropertyMultiple, WriteProperty. The device announces itself with I-Am at startup.

| Object | Name | Present_Value | Notes |
| --- | --- | --- | --- |
| Device 1001 | RoomNode-node1 | — | Object_List, Protocol_Services_Supported, Protocol_Object_Types_Supported |
| AI 1 | Room temperature | REAL, °C | Regulated temperature. Reliability `no-sensor` and Status_Flags fault when no sensor is valid |
| AI 2 | Aux temperature | REAL, °C | SHTC3 |
| AI 3 | Relative humidity | REAL, %RH | SHTC3 |
| AV 1 | Setpoint | REAL, °C | Commandable: Priority_Array, Relinquish_Default (the slider), Current_Command_Priority |
| BO 1 | Heating | ENUMERATED active/inactive | Commandable: Priority_Array shows every level the controller uses (1, 5, 6, 8, 16); Minimum_On_Time and Minimum_Off_Time in seconds |
| MSV 1 | Mode | UNSIGNED 1–3 | State_Text: Auto, Off, On |

### Writes

| Target | Value | Effect |
| --- | --- | --- |
| AV 1 Present_Value @ 8 | REAL 16–26 | Setpoint override |
| AV 1 Present_Value @ 8 | NULL | Relinquish the override |
| BO 1 Present_Value @ 8 | active / inactive | Mode on / off |
| BO 1 Present_Value @ 8 | NULL | Mode auto |
| MSV 1 Present_Value | 1, 2, 3 | Mode auto, off, on |

The priority arrays are the controller's own, not copies: levels 1 (fail-safe), 5 (frost protection), 6 (minimum on/off time) and 16 (thermostat) belong to the controller, so a write at any priority other than 8 is answered with `write-access-denied`. A write without a priority means 16 according to the standard and is denied for the same reason. Values outside the valid range give `value-out-of-range`; writes to any other property give `write-access-denied`.

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
