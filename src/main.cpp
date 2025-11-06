#include <Arduino.h>
#include <U8g2lib.h>

#define OLED_SDA 5
#define OLED_SCL 6
#define OLED_RESET U8X8_PIN_NONE
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RESET, OLED_SCL, OLED_SDA);

const int OLED_VISIBLE_W = 72;
const int OLED_VISIBLE_H = 40;
const int OLED_X_OFFSET = 30;
const int OLED_Y_OFFSET = 12;

#define BUTTON_PIN 9
#define IEM_PIN    4
#define VIB_PIN    10

// loudness, from 1 to 255
#define Loud1    2
#define Loud2    1

const int IEM_LEDC_CHANNEL = 0;
const int IEM_LEDC_FREQ_RES = 8;
const int IEM_LEDC_BASE_FREQ = 2000;
volatile int bpm = 130;
const int BPM_MIN = 30;
const int BPM_MAX = 999;
const int BEATS_PER_BAR = 4;
unsigned long lastBeatMillis = 0;
int beatIndex = 0;
const unsigned long CLICK_LEN_MS = 60;
const int TAP_BUF_SIZE = 6;
unsigned long tapTimes[TAP_BUF_SIZE];
int tapIdx = 0;
int tapCount = 0;
unsigned long lastTapMillis = 0;
unsigned long btnPressStart = 0;
bool btnPrev = HIGH;
bool editing = false;
int editDigit = 0;
unsigned long editLastInteraction = 0;
const unsigned long EDIT_ADVANCE_MS = 5000;
bool vibMode = false;
const unsigned long VIB_PULSE_MS = 50;

int computeBPMfromTaps() {
  int valid = min(tapCount, TAP_BUF_SIZE);
  if (valid < 2) return -1;
  unsigned long total = 0; int n = 0;
  for (int i = 0; i < valid - 1; ++i) {
    int a = (tapIdx - 1 - i + TAP_BUF_SIZE) % TAP_BUF_SIZE;
    int b = (tapIdx - 2 - i + TAP_BUF_SIZE) % TAP_BUF_SIZE;
    unsigned long dt = tapTimes[a] - tapTimes[b];
    if (dt > 0) { total += dt; n++; }
  }
  if (!n) return -1;
  unsigned long avg = total / n;
  return (avg ? (int)round(60000.0 / avg) : -1);
}

void playIEMClick(bool isDownbeat) {
  int freq = isDownbeat ? 1760 : 880;
  int volume = isDownbeat ? Loud1 : Loud2;

  ledcWriteTone(IEM_LEDC_CHANNEL, freq);
  ledcWrite(IEM_LEDC_CHANNEL, volume);
  delay(CLICK_LEN_MS);
  ledcWrite(IEM_LEDC_CHANNEL, 0);
}


void vibratePulse() {
  digitalWrite(VIB_PIN, HIGH);
  delay(VIB_PULSE_MS);
  digitalWrite(VIB_PIN, LOW);
}

void handleTap(unsigned long now) {
  tapTimes[tapIdx] = now;
  tapIdx = (tapIdx + 1) % TAP_BUF_SIZE;
  tapCount = min(tapCount + 1, TAP_BUF_SIZE);
  lastTapMillis = now;

  int newBpm = computeBPMfromTaps();
  if (newBpm > 0) bpm = newBpm;
}

void enterEditMode() {
  editing = true;
  editDigit = 0;
  editLastInteraction = millis();
}

void incrementEditDigitValue() {
  int place = (editDigit == 0) ? 100 : (editDigit == 1 ? 10 : 1);
  int digit = (bpm / place) % 10;
  digit = (digit + 1) % 10;
  bpm = bpm - ((bpm / place) % 10) * place + digit * place;
  bpm = constrain(bpm, BPM_MIN, BPM_MAX);
  editLastInteraction = millis();
}

void drawOLED() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);

  u8g2.drawStr(OLED_X_OFFSET + 1, OLED_Y_OFFSET + 20, vibMode ? "V: Y" : "V: N");

  char buf[12];
  snprintf(buf, sizeof(buf), "%03d", bpm);
  u8g2.setFont(u8g2_font_6x12_tr);
  int strWidth = u8g2.getStrWidth(buf);
  int bpmX = OLED_X_OFFSET + (OLED_VISIBLE_W - strWidth) / 2;
  int bpmY = OLED_Y_OFFSET + 25;

  static unsigned long lastBlink = 0;
  static bool blinkState = true;
  unsigned long now = millis();
  if (now - lastBlink > 500) {
    blinkState = !blinkState;
    lastBlink = now;
  }

  for (int i = 0; i < 3; i++) {
    char c[2] = { buf[i], 0 };
    int charWidth = u8g2.getStrWidth(c);
    int charX = bpmX + u8g2.getStrWidth(buf) / 3 * i;

    if (i == 1) charX += 1;
    if (i == 2) charX += 2;

    bool hide = (editing && i == editDigit && !blinkState);
    if (!hide) {
      u8g2.drawStr(charX, bpmY, c);
    }
  }

  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(OLED_X_OFFSET + (OLED_VISIBLE_W / 2) - 9, bpmY + 8, "BPM");

  int startX = OLED_X_OFFSET;
  int y = OLED_Y_OFFSET + 48;
  for (int i = 0; i < BEATS_PER_BAR; i++) {
    int bx = startX + i * 18;
    if (i == beatIndex) u8g2.drawBox(bx, y - 5, 14, 5);
    else u8g2.drawFrame(bx, y - 5, 14, 5);
  }

  if (editing) {
    u8g2.drawStr(OLED_X_OFFSET + OLED_VISIBLE_W - 20, OLED_Y_OFFSET + 20, "EDIT");
  }

  u8g2.sendBuffer();
}


void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(VIB_PIN, OUTPUT);
  digitalWrite(VIB_PIN, LOW);

  ledcSetup(IEM_LEDC_CHANNEL, IEM_LEDC_BASE_FREQ, IEM_LEDC_FREQ_RES);
  ledcAttachPin(IEM_PIN, IEM_LEDC_CHANNEL);
  ledcWriteTone(IEM_LEDC_CHANNEL, 0);

  u8g2.begin();
  u8g2.setContrast(255);
  u8g2.setBusClock(400000);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(OLED_X_OFFSET, OLED_Y_OFFSET + 10, "Metronome ready");
  u8g2.sendBuffer();
}

void loop() {
  unsigned long now = millis();

static bool btnPrev = HIGH;
static unsigned long btnPressStart = 0;

bool btn = digitalRead(BUTTON_PIN);

if (btnPrev == HIGH && btn == LOW) {
  btnPressStart = now;
}

if (btnPrev == LOW && btn == HIGH) {
  unsigned long held = now - btnPressStart;

  if (held < 600) {
    if (editing)
      incrementEditDigitValue();
    else
      handleTap(now);

  } else if (held >= 4000) {
    vibMode = !vibMode;

  } else if (held >= 1000 && held < 4000) {
    enterEditMode();
  }
}

btnPrev = btn;


  if (editing && (now - editLastInteraction) > EDIT_ADVANCE_MS) {
    editDigit++;
    if (editDigit > 2) editing = false;
    else editLastInteraction = now;
  }

  if (tapCount > 0 && (now - lastTapMillis) > 4000) { tapCount = 0; tapIdx = 0; }

  static unsigned long nextBeat = 0;
  if (nextBeat == 0) nextBeat = now + (60000 / bpm);
  if (now >= nextBeat) {
    bool downbeat = (beatIndex == 0);
    if (vibMode) vibratePulse();
    playIEMClick(downbeat);
    beatIndex = (beatIndex + 1) % BEATS_PER_BAR;
    nextBeat = now + (60000 / bpm);
  }

  static unsigned long lastUI = 0;
  if (now - lastUI > 80) { drawOLED(); lastUI = now; }

  delay(5);
}