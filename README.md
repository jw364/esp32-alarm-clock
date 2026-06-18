# ESP32 Bedside Alarm Clock

A fully self-contained bedside alarm clock built on an ESP32, featuring a large 7-segment time display, a 1.3" OLED secondary display, a real-time clock module, microSD audio playback over I2S, and a buzzer fallback tone — all controlled by three tactile push buttons.

> **Status:** Hardware-verified and running. No simulation code. Built against real components listed below.

---

## Table of Contents

- [Features](#features)
- [Hardware List](#hardware-list)
- [Wiring and Pin Assignments](#wiring-and-pin-assignments)
- [Library Requirements](#library-requirements)
- [Build Instructions](#build-instructions)
- [Upload Instructions](#upload-instructions)
- [First-Time RTC Setup](#first-time-rtc-setup)
- [SD Card Setup](#sd-card-setup)
- [Alarm Sound Setup](#alarm-sound-setup)
- [Usage](#usage)
- [OLED Display](#oled-display)
- [Serial Debug Output](#serial-debug-output)
- [Troubleshooting](#troubleshooting)
- [Future Roadmap](#future-roadmap)
- [License](#license)

---

## Features

| Feature | Detail |
|---|---|
| Time display | 12-hour format with blinking colon on Adafruit 1.2" HT16K33 7-segment |
| Secondary display | Day of week + alarm time/state on 1.3" SH1106 128x64 OLED |
| Real-time clock | DS3231 with battery backup — keeps time through power loss |
| Alarm audio | Plays `/alarm.wav` or `/alarm.mp3` from a FAT32 microSD card |
| Alarm fallback | Auto-generates an 880 Hz / 440 Hz alternating tone via I2S if no audio file |
| Alarm looping | Audio repeats until any button is pressed |
| Retrigger prevention | Alarm will not fire again within the same minute |
| Button control | Three tactile buttons: Hour+, Minute+, Alarm enable/disable |
| Serial debug | Detailed log at 115200 baud covering all subsystems |

---

## Hardware List

| Qty | Component | Notes |
|---|---|---|
| 1 | ESP32 38-pin DevKit (or equivalent) | Any 38-pin DevKit with VSPI/HSPI |
| 1 | Adafruit 1.2" 4-Digit 7-Segment Display with I2C Backpack | HT16K33 driver, I2C address 0x70 |
| 1 | Hosyond 1.3" I2C 128x64 SH1106 OLED Display (white) | I2C interface, 4-pin (VCC/GND/SDA/SCL) |
| 1 | DS3231 AT24C32 IIC RTC Module | DS3231 at 0x68; AT24C32 EEPROM at 0x57 (unused) |
| 1 | MicroSD card module | SPI interface, 3.3 V compatible |
| 1 | MicroSD card | FAT32 formatted, any size |
| 1 | NULLLAB I2S audio amplifier with speaker kit | NS4168 or compatible I2S Class D amp |
| 3 | Momentary push buttons (SPST NO) | Panel or breadboard mount |
| — | Jumper wires, breadboard or enclosure, power supply | |

---

## Wiring and Pin Assignments

### Complete Pin Table

| Function | Signal | GPIO | Direction |
|---|---|---|---|
| **I2C** (shared by 3 devices) | SDA | 21 | Bidirectional |
| **I2C** (shared by 3 devices) | SCL | 22 | Output |
| **Button** Hour+ | — | 32 | Input (PULLUP) |
| **Button** Minute+ | — | 33 | Input (PULLUP) |
| **Button** Alarm toggle | — | 25 | Input (PULLUP) |
| **I2S** BCLK | BCLK | 26 | Output |
| **I2S** Word Select | LRC/WS | 27 | Output |
| **I2S** Data | DIN | 14 | Output |
| **SD SPI** CS | CS | 5 | Output |
| **SD SPI** Clock | SCK | 18 | Output |
| **SD SPI** MISO | MISO | 23 | Input |
| **SD SPI** MOSI | MOSI | 13 | Output |

> No reserved flash pins (GPIO 6-11) are used. No input-only pins (GPIO 34-39) are used as outputs.
> GPIO 16, 17, 4, 15, and 19 are free for future expansion.

---

### I2C Bus — Adafruit 7-Segment + DS3231 RTC + SH1106 OLED

All three I2C devices share the same two wires. Wire each device's SDA and SCL to GPIO 21 and 22 respectively.

```
ESP32 GPIO 21 (SDA) ──+── HT16K33 SDA
                       +── DS3231  SDA
                       +── SH1106  SDA

ESP32 GPIO 22 (SCL) ──+── HT16K33 SCL
                       +── DS3231  SCL
                       +── SH1106  SCL
```

**I2C addresses — no conflicts:**

| Device | Address |
|---|---|
| SH1106 OLED (Hosyond 1.3") | `0x3C` (try `0x3D` if init fails) |
| AT24C32 EEPROM (on RTC module, unused) | `0x57` |
| DS3231 RTC | `0x68` |
| HT16K33 7-seg backpack | `0x70` |

The AT24C32 EEPROM is soldered onto the DS3231 module and sits on the bus harmlessly — this sketch never addresses `0x57`.

The I2C bus runs at **400 kHz** (fast-mode). All three active devices (SH1106, DS3231, HT16K33) support 400 kHz.

> **Pull-up resistors:** Each I2C module typically includes on-board 4.7 kOhm pull-up resistors. Three sets in parallel gives approximately 1.6 kOhm effective pull-up — fine at 400 kHz. If I2C errors appear, desolder pull-ups from one or two modules.

### Hosyond 1.3" I2C SH1106 OLED

The OLED is a standard 4-pin I2C module. Connect it to the shared I2C bus — no extra GPIO pins required.

```
ESP32 GPIO 21 (SDA) ──── OLED SDA
ESP32 GPIO 22 (SCL) ──── OLED SCL
3.3 V               ──── OLED VCC
GND                 ──── OLED GND
```

---

### MicroSD Card Module — Hardware SPI (Remapped VSPI)

The sketch uses the ESP32 VSPI peripheral with custom pin remapping via `SPIClass`.

```
ESP32           MicroSD Module
GPIO  5 ──────── CS
GPIO 18 ──────── SCK
GPIO 23 ──────── MISO
GPIO 13 ──────── MOSI
3.3 V   ──────── VCC   (check module label — some require 5 V with onboard regulator)
GND     ──────── GND
```

> **Note on GPIO 5:** GPIO 5 outputs a brief PWM signal at boot (ESP32 strapping pin behaviour). This can occasionally cause the SD `begin()` call to fail on the very first attempt. The sketch detects this and falls back to the backup tone. Power-cycle the board if it happens at runtime.

---

### NULLLAB I2S Amplifier

```
ESP32           NULLLAB Amp
GPIO 26 ──────── BCLK
GPIO 27 ──────── LRC   (also labelled WS on some amps)
GPIO 14 ──────── DIN
5 V     ──────── VIN   (check your kit — some accept 3.3 V)
GND     ──────── GND
Speaker outputs to speaker terminals
```

> The NULLLAB kit's I2S amp (typically NS4168 or compatible) is a mono Class D amplifier. The sketch outputs stereo-interleaved I2S data; the amp uses the left channel by default. No MCLK or GAIN pin configuration is required.

---

### Push Buttons

Wire one leg of each button to the GPIO pin listed below. Wire the other leg to **GND**. The sketch uses `INPUT_PULLUP`, so no external resistors are needed.

```
ESP32 GPIO 32 ──── [Hour+  button] ──── GND
ESP32 GPIO 33 ──── [Minute+ button] ─── GND
ESP32 GPIO 25 ──── [Alarm toggle]  ──── GND
```

---

## Library Requirements

Install all libraries before opening the sketch in Arduino IDE.

| Library | Install via | Purpose |
|---|---|---|
| **RTClib** (Adafruit) | Arduino Library Manager | DS3231 real-time clock |
| **Adafruit GFX Library** | Arduino Library Manager | Required by LED Backpack |
| **Adafruit LED Backpack Library** | Arduino Library Manager | HT16K33 7-segment display |
| **U8g2** (Oliver Krause) | Arduino Library Manager | SH1106 OLED display |
| **ESP32-audioI2S** (schreibfaul1) | GitHub ZIP — see below | WAV/MP3 playback + I2S |
| **SD** | Built-in (ESP32 Arduino core) | MicroSD file access |

### Installing ESP32-audioI2S from GitHub

This library is not in the Arduino Library Manager:

1. Open [github.com/schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)
2. Click **Code -> Download ZIP**
3. In Arduino IDE: **Sketch -> Include Library -> Add .ZIP Library...**
4. Select the downloaded ZIP

---

## Build Instructions

1. **Install the ESP32 Arduino core** via Boards Manager:
   - In Arduino IDE: **File -> Preferences -> Additional boards manager URLs**
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Open **Tools -> Board -> Boards Manager**, search `esp32`, install **esp32 by Espressif Systems**

2. **Install all six libraries** listed in [Library Requirements](#library-requirements)

3. **Open** `alarm_clock.ino` in Arduino IDE

4. **Select your board:** Tools -> Board -> **ESP32 Arduino -> ESP32 Dev Module**

5. **Board settings:**

   | Setting | Value |
   |---|---|
   | Board | ESP32 Dev Module |
   | Upload Speed | 921600 |
   | CPU Frequency | 240 MHz |
   | Flash Frequency | 80 MHz |
   | Flash Mode | QIO |
   | Flash Size | 4MB (32Mb) |
   | Partition Scheme | Default 4MB with spiffs |
   | Core Debug Level | None |
   | PSRAM | Disabled |

6. **Verify (compile) first** with the checkmark button before connecting hardware. Fix any library-not-found errors before proceeding.

---

## Upload Instructions

1. Connect the ESP32 to your PC via USB
2. Select the correct **Port** under Tools -> Port (e.g. `COM3` on Windows, `/dev/ttyUSB0` on Linux)
3. Click the **Upload** button (right arrow)
4. If upload fails with "Failed to connect":
   - Hold the **BOOT** button on the ESP32 while clicking Upload
   - Release BOOT once `Connecting...` appears in the output console
5. After upload, open **Tools -> Serial Monitor** at **115200 baud** to see boot output

---

## First-Time RTC Setup

The DS3231 retains time through power loss using its coin-cell battery. If the module is brand new or the battery is dead, it will report wrong time.

**To set the time:**

1. In `alarm_clock.ino`, find `setupRTC()` and uncomment this line:
   ```cpp
   rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
   ```
2. **Upload** the sketch — this sets the RTC to your PC's compile time
3. **Immediately re-comment** that line:
   ```cpp
   // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
   ```
4. **Upload again** — the sketch now runs with the time locked in

> The RTC keeps time as long as the coin cell (typically CR2032) has charge. Battery life is typically 5-10 years.

---

## SD Card Setup

1. **Format** the microSD card as **FAT32** (not exFAT, not NTFS)
   - Windows: right-click the drive -> Format -> FAT32
   - macOS: Disk Utility -> Erase -> MS-DOS (FAT)
2. **Place audio files at the root** of the card (not inside any folder):
   - `/alarm.wav` — preferred; plays first if found
   - `/alarm.mp3` — plays if WAV not found
3. Insert the card into the module before powering on

The sketch reports SD status on boot:
```
[SD]    Initializing MicroSD (CS=5 SCK=18 MISO=23 MOSI=13) ... OK  (card: 7812 MB)
[SD]    /alarm.wav: FOUND       /alarm.mp3: not found
```

---

## Alarm Sound Setup

The sketch supports two audio formats on the SD card:

| File | Format requirements |
|---|---|
| `/alarm.wav` | PCM WAV, 8-48 kHz, mono or stereo, 16-bit |
| `/alarm.mp3` | MP3, any standard bitrate |

WAV is tried first. The alarm loops automatically until a button is pressed.

**If no audio file is present or the SD card fails to initialize**, the sketch generates a backup tone directly via the ESP32 I2S peripheral:
- 880 Hz for 500 ms, then 440 Hz for 500 ms, repeating
- Automatically stops after 30 seconds
- No extra hardware required — the same I2S amp output is used

---

## Usage

| Button | Normal mode | While alarm is sounding |
|---|---|---|
| **Hour+** | Increments alarm hour by 1 (wraps 23 to 0) | Stops the alarm |
| **Minute+** | Increments alarm minute by 1 (wraps 59 to 0) | Stops the alarm |
| **Alarm** | Toggles alarm ON / OFF | Stops the alarm |

- Alarm time is displayed on the OLED and in Serial output
- Once the alarm triggers, it will not re-trigger until the minute rolls over
- All button presses immediately refresh the OLED display

---

## OLED Display

The Hosyond 1.3" SH1106 OLED shows four rows of information on its 128x64 pixel screen:

```
+------------------------+
| WEDNESDAY              |  <- Full day name (9x18 bold font)
| Jun 18, 2026           |  <- Date from RTC (6x10 font)
|------------------------|  <- Separator line
| ALM 7:00am ON          |  <- Alarm time and state (6x10 font)
| ** ALARM! Press button |  <- Shown only when alarm is sounding
+------------------------+
```

The OLED refreshes:
- Every **1 second** (from the RTC poll — updates time, date, and alarm state)
- **Immediately** when Hour+, Minute+, or Alarm buttons are pressed
- **Immediately** when the alarm starts or stops

The OLED shares the I2C bus (GPIO 21/22) with the DS3231 and HT16K33. No additional GPIO pins are used.

**Planned future displays** (not yet implemented — see [Future Roadmap](#future-roadmap)):
- Weather information (requires Wi-Fi + weather API)
- Wi-Fi connection status
- System messages

---

## Serial Debug Output

Connect a serial monitor at **115200 baud** to see detailed boot and runtime logs.

**Boot sequence example:**
```
=============================================
   ESP32 Bedside Alarm Clock
=============================================
[RTC]   Initializing DS3231 ... OK
[RTC]   Current time: 2026-06-18 07:32:00  DoW=3(WED)
[DISP]  Initializing Adafruit HT16K33 7-seg (0x70) ... OK
[OLED]  Initializing SH1106 1.3" 128x64 SW-SPI ... OK
[SD]    Initializing MicroSD (CS=5 SCK=18 MISO=23 MOSI=13) ... OK  (card: 7812 MB)
[SD]    /alarm.wav: FOUND       /alarm.mp3: not found
[AUDIO] Initializing I2S (BCLK=26 LRC=27 DIN=14) ... OK  (volume 18/21)
[BTN]   Hour=GPIO32  Minute=GPIO33  AlarmToggle=GPIO25
---------------------------------------------
[BOOT]  Ready.
```

**Runtime heartbeat (every 10 seconds):**
```
[STAT]  07:45  WED  | alarm ON @ 08:00 (8:00 AM)
```

---

## Troubleshooting

### 7-segment display shows nothing or garbage
- Check I2C wiring: SDA=21, SCL=22
- Confirm the backpack address jumpers are not bridged (default address is 0x70)
- Run an I2C scanner sketch to detect all devices on the bus

### OLED shows nothing
- Check I2C wiring: SDA=21, SCL=22 (shares the bus with DS3231 and HT16K33)
- The Hosyond module is 3.3 V only — do not apply 5 V to VCC
- Run an I2C scanner sketch to confirm the OLED responds at address `0x3C`
- If the OLED is at `0x3D` instead, change `OLED_I2C_ADDR` from `0x3C` to `0x3D` in the pin definitions
- Verify no other device on the bus has address 0x3C (see address table above)

### RTC shows wrong time or prints "RTC lost power"
- Follow the [First-Time RTC Setup](#first-time-rtc-setup) procedure
- Verify the CR2032 coin cell is inserted and has charge (>2.0 V)

### SD card not detected
- Confirm FAT32 format (not exFAT)
- Check SPI wiring: CS=5, SCK=18, MISO=23, MOSI=13
- Power the SD module from 3.3 V
- If `begin()` fails intermittently at boot, this is the GPIO 5 strapping glitch — power-cycle the board

### No audio from speaker
- Confirm I2S wiring: BCLK=26, LRC=27, DIN=14
- Check amplifier VCC: most NULLLAB NS4168 kits require 5 V input for full volume
- Test amp wiring by removing the SD card — the backup tone will play if I2S is wired correctly
- Check Serial for `[ALARM] Playing /alarm.wav` — if that line appears but there is no sound, the amp wiring or power is the issue

### Alarm does not trigger
- Press the Alarm toggle button until the OLED shows `ALM ... ON`
- Verify RTC time is correct (see First-Time RTC Setup)
- Check Serial for `[ALARM] TRIGGER` at the expected time

### Backup tone plays instead of audio file
- Confirm `/alarm.wav` or `/alarm.mp3` exists at the SD card root (not in a subfolder)
- Check boot log for `FOUND` next to the filename
- Ensure the WAV file is uncompressed PCM format (not ADPCM or other compressed variants)

### Compiler warnings about `i2s_write` or `driver/i2s.h`
- These appear on Arduino ESP32 core 3.x (ESP-IDF v5) and are deprecation warnings
- The code compiles and runs correctly despite the warnings
- Downgrade to ESP32 Arduino core 2.x to eliminate them if preferred

---

## Future Roadmap

**Alarm & display:**
- [ ] Persist alarm settings to AT24C32 EEPROM or ESP32 NVS so they survive power loss
- [ ] Multiple alarm slots with day-of-week scheduling
- [ ] Brightness control for 7-segment display (auto-dim at night via photoresistor)
- [ ] OLED brightness auto-dim on time schedule
- [ ] Volume adjustment via long button press

**OLED extra screens (requires Wi-Fi):**
- [ ] Weather information — current conditions + forecast via OpenWeatherMap API
- [ ] Wi-Fi connection status indicator
- [ ] System messages and boot status
- [ ] Scrolling display when content exceeds one screen

**Connectivity:**
- [ ] Network time sync (NTP) via Wi-Fi as a RTC cross-check
- [ ] Web interface for alarm configuration via Wi-Fi Access Point
- [ ] OTA firmware updates over Wi-Fi

**Hardware:**
- [ ] 3D-printed enclosure to mount all modules
- [ ] Custom PCB to replace breadboard wiring

---

## Wiring Diagram

> *Schematic placeholder — a Fritzing or KiCad diagram will be added here.*

---

## Photos

> *Photo placeholder — photos of the assembled hardware will be added here.*

---

## License

MIT
