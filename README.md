# ESP32 Alarm Clock

A desk alarm clock built on an ESP32. It shows the current time (synced over WiFi), the room temperature from an LM35DZ sensor, and an alarm that flashes the screen and sounds a passive piezo buzzer.

## Features

- Digital clock with hours, minutes, seconds and date, synced over NTP (Tehran time, UTC+3:30)
- Room temperature from an LM35DZ, shown on the clock screen
- Alarm with ON/OFF, 5-minute snooze, and stop
- Alarm time is set on the device with two buttons
- Louder buzzer using "bridge drive" (two GPIO pins, no transistor needed)

## Hardware

| Part | Notes |
|---|---|
| ESP32 DevKit (WROOM) | Any ESP32 board with the same pinout works |
| 0.96" OLED, SSD1306, I2C, 128×64 | Two-color (yellow top band, blue bottom) |
| LM35DZ temperature sensor | Use 5V power |
| Passive piezo buzzer | A second one can be wired in parallel for more volume |
| 2 tactile pushbuttons (4 legs) | |
| Breadboard and jumper wires | |
| 220 Ω resistor | Optional, in series with the buzzer |
| 100 nF ceramic capacitor | Optional, across the LM35DZ power legs |

## Wiring

**OLED (I2C)**

| OLED | ESP32 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | GPIO33 |
| SCL | GPIO25 |

**Buttons** (no external resistors; the code uses internal pull-ups)

| Button | ESP32 | Other leg |
|---|---|---|
| MODE | GPIO4 | GND |
| UP | GPIO13 | GND |

A 4-leg tactile button has two pairs of legs, and pressing connects the two pairs. The GPIO wire and the GND wire must go to legs from **different** pairs. Use a multimeter in continuity mode to find the pairs: touching two legs of the same pair beeps without pressing.

**Passive buzzer (bridge drive)**

| Buzzer | ESP32 |
|---|---|
| (+) | GPIO26 (through a 220 Ω resistor, optional) |
| (−) | GPIO27 |

**LM35DZ** (hold the flat side toward you, legs pointing down)

| Leg | ESP32 |
|---|---|
| Left (+Vs) | 5V (VIN) |
| Middle (Vout) | GPIO34 |
| Right (GND) | GND |

Reversing the left and right legs can damage the sensor. Keep it a few centimeters away from the ESP32, because the board heats up and skews the reading.

## Software setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) (version 2 or later).
2. Add ESP32 board support. Open **Boards Manager**, search for `esp32` by Espressif, and install it.
3. Install the display libraries from **Library Manager**: `Adafruit SSD1306` and `Adafruit GFX`. Accept the extra dependencies when asked.
4. Select the board **ESP32 Dev Module** and the correct COM port. If the port doesn't appear, install the CH340 driver.
5. Open `alarm_clock/alarm_clock.ino`, then set `WIFI_SSID` and `WIFI_PASS` to your network. The ESP32 only works with 2.4 GHz WiFi.
6. Upload the sketch.

## Using the clock

**Clock screen:** the top line shows the temperature, the middle shows the time, and the bottom shows the date.

**Setting the alarm:**

| Button | Clock screen | Setting the alarm | While the alarm rings |
|---|---|---|---|
| MODE | Starts setting: hour → minute → ON/OFF → back to clock | Next step | Snooze for 5 minutes |
| UP | Nothing | +1 hour, +1 minute, or toggle ON/OFF, depending on the step | Stop the alarm and turn it off |

The field being set blinks.

The alarm starts OFF. Turn it on in the ON/OFF step. While a snooze is running, the set screens show the snooze time. A snooze ring shows the time it was scheduled for, such as 15:05. The first ring shows the alarm time.

The alarm rings once a day at the set time. When you finish setting, the current minute is treated as already handled, so setting the alarm to the current minute won't make it ring immediately.

## Calibrating the temperature

The LM35DZ outputs 10 mV per °C. Every second the sketch takes 31 readings and uses the middle value, which ignores spikes.

1. Open the Serial Monitor at 115200 baud. You'll see the millivolts and the Celsius value every second.
2. Compare the reading with a room thermometer.
3. Add the difference (thermometer minus sketch) to `TEMP_OFFSET`. For example, if the sketch shows 26 °C and the thermometer shows 22 °C, the difference is -4, so change `TEMP_OFFSET` to `-9.5` (it is currently `-5.5`).

## Troubleshooting

- **Blank OLED:** Check that SDA and SCL are not swapped. If it's still blank, change `0x3C` to `0x3D` in `display.begin()`.
- **"WiFi failed" on the screen:** Check the network name and password, and make sure the network is 2.4 GHz.
- **Temperature is wrong:** Make sure the sensor is on 5V, not 3.3V. Move it away from the board, then calibrate with `TEMP_OFFSET`.
- **Buttons respond wrong or not at all:** The GPIO wire and the GND wire must be on different pairs of legs.
- **Alarm is quiet:** Use bridge wiring (GPIO26 and GPIO27) and mount the buzzer on a box or cup. You can also change `ALARM_FREQ_LOW` and `ALARM_FREQ_HIGH` by a few hundred Hz to find a louder tone.
- **Stuck on "Connecting WiFi...":** The clock needs WiFi at startup to get the time. The sketch keeps retrying until it connects, so check the network name and password.

## Project layout

```
alarm_clock/alarm_clock.ino          Main sketch
README.md
.gitignore
```
