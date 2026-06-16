# ESP32 Bedside Alarm Clock

An Arduino-based alarm clock for the ESP32 with real-time clock, dual 7-segment displays, SD card music playback over I2S, and a fallback backup tone.

## Features

- 12-hour time display with blinking colon (Adafruit HT16K33 7-segment)
- Day-of-week display — SUN / MON / TUE / WED / THU / FRI / SAT (TM1637 7-segment)
- Real-time clock via DS3231 over I2C
- Alarm time set with two push buttons (hour / minute)
- Alarm enable/disable toggle button
- Plays `/alarm.wav` or `/alarm.mp3` from a microSD card over I2S on trigger
- Automatic fallback to a generated 880 Hz / 440 Hz alternating tone if no audio file is found
- Alarm loops until any button is pressed
- Retrigger prevention — alarm won't fire again within the same minute
- Serial debug output at 115200 baud for all subsystems

## Hardware

| Component | Part |
|---|---|
| Microcontroller | ESP32 (38-pin DevKit or equivalent) |
| Main time display | Adafruit 1.2" 4-Digit 7-Segment Display with I2C Backpack (HT16K33) |
| Day-of-week display | DIYables TM1637 4-Digit 7-Segment Display |
| Real-time clock | DS3231 RTC module |
| Audio amplifier | NS4168 or MAX98357A I2S amplifier + speaker |
| Storage | MicroSD card module + FAT32-formatted card |
| Buttons | 3× momentary push buttons |

## Wiring

### I2C — Adafruit 7-Seg + DS3231
| Signal | GPIO |
|---|---|
| SDA | 21 |
| SCL | 22 |

### TM1637 Day Display
| Signal | GPIO |
|---|---|
| CLK | 16 |
| DIO | 17 |

### Push Buttons (other terminal → GND)
| Button | GPIO |
|---|---|
| Hour + | 32 |
| Minute + | 33 |
| Alarm On/Off | 25 |

### I2S Amplifier
| Signal | GPIO |
|---|---|
| BCLK | 26 |
| LRC (WS) | 27 |
| DIN | 14 |

### MicroSD (SPI)
| Signal | GPIO |
|---|---|
| CS | 5 |
| SCK | 18 |
| MISO | 23 |
| MOSI | 13 |

## Required Libraries

Install via Arduino Library Manager unless noted:

| Library | Source |
|---|---|
| RTClib | Adafruit (Library Manager) |
| Adafruit GFX Library | Adafruit (Library Manager) |
| Adafruit LED Backpack Library | Adafruit (Library Manager) |
| TM1637Display | Avishay Orpaz (Library Manager) |
| ESP32-audioI2S | [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) (GitHub) |
| SD | Built-in with ESP32 Arduino core |

## Setup

1. **Format the SD card** as FAT32 and place your alarm audio at the root as `/alarm.wav` and/or `/alarm.mp3`.

2. **Set the RTC time** — if the serial monitor prints `RTC lost power`, uncomment this line in `setupRTC()`, upload once, then comment it out and upload again:
   ```cpp
   // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
   ```

3. **Install ESP32-audioI2S** from GitHub (not in Library Manager). Download the ZIP and add via *Sketch → Include Library → Add .ZIP Library*.

4. Select **ESP32 Dev Module** (or your board) in Arduino IDE, set upload speed to 921600, and flash.

## Usage

| Button | Action |
|---|---|
| Hour | Increments alarm hour (cycles 0–23) |
| Minute | Increments alarm minute (cycles 0–59) |
| Alarm | Toggles alarm on / off |
| Any button (while alarm sounds) | Stops the alarm |

Current time and alarm state are printed to Serial at 115200 baud every 10 seconds.

## Audio Fallback

If the SD card is missing or neither `/alarm.wav` nor `/alarm.mp3` is found, a backup tone is generated directly via the ESP32 I2S peripheral — no extra hardware required. The tone alternates between 880 Hz and 440 Hz and auto-stops after 30 seconds.

## License

MIT
