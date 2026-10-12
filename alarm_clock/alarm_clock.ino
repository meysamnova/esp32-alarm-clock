/*
 * ESP32 alarm clock with room temperature
 *
 * Hardware: ESP32 DevKit, 0.96" SSD1306 OLED (128x64, two-color),
 *           LM35DZ temperature sensor, passive piezo buzzer, 2 pushbuttons.
 * Wiring and setup: see README.md
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>
#include <string.h>

// ---------- WiFi (used at boot to get the time over NTP; 2.4 GHz only) ----------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ---------- Pins ----------
const int SDA_PIN  = 33;
const int SCL_PIN  = 25;
const int BTN_MODE = 4;    // other leg to GND
const int BTN_UP   = 13;   // other leg to GND
const int BUZZER_A = 26;   // buzzer (+)
const int BUZZER_B = 27;   // buzzer (-) in bridge mode
const int TEMP_PIN = 34;   // LM35DZ Vout

// true  = bridge drive: both pins swing, about double the voltage, much louder
// false = single pin: buzzer (-) goes to GND
const bool BRIDGE_DRIVE = true;

// ---------- Alarm tone ----------
// Passive buzzer tones. Keep the pair about 500 Hz apart; adjust if another tone sounds louder
const int ALARM_FREQ_LOW  = 2500;
const int ALARM_FREQ_HIGH = 3000;

// ---------- Alarm time (you can change it on the device) ----------
int  alarmHour = 6;
int  alarmMin  = 30;
bool alarmOn   = true;

// ---------- Temperature calibration ----------
// Adjust until the reading matches a room thermometer
const float TEMP_OFFSET = -5.5;
float tempC = 0.0;

// ---------- Time zone: Tehran = UTC+3:30, no DST ----------
const long GMT_OFFSET_SEC = 12600;
const int  DST_OFFSET_SEC = 0;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum Mode { NORMAL, SET_HOUR, SET_MIN, SET_ONOFF };
Mode mode = NORMAL;

bool     ringing     = false;
time_t   snoozeUntil = 0;
int      firedKey    = -1;     // day+minute the alarm last fired or was dismissed
uint32_t lastToggle  = 0;
bool     toneHigh    = false;

// ---------- Debounced button (returns true on press) ----------
struct Button { uint8_t pin; bool last; uint32_t t; };
Button modeBtn = {BTN_MODE, false, 0};
Button upBtn   = {BTN_UP,   false, 0};

bool pressed(Button &b) {
  bool down = (digitalRead(b.pin) == LOW);
  if (down != b.last && millis() - b.t > 40) {
    b.t = millis();
    b.last = down;
    return down;
  }
  return false;
}

// ---------- Temperature ----------
void readTemperature() {
  uint16_t s[31];
  for (int i = 0; i < 31; i++) {
    s[i] = analogReadMilliVolts(TEMP_PIN);
    delayMicroseconds(200);
  }
  // Sort, then use the middle sample so spikes are ignored
  for (int i = 0; i < 30; i++)
    for (int j = i + 1; j < 31; j++)
      if (s[j] < s[i]) { uint16_t x = s[i]; s[i] = s[j]; s[j] = x; }

  int mv = s[15];
  tempC = mv / 10.0 + TEMP_OFFSET;    // LM35: 10 mV per degree C
  Serial.printf("LM35 %d mV -> %.1f C\n", mv, tempC);
}

// ---------- Buzzer helpers ----------
void silenceBuzzer() {
  noTone(BUZZER_A);
  digitalWrite(BUZZER_A, LOW);
  digitalWrite(BUZZER_B, LOW);
}

// Bridge drive: square wave on both pins, opposite phase.
// The buzzer sees about 6.6 V peak-to-peak instead of 3.3 V.
void playBridged(int freq, uint32_t ms) {
  uint32_t halfUs = 500000UL / freq;
  uint32_t end = millis() + ms;
  while (millis() < end) {
    digitalWrite(BUZZER_A, HIGH);
    digitalWrite(BUZZER_B, LOW);
    delayMicroseconds(halfUs);
    digitalWrite(BUZZER_A, LOW);
    digitalWrite(BUZZER_B, HIGH);
    delayMicroseconds(halfUs);
  }
  digitalWrite(BUZZER_A, LOW);
  digitalWrite(BUZZER_B, LOW);
}

// ---------- Drawing ----------
void showMessage(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.println(line1);
  display.println(line2);
  display.display();
}

// Ringing screen: WAKE UP! in the yellow band, the alarm time in the blue band
void drawRinging() {
  char hm[6];
  snprintf(hm, sizeof hm, "%02d:%02d", alarmHour, alarmMin);

  display.clearDisplay();

  display.setTextSize(2);
  display.setCursor((128 - 8 * 12) / 2, 0);
  display.print("WAKE UP!");

  display.setTextSize(3);
  display.setCursor((128 - 5 * 18) / 2, 26);
  display.print(hm);

  display.display();
}

void drawClock(const struct tm &t) {
  char line[24];
  display.clearDisplay();

  // Yellow band (rows 0-15): temperature, or which setting you're changing
  display.setTextSize(1);
  display.setCursor(0, 4);
  if (mode == NORMAL) {
    snprintf(line, sizeof line, "Temperature: %.1f C", tempC);
    display.print(line);
  } else if (mode == SET_HOUR) {
    display.print("SET ALARM HOUR");
  } else if (mode == SET_MIN) {
    display.print("SET ALARM MINUTE");
  } else {
    display.print("SET ALARM ON/OFF");
  }

  if (mode == NORMAL) {
    // Blue band: the clock
    char hms[9];
    strftime(hms, sizeof hms, "%H:%M:%S", &t);
    display.setTextSize(2);
    display.setCursor((128 - 8 * 12) / 2, 26);
    display.print(hms);

    // Date
    strftime(line, sizeof line, "%a %d %b %Y", &t);
    display.setTextSize(1);
    display.setCursor((128 - (int)strlen(line) * 6) / 2, 52);
    display.print(line);
  } else {
    // Blue band: alarm time. The field being set blinks.
    bool show = ((millis() / 400) % 2) == 0;   // on for 400 ms, off for 400 ms
    const int y  = 26;
    const int x0 = 10;                          // "HH:MM OFF" is centered at this x

    char hh[3], mm[3];
    snprintf(hh, sizeof hh, "%02d", alarmHour);
    snprintf(mm, sizeof mm, "%02d", alarmMin);

    display.setTextSize(2);

    if (mode != SET_HOUR || show) {             // hour
      display.setCursor(x0, y);
      display.print(hh);
    }

    display.setCursor(x0 + 24, y);              // colon never blinks
    display.print(":");

    if (mode != SET_MIN || show) {              // minute
      display.setCursor(x0 + 36, y);
      display.print(mm);
    }

    if (mode != SET_ONOFF || show) {            // ON/OFF
      display.setCursor(x0 + 72, y);
      display.print(alarmOn ? "ON" : "OFF");
    }
  }
  display.display();
}

// ---------- Alarm state ----------
void startRinging() {
  ringing = true;
  lastToggle = 0;
  drawRinging();
}

void stopRinging() {
  ringing = false;
  silenceBuzzer();
  display.invertDisplay(false);
}

// ---------- WiFi ----------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    showMessage("Connecting WiFi...");
    delay(500);
    if (millis() - start > 15000) {
      showMessage("WiFi failed", "Check name/password");
      delay(2000);
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      start = millis();
    }
  }
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);
  pinMode(BTN_MODE, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BUZZER_A, OUTPUT);
  pinMode(BUZZER_B, OUTPUT);
  silenceBuzzer();

  pinMode(TEMP_PIN, INPUT);
  analogSetPinAttenuation(TEMP_PIN, ADC_2_5db);   // about 100-1250 mV range, best for the LM35

  Wire.begin(SDA_PIN, SCL_PIN);                   // must come before display.begin()
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (true) delay(100);
  }
  Wire.setClock(400000);
  display.setTextColor(SSD1306_WHITE);

  connectWiFi();
  configTime(GMT_OFFSET_SEC, DST_OFFSET_SEC, "pool.ntp.org", "time.google.com");
}

// ---------- Loop ----------
void loop() {
  // Read temperature once a second
  static uint32_t lastTempRead = 0;
  if (millis() - lastTempRead >= 1000) {
    lastTempRead = millis();
    readTemperature();
  }

  struct tm t;
  if (!getLocalTime(&t, 10)) {
    showMessage("Syncing time...", "Waiting for NTP");
    delay(300);
    return;
  }
  int nowKey = (t.tm_yday * 24 + t.tm_hour) * 60 + t.tm_min;

  // Buttons
  if (pressed(modeBtn)) {
    if (ringing) {                        // MODE while ringing = snooze 5 min
      stopRinging();
      snoozeUntil = time(nullptr) + 5 * 60;
    } else {
      mode = (Mode)((mode + 1) % 4);      // NORMAL -> HOUR -> MINUTE -> ON/OFF -> NORMAL
      if (mode == NORMAL) {
        firedKey = nowKey;                // finished setting: don't ring for this minute
      }
    }
  }
  if (pressed(upBtn)) {
    if (ringing) {                        // UP while ringing = stop
      stopRinging();
      snoozeUntil = 0;
    } else if (mode == SET_HOUR) {
      alarmHour = (alarmHour + 1) % 24;
    } else if (mode == SET_MIN) {
      alarmMin = (alarmMin + 1) % 60;
    } else if (mode == SET_ONOFF) {
      alarmOn = !alarmOn;
    }
  }

  // Alarm: only in normal mode, once per day and minute
  if (alarmOn && !ringing && mode == NORMAL
      && t.tm_hour == alarmHour && t.tm_min == alarmMin
      && t.tm_sec < 10 && nowKey != firedKey) {
    firedKey = nowKey;
    startRinging();
  }

  // Snooze finished
  if (snoozeUntil && time(nullptr) >= snoozeUntil) {
    snoozeUntil = 0;
    startRinging();
  }

  // Ringing: siren and screen flash
  if (ringing) {
    if (millis() - lastToggle > 250) {
      lastToggle = millis();
      toneHigh = !toneHigh;
      display.invertDisplay(toneHigh);
      if (!BRIDGE_DRIVE) tone(BUZZER_A, toneHigh ? ALARM_FREQ_HIGH : ALARM_FREQ_LOW);
    }
    if (BRIDGE_DRIVE) playBridged(toneHigh ? ALARM_FREQ_HIGH : ALARM_FREQ_LOW, 20);
    return;
  }

  // Redraw: 5 times a second on the clock, 10 times a second while setting (for the blink)
  static uint32_t lastDraw = 0;
  uint32_t interval = (mode == NORMAL) ? 200 : 100;
  if (millis() - lastDraw >= interval) {
    lastDraw = millis();
    drawClock(t);
  }
}
