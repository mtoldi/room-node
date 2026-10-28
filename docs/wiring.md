# Wiring

All parts are Soldered boards. No soldering or extra components are needed.

## Sensors (easyC / Qwiic, I²C)

| Sensor | Address | Connection |
| --- | --- | --- |
| TMP117 | 0x49 (0x48 with jumper) | easyC cable |
| SHTC3 | 0x70 | easyC cable, daisy-chained |

The easyC connector on the ConnectPlus uses SDA = IO21 and SCL = IO22.

## Slider potentiometer (setpoint)

The slider is a dual-gang potentiometer with two independent tracks. Only side A is used.

| Potentiometer | ConnectPlus | Note |
| --- | --- | --- |
| VCCA | 3V3 | **Not 5 V**: the wiper would put 5 V on the ADC pin |
| GND (side A) | GND | |
| OUTA | IO34 | ADC1_CH6, input only. ADC2 cannot be used while Wi-Fi is on |

## Smart LED WS2812B Stick10 (heating indicator)

Connect to the **DIN** side of the stick (DOUT is for chaining).

| Stick | ConnectPlus | Note |
| --- | --- | --- |
| VCC | VCC (5 V) | |
| GND | GND | Common ground is required |
| DIN | IO27 | Not a strapping pin, not used by I²C or the on-board RGB LED (IO32) |

The data line is driven at 3.3 V while the stick runs at 5 V, slightly below the WS2812B input-high threshold. With short wires this works in practice; if the LEDs flicker, add a level shifter. The firmware limits brightness to about 20 % to keep the current well within USB limits.

## Pins in use

| GPIO | Function |
| --- | --- |
| IO21, IO22 | I²C (easyC) |
| IO27 | WS2812B data |
| IO34 | Potentiometer (ADC1_CH6) |

All pins are configurable in `idf.py menuconfig` → RoomNode.
