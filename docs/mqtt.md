# MQTT interface

All topics are under `roomnode/<id>/`, where `<id>` is `CONFIG_ROOMNODE_ID` (default `node1`).

| Topic | Direction | Payload | Notes |
| --- | --- | --- | --- |
| `state` | node → broker | JSON, see below | Retained. Every 10 s and immediately after a change of heating, priority, mode, setpoint or status |
| `status` | node → broker | `online` / `offline` | Retained. `offline` is the Last Will, published by the broker about 22 s (1.5 × keepalive) after the node disappears |
| `cmd/setpoint` | broker → node | `21.5` or `null` | Operator setpoint at priority 8, valid 16–26 °C. `null` relinquishes it; moving the slider by more than 0.3 °C does too |
| `cmd/mode` | broker → node | `auto` / `off` / `on` | `off` and `on` write priority 8 of the heating output, `auto` relinquishes it |

Invalid commands (out of range, not a number, unknown mode) are rejected and logged; the state does not change. Commands are subscribed with QoS 1.

## State message

```json
{"t_ref":21.84,"t_aux":22.10,"rh":47.2,"setpoint":22.0,"heating":true,
 "priority":16,"mode":"auto","status":64,"uptime":86400}
```

| Field | Meaning |
| --- | --- |
| `t_ref` | Regulated temperature in °C: TMP117, SHTC3 if the TMP117 fails, or the room model in demo builds. `null` without a valid sensor |
| `t_aux` | SHTC3 temperature in °C, `null` if the sensor is faulty |
| `rh` | Relative humidity in %, `null` if the SHTC3 is faulty |
| `setpoint` | Active setpoint in °C (operator override or slider) |
| `heating` | Heating output |
| `priority` | Priority level currently controlling the heating output (1 fail-safe, 5 frost, 6 minimum on/off time, 8 operator, 16 thermostat, 0 default) |
| `mode` | `auto`, `off` or `on` |
| `status` | Bit field, identical to Modbus input register 5 (see below) |
| `uptime` | Seconds since boot |

## Status bits

| Bit | Value | Meaning |
| --- | --- | --- |
| 0 | 1 | TMP117 fault |
| 1 | 2 | SHTC3 fault |
| 2 | 4 | MQTT offline (never seen over MQTT itself, but visible over Modbus and BACnet) |
| 3 | 8 | Simulation (demo build) |
| 4 | 16 | Frost protection active |
| 5 | 32 | Heating on for more than 2 h |
| 6 | 64 | Setpoint overridden by an operator |

## Trying it out

```sh
mosquitto_sub -h <broker> -t "roomnode/#" -v
mosquitto_pub -h <broker> -t roomnode/node1/cmd/setpoint -m 23.5
mosquitto_pub -h <broker> -t roomnode/node1/cmd/mode -m on
```
