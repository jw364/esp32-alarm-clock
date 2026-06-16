/*
 * ESP32 Bedside Alarm Clock
 * ============================================================
 * WIRING GUIDE
 * ============================================================
 *
 * I2C Bus — Adafruit HT16K33 7-Seg (0x70) + DS3231 RTC (0x68):
 *   SDA         → GPIO 21
 *   SCL         → GPIO 22
 *   (both devices share the same I2C bus)
 *
 * TM1637 4-Digit 7-Seg (Day of Week):
 *   CLK         → GPIO 16
 *   DIO         → GPIO 17
 *   VCC         → 3.3 V or 5 V
 *   GND         → GND
 *
 * Push Buttons (other terminal → GND; INPUT_PULLUP used):
 *   Hour +      → GPIO 32
 *   Minute +    → GPIO 33
 *   Alarm On/Off→ GPIO 25
 *
 * I2S Amplifier (NS4168 / MAX98357A):
 *   BCLK        → GPIO 26
 *   LRC (WS)    → GPIO 27
 *   DIN         → GPIO 14
 *   VCC         → 5 V (check amp datasheet)
 *   GND         → GND
 *
 * MicroSD Card Module (SPI — VSPI bus, custom pins):
 *   CS          → GPIO 5
 *   SCK         → GPIO 18
 *   MISO        → GPIO 23
 *   MOSI        → GPIO 13
 *   VCC         → 3.3 V
 *   GND         → GND
 *
 * SD card must contain /alarm.wav and/or /alarm.mp3 at root.
 *
 * ============================================================
 * REQUIRED LIBRARIES (Arduino Library Manager unless noted)
 * ============================================================
 *   RTClib                  — Adafruit
 *   Adafruit GFX Library    — Adafruit
 *   Adafruit LED Backpack   — Adafruit
 *   TM1637Display           — Avishay Orpaz
 *   ESP32-audioI2S          — schreibfaul1 (GitHub)
 *   SD                      — built-in with ESP32 Arduino core
 * ============================================================
 */

#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <TM1637Display.h>
#include <SD.h>
#include <SPI.h>
#include <Audio.h>          // ESP32-audioI2S by schreibfaul1
#include "driver/i2s.h"     // Direct I2S access for backup tone

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define PIN_SDA            21
#define PIN_SCL            22

#define PIN_TM1637_CLK     16
#define PIN_TM1637_DIO     17

#define PIN_BTN_HOUR       32
#define PIN_BTN_MINUTE     33
#define PIN_BTN_ALARM      25

#define PIN_I2S_BCLK       26
#define PIN_I2S_LRC        27
#define PIN_I2S_DIN        14

#define PIN_SD_CS          5
#define PIN_SD_SCK         18
#define PIN_SD_MISO        23
#define PIN_SD_MOSI        13

// ============================================================
//  CONSTANTS
// ============================================================
#define DEBOUNCE_MS              50
#define RTC_POLL_MS              1000
#define COLON_BLINK_MS           500
#define AUDIO_VOLUME             18      // 0–21
#define BACKUP_TONE_FREQ_HZ      880     // A5, attention-grabbing
#define BACKUP_SAMPLE_RATE       16000
#define BACKUP_TONE_AMPLITUDE    10000   // 16-bit signed; 10000 ≈ 61% full scale
#define BACKUP_TONE_MAX_MS       30000   // auto-stop backup tone after 30 s
#define STATUS_PRINT_MS          10000   // serial heartbeat interval

// ============================================================
//  TM1637 SEGMENT PATTERNS FOR DAY NAMES
//
//  Bit order per TM1637Display library:
//    bit 0 = A (top)        bit 4 = E (bottom-left)
//    bit 1 = B (top-right)  bit 5 = F (top-left)
//    bit 2 = C (bot-right)  bit 6 = G (middle)
//    bit 3 = D (bottom)     bit 7 = decimal point (unused)
//
//  Notes:
//    M (MON) is approximated — 7-segment cannot render it perfectly.
//    W (WED) is approximated as a "u" shape (bottom arc).
// ============================================================
static const uint8_t DAY_SEG[7][4] = {
  { 0x6D, 0x3E, 0x54, 0x00 },  // 0 SUN  S=0x6D  U=0x3E  n=0x54
  { 0x37, 0x3F, 0x54, 0x00 },  // 1 MON  M=0x37* O=0x3F  n=0x54
  { 0x78, 0x3E, 0x79, 0x00 },  // 2 TUE  t=0x78  U=0x3E  E=0x79
  { 0x1C, 0x79, 0x5E, 0x00 },  // 3 WED  W=0x1C* E=0x79  d=0x5E
  { 0x78, 0x76, 0x3E, 0x00 },  // 4 THU  t=0x78  H=0x76  U=0x3E
  { 0x71, 0x50, 0x06, 0x00 },  // 5 FRI  F=0x71  r=0x50  I=0x06
  { 0x6D, 0x77, 0x78, 0x00 },  // 6 SAT  S=0x6D  A=0x77  t=0x78
};
static const char* const DAY_NAMES[7] = {
  "SUN","MON","TUE","WED","THU","FRI","SAT"
};

// ============================================================
//  OBJECTS
// ============================================================
RTC_DS3231         rtc;
Adafruit_7segment  timeDisp;              // I2C 0x70
TM1637Display      dayDisp(PIN_TM1637_CLK, PIN_TM1637_DIO);
Audio              audio;
SPIClass           sdSPI(VSPI);

// ============================================================
//  ALARM & CLOCK STATE
// ============================================================
uint8_t  alarmHour        = 7;    // 24-hour, 0–23
uint8_t  alarmMinute      = 0;    // 0–59
bool     alarmEnabled     = false;
bool     alarmPlaying     = false;
bool     alarmFired       = false; // prevents re-trigger within same minute
bool     sdAvailable      = false;
bool     audioFilePlaying = false; // true while Audio lib has active stream
volatile bool audioNeedsRestart = false; // set by EOF callback, consumed in loop()

uint8_t  nowHour          = 0;
uint8_t  nowMinute        = 0;
uint8_t  nowDay           = 0;    // 0=Sun … 6=Sat
int8_t   prevMinute       = -1;
int8_t   prevDay          = -1;
bool     colonOn          = false;

uint32_t lastRTCPoll      = 0;
uint32_t lastColonBlink   = 0;
uint32_t lastStatusPrint  = 0;

// ============================================================
//  BACKUP TONE (FreeRTOS task writes square wave to I2S)
// ============================================================
TaskHandle_t         backupToneHandle = NULL;
volatile bool        backupToneStop   = false;
uint32_t             backupToneBegin  = 0;

// ============================================================
//  BUTTON DEBOUNCE
// ============================================================
struct Button {
    uint8_t  pin;
    bool     raw;
    bool     stable;
    uint32_t changeTime;
    bool     edge;   // true for exactly one loop() iteration on press
};

Button bHour   = { PIN_BTN_HOUR,   true, true, 0, false };
Button bMinute = { PIN_BTN_MINUTE, true, true, 0, false };
Button bAlarm  = { PIN_BTN_ALARM,  true, true, 0, false };

// ============================================================
//  FUNCTION PROTOTYPES
// ============================================================
void    setupRTC();
void    setupDisplay();
void    setupTM1637();
void    setupSD();
void    setupAudio();
void    readButtons();
void    updateTimeDisplay();
void    updateDayDisplay();
void    checkAlarm();
void    playAlarm();
void    stopAlarm();
void    debounce(Button &b);
bool    tryPlayAudioFile();
void    restartAudioFile();
void    startBackupTone();
void    stopBackupTone();
void    backupToneTaskFn(void* pv);
uint8_t to12Hr(uint8_t h24);
bool    isAM(uint8_t h24);

// ============================================================
//  SETUP FUNCTIONS
// ============================================================

void setupRTC() {
    Serial.print("[RTC]   Initializing DS3231 ... ");
    if (!rtc.begin()) {
        Serial.println("FAILED — check SDA=21 SCL=22. Halting.");
        while (true) { delay(1000); }
    }
    Serial.println("OK");

    if (rtc.lostPower()) {
        Serial.println("[RTC]   WARNING: RTC lost power — time may be wrong.");
        Serial.println("[RTC]   Uncomment rtc.adjust() below, upload once, then re-comment.");
        // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    DateTime t = rtc.now();
    Serial.printf("[RTC]   Current time: %04d-%02d-%02d %02d:%02d:%02d  DoW=%d(%s)\n",
                  t.year(), t.month(), t.day(),
                  t.hour(), t.minute(), t.second(),
                  t.dayOfTheWeek(), DAY_NAMES[t.dayOfTheWeek()]);
}

void setupDisplay() {
    Serial.print("[DISP]  Initializing Adafruit HT16K33 7-seg (0x70) ... ");
    timeDisp.begin(0x70);
    timeDisp.setBrightness(10); // 0–15
    // Show "----" on boot so the user knows it's alive
    for (uint8_t pos : {0, 1, 3, 4}) timeDisp.writeDigitRaw(pos, 0x40); // middle seg
    timeDisp.drawColon(true);
    timeDisp.writeDisplay();
    Serial.println("OK");
}

void setupTM1637() {
    Serial.print("[TM16]  Initializing TM1637 (CLK=16 DIO=17) ... ");
    dayDisp.setBrightness(5); // 0–7
    dayDisp.setSegments(DAY_SEG[0]); // SUN as power-on test
    Serial.println("OK");
}

void setupSD() {
    Serial.print("[SD]    Initializing MicroSD (CS=5 SCK=18 MISO=23 MOSI=13) ... ");
    sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    if (!SD.begin(PIN_SD_CS, sdSPI)) {
        Serial.println("FAILED — backup tone will be used if alarm triggers.");
        sdAvailable = false;
        return;
    }
    sdAvailable = true;
    Serial.printf("OK  (card: %llu MB)\n", SD.cardSize() / (1024ULL * 1024ULL));

    bool hasWav = SD.exists("/alarm.wav");
    bool hasMp3 = SD.exists("/alarm.mp3");
    Serial.printf("[SD]    /alarm.wav: %-9s  /alarm.mp3: %s\n",
                  hasWav ? "FOUND" : "not found",
                  hasMp3 ? "FOUND" : "not found");

    if (!hasWav && !hasMp3) {
        Serial.println("[SD]    WARNING: no alarm audio file found. Backup tone will be used.");
    }
}

void setupAudio() {
    Serial.print("[AUDIO] Initializing I2S (BCLK=26 LRC=27 DIN=14) ... ");
    audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
    audio.setVolume(AUDIO_VOLUME);
    Serial.printf("OK  (volume %d/21)\n", AUDIO_VOLUME);
}

// ============================================================
//  UTILITY
// ============================================================

uint8_t to12Hr(uint8_t h24) {
    if (h24 == 0)  return 12;
    if (h24 <= 12) return h24;
    return h24 - 12;
}

bool isAM(uint8_t h24) { return h24 < 12; }

void debounce(Button &b) {
    bool raw = digitalRead(b.pin);
    b.edge = false;

    if (raw != b.raw) {
        b.raw        = raw;
        b.changeTime = millis();
    }

    if (millis() - b.changeTime > DEBOUNCE_MS && b.stable != b.raw) {
        b.stable = b.raw;
        if (b.stable == LOW) {  // falling edge = confirmed press
            b.edge = true;
        }
    }
}

// ============================================================
//  READ BUTTONS
// ============================================================
void readButtons() {
    debounce(bHour);
    debounce(bMinute);
    debounce(bAlarm);

    // Any button press while alarm is sounding stops it
    if (alarmPlaying && (bHour.edge || bMinute.edge || bAlarm.edge)) {
        Serial.println("[BTN]   Button pressed — stopping alarm.");
        stopAlarm();
        return;
    }

    if (bHour.edge) {
        alarmHour = (alarmHour + 1) % 24;
        Serial.printf("[BTN]   Alarm hour   → %02d:xx  (%d %s)\n",
                      alarmHour, to12Hr(alarmHour), isAM(alarmHour) ? "AM" : "PM");
    }

    if (bMinute.edge) {
        alarmMinute = (alarmMinute + 1) % 60;
        Serial.printf("[BTN]   Alarm minute → xx:%02d\n", alarmMinute);
    }

    if (bAlarm.edge) {
        alarmEnabled = !alarmEnabled;
        Serial.printf("[BTN]   Alarm %s — set for %02d:%02d  (%d:%02d %s)\n",
                      alarmEnabled ? "ENABLED " : "DISABLED",
                      alarmHour, alarmMinute,
                      to12Hr(alarmHour), alarmMinute,
                      isAM(alarmHour) ? "AM" : "PM");
    }
}

// ============================================================
//  UPDATE TIME DISPLAY  (Adafruit HT16K33)
//  Layout: [H][H] : [M][M]   leading zero suppressed on hour
// ============================================================
void updateTimeDisplay() {
    uint8_t h = to12Hr(nowHour);
    uint8_t m = nowMinute;

    // Positions: 0=tens-H  1=ones-H  (2=colon)  3=tens-M  4=ones-M
    if (h / 10 == 0) {
        timeDisp.writeDigitRaw(0, 0x00);  // blank leading zero
    } else {
        timeDisp.writeDigitNum(0, h / 10, false);
    }
    timeDisp.writeDigitNum(1, h % 10, false);
    timeDisp.drawColon(colonOn);
    timeDisp.writeDigitNum(3, m / 10, false);
    timeDisp.writeDigitNum(4, m % 10, false);
    timeDisp.writeDisplay();
}

// ============================================================
//  UPDATE DAY DISPLAY  (TM1637)
//  Only redraws when day changes to reduce I2C traffic
// ============================================================
void updateDayDisplay() {
    if (nowDay == (uint8_t)prevDay) return;
    prevDay = (int8_t)nowDay;

    dayDisp.setSegments(DAY_SEG[nowDay % 7]);
    Serial.printf("[TM16]  Day display → %s\n", DAY_NAMES[nowDay % 7]);
}

// ============================================================
//  CHECK ALARM
// ============================================================
void checkAlarm() {
    if (!alarmEnabled || alarmFired || alarmPlaying) return;

    if (nowHour == alarmHour && nowMinute == alarmMinute) {
        Serial.printf("[ALARM] TRIGGER — %02d:%02d matches alarm %02d:%02d\n",
                      nowHour, nowMinute, alarmHour, alarmMinute);
        alarmFired = true;
        playAlarm();
    }
}

// ============================================================
//  PLAY ALARM
//  Tries /alarm.wav then /alarm.mp3; falls back to backup tone
// ============================================================
void playAlarm() {
    alarmPlaying = true;
    audioNeedsRestart = false;

    if (sdAvailable && tryPlayAudioFile()) return;

    Serial.println("[ALARM] No audio file available — starting backup tone.");
    audioFilePlaying = false;
    if (backupToneHandle == NULL) {
        startBackupTone();
    }
}

bool tryPlayAudioFile() {
    if (SD.exists("/alarm.wav")) {
        Serial.println("[ALARM] Connecting to /alarm.wav ...");
        if (audio.connecttoFS(SD, "/alarm.wav")) {
            audioFilePlaying = true;
            Serial.println("[ALARM] Playing /alarm.wav");
            return true;
        }
        Serial.println("[ALARM] connecttoFS failed for /alarm.wav");
    }
    if (SD.exists("/alarm.mp3")) {
        Serial.println("[ALARM] Connecting to /alarm.mp3 ...");
        if (audio.connecttoFS(SD, "/alarm.mp3")) {
            audioFilePlaying = true;
            Serial.println("[ALARM] Playing /alarm.mp3");
            return true;
        }
        Serial.println("[ALARM] connecttoFS failed for /alarm.mp3");
    }
    return false;
}

// Called from loop() when audio EOF callback sets audioNeedsRestart
void restartAudioFile() {
    Serial.println("[AUDIO] Restarting alarm audio file ...");
    if (sdAvailable && tryPlayAudioFile()) return;

    Serial.println("[ALARM] Audio restart failed — switching to backup tone.");
    audioFilePlaying = false;
    if (backupToneHandle == NULL) {
        startBackupTone();
    }
}

// ============================================================
//  STOP ALARM
// ============================================================
void stopAlarm() {
    if (!alarmPlaying) return;

    if (audioFilePlaying) {
        audio.stopSong();
        audioFilePlaying = false;
        Serial.println("[ALARM] Audio file stopped.");
    }

    stopBackupTone();

    alarmPlaying      = false;
    audioNeedsRestart = false;
    Serial.println("[ALARM] Alarm stopped.");
}

// ============================================================
//  BACKUP TONE — FreeRTOS task
//
//  The ESP32-audioI2S library installs the I2S driver on
//  I2S_NUM_0 during setupAudio(). After audio.stopSong() the
//  driver remains installed, so we can write samples directly
//  with i2s_write() without re-installing the driver.
//
//  Two alternating square-wave frequencies (880 Hz / 440 Hz,
//  500 ms each) produce a clearly audible attention pattern.
// ============================================================
void startBackupTone() {
    backupToneStop  = false;
    backupToneBegin = millis();
    Serial.printf("[ALARM] Backup tone: %d Hz / %d Hz alternating, sample rate %d\n",
                  BACKUP_TONE_FREQ_HZ, BACKUP_TONE_FREQ_HZ / 2, BACKUP_SAMPLE_RATE);

    xTaskCreatePinnedToCore(
        backupToneTaskFn,
        "BackupTone",
        4096,
        NULL,
        2,                  // priority above loop task
        &backupToneHandle,
        1                   // CPU core 1
    );
}

void stopBackupTone() {
    if (backupToneHandle == NULL) return;

    backupToneStop = true;
    for (int i = 0; i < 200 && backupToneHandle != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (backupToneHandle != NULL) {
        vTaskDelete(backupToneHandle);
        backupToneHandle = NULL;
    }
    Serial.println("[ALARM] Backup tone stopped.");
}

void backupToneTaskFn(void* pv) {
    const uint32_t BUF_SAMPLES   = 256;
    const uint32_t PHASE_SAMPLES = BACKUP_SAMPLE_RATE / 2; // 0.5 s per frequency

    int16_t  buf[BUF_SAMPLES * 2]; // interleaved stereo: L, R, L, R …
    uint32_t samplePos = 0;

    const uint32_t freqs[2] = {
        (uint32_t)BACKUP_TONE_FREQ_HZ,
        (uint32_t)BACKUP_TONE_FREQ_HZ / 2
    };

    while (!backupToneStop) {
        uint32_t freq   = freqs[(samplePos / PHASE_SAMPLES) % 2];
        uint32_t period = BACKUP_SAMPLE_RATE / freq;

        for (uint32_t i = 0; i < BUF_SAMPLES; i++) {
            int16_t s = ((samplePos % period) < (period / 2))
                        ? (int16_t)BACKUP_TONE_AMPLITUDE
                        : (int16_t)-BACKUP_TONE_AMPLITUDE;
            buf[i * 2]     = s;  // left
            buf[i * 2 + 1] = s;  // right
            samplePos++;
        }

        size_t written = 0;
        i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
    }

    // Flush a buffer of silence so the amp doesn't click
    memset(buf, 0, sizeof(buf));
    size_t bw = 0;
    i2s_write(I2S_NUM_0, buf, sizeof(buf), &bw, pdMS_TO_TICKS(50));

    backupToneHandle = NULL;
    vTaskDelete(NULL);
}

// ============================================================
//  ESP32-audioI2S LIBRARY CALLBACKS
//  These are weak-linked free functions called by the library.
// ============================================================

void audio_info(const char *info) {
    Serial.printf("[AUDIO] %s\n", info);
}

// Called when a WAV file finishes playing
void audio_eof_wav(const char *info) {
    Serial.printf("[AUDIO] EOF WAV: %s\n", info);
    if (alarmPlaying) {
        audioNeedsRestart = true; // handled safely in loop()
    } else {
        audioFilePlaying = false;
    }
}

// Called when an MP3/AAC/FLAC file finishes playing
void audio_eof_mp3(const char *info) {
    Serial.printf("[AUDIO] EOF MP3: %s\n", info);
    if (alarmPlaying) {
        audioNeedsRestart = true;
    } else {
        audioFilePlaying = false;
    }
}

// ============================================================
//  ARDUINO SETUP
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(250);
    Serial.println();
    Serial.println("=============================================");
    Serial.println("   ESP32 Bedside Alarm Clock");
    Serial.println("=============================================");

    Wire.begin(PIN_SDA, PIN_SCL);

    setupRTC();
    setupDisplay();
    setupTM1637();
    setupSD();
    setupAudio();

    pinMode(PIN_BTN_HOUR,   INPUT_PULLUP);
    pinMode(PIN_BTN_MINUTE, INPUT_PULLUP);
    pinMode(PIN_BTN_ALARM,  INPUT_PULLUP);
    Serial.println("[BTN]   Hour=GPIO32  Minute=GPIO33  AlarmToggle=GPIO25");

    Serial.println("---------------------------------------------");
    Serial.println("[BOOT]  Ready.");
    Serial.println("[BOOT]  Hour/Minute buttons: set alarm time.");
    Serial.println("[BOOT]  Alarm button: enable / disable alarm.");
    Serial.println("[BOOT]  Any button while alarm sounds: stop alarm.");
    Serial.println("=============================================");
}

// ============================================================
//  ARDUINO LOOP
// ============================================================
void loop() {
    uint32_t now = millis();

    // ── Poll RTC every second ──────────────────────────────
    if (now - lastRTCPoll >= RTC_POLL_MS) {
        lastRTCPoll = now;

        DateTime dt  = rtc.now();
        nowHour      = dt.hour();
        nowMinute    = dt.minute();
        nowDay       = dt.dayOfTheWeek();

        // Reset alarmFired when minute rolls over (prevents repeat triggers)
        if ((int8_t)nowMinute != prevMinute) {
            if (alarmFired) Serial.println("[ALARM] Minute changed — alarmFired reset.");
            alarmFired = false;
            prevMinute = (int8_t)nowMinute;
        }

        updateDayDisplay();
    }

    // ── Blink colon every 500 ms ───────────────────────────
    if (now - lastColonBlink >= COLON_BLINK_MS) {
        lastColonBlink = now;
        colonOn = !colonOn;
        updateTimeDisplay();
    }

    // ── Buttons ────────────────────────────────────────────
    readButtons();

    // ── Alarm check ────────────────────────────────────────
    checkAlarm();

    // ── Audio library heartbeat (must be called frequently) ─
    if (audioFilePlaying) {
        audio.loop();
    }

    // ── Restart audio file after EOF (set by callback) ─────
    if (audioNeedsRestart && alarmPlaying) {
        audioNeedsRestart = false;
        audioFilePlaying  = false;
        restartAudioFile();
    }

    // ── Auto-stop backup tone after time limit ─────────────
    if (alarmPlaying && !audioFilePlaying && backupToneHandle != NULL) {
        if (now - backupToneBegin >= BACKUP_TONE_MAX_MS) {
            Serial.println("[ALARM] Backup tone time limit reached — stopping.");
            stopAlarm();
        }
    }

    // ── Periodic serial status heartbeat ───────────────────
    if (now - lastStatusPrint >= STATUS_PRINT_MS) {
        lastStatusPrint = now;
        Serial.printf("[STAT]  %02d:%02d  %s  | alarm %s @ %02d:%02d (%d:%02d %s)%s\n",
                      nowHour, nowMinute,
                      DAY_NAMES[nowDay % 7],
                      alarmEnabled ? "ON " : "OFF",
                      alarmHour, alarmMinute,
                      to12Hr(alarmHour), alarmMinute,
                      isAM(alarmHour) ? "AM" : "PM",
                      alarmPlaying ? "  ** PLAYING **" : "");
    }
}
