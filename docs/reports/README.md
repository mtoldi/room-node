# Test reports

## System tests on hardware — 2026-10-29

[system-tests.html](system-tests.html) is the pytest-html report of this run. GitHub shows HTML files as source: download the file and open it in a browser.

| | |
| --- | --- |
| Firmware | normal build (5 min minimum on/off time), commit of 2026-10-29 |
| Hardware | Dasduino ConnectPlus, TMP117, SHTC3, slider, WS2812B stick |
| Network | node and test PC on the same 2.4 GHz Wi-Fi subnet; Mosquitto 2 broker on the PC |
| Clients | bacpypes3 0.0.110, pymodbus 3.16, paho-mqtt 2.1 |
| Result | **13 passed**, 0 failed, 0 skipped |

| Test | Result |
| --- | --- |
| ST-01 BACnet discovery and Device object | passed |
| ST-02 BACnet ReadPropertyMultiple on AI 1–3 | passed |
| ST-03 BACnet setpoint reaches Modbus and MQTT | passed |
| ST-03 BACnet mode drives the heating priority | passed |
| ST-04 BACnet write rules | passed |
| ST-05 Modbus registers | passed |
| ST-06 Modbus rejects out-of-range writes | passed |
| ST-07 MQTT state | passed |
| ST-08 Mode on via MQTT | passed |
| ST-08 Invalid MQTT commands change nothing | passed |
| ST-09 Same temperature over BACnet, Modbus and MQTT | passed |
| ST-10 TMP117 and SHTC3 agree within 1 °C | passed |
| ST-11 MQTT reconnection | passed |

Module test reports are attached to every CI run of the [tests workflow](https://github.com/mtoldi/room-node/actions/workflows/tests.yml) as the `module-test-report` artifact.
