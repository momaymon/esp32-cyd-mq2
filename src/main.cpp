// ESP32-2432S028 (CYD) + MQ-2 gas sensor monitor
// Shows AO voltage, Rs/R0, estimated LPG / smoke ppm, a level bar and a history graph.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>

#include "config.h"
#include "strings.h"

TFT_eSPI tft;

// Screen layout (landscape 320x240)
static const int W = 320;
static const int H = 240;
static const int GRAPH_X = 10;
static const int GRAPH_Y = 150;
static const int GRAPH_W = 300;
static const int GRAPH_H = 80;

static float r0 = 10.0f;  // sensor resistance in clean air (kOhm), set by calibration
static uint16_t history[GRAPH_W];
static int histHead = 0;

// Gas curves from the MQ-2 datasheet: {log10(x ppm), log10(y Rs/R0), slope}
static const float LPG_CURVE[3] = {2.3f, 0.21f, -0.47f};
static const float SMOKE_CURVE[3] = {2.3f, 0.53f, -0.44f};

enum Level { LEVEL_OK, LEVEL_WARN, LEVEL_DANGER };

// ---------- Sensor ----------

float readAoVolts() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(MQ2_AO_PIN);
  return (sum / 16.0f) / 1000.0f * DIVIDER_RATIO;  // voltage at the module AO pin
}

float rsFromVolts(float v) {
  if (v < 0.01f) v = 0.01f;
  return MQ2_RL_KOHM * (MQ2_VCC - v) / v;
}

float ppmFromRatio(float ratio, const float *curve) {
  if (ratio <= 0) return 0;
  return powf(10.0f, ((log10f(ratio) - curve[1]) / curve[2]) + curve[0]);
}

// ---------- LED ----------

void setLed(bool r, bool g, bool b) {
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

// ---------- Screens ----------

void drawHeader() {
  tft.fillRect(0, 0, W, 28, TFT_NAVY);
  tft.setTextColor(TFT_WHITE, TFT_NAVY);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(STR_TITLE, 8, 14, 4);
}

void progressScreen(const char *label, int pct) {
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.fillRect(0, 90, W, 30, TFT_BLACK);
  char buf[48];
  snprintf(buf, sizeof(buf), "%s %d%%", label, pct);
  tft.drawString(buf, W / 2, 105, 4);
  tft.drawRect(30, 130, 260, 20, TFT_WHITE);
  tft.fillRect(32, 132, 256 * pct / 100, 16, TFT_CYAN);
}

void warmup() {
  setLed(false, false, true);
  for (int s = 0; s <= WARMUP_SECONDS; s++) {
    progressScreen(STR_WARMUP, s * 100 / WARMUP_SECONDS);
    Serial.printf("warm-up %d/%d s, AO=%.2f V\n", s, WARMUP_SECONDS, readAoVolts());
    delay(1000);
  }
}

void calibrate() {
  tft.fillRect(0, 90, W, 70, TFT_BLACK);
  float sum = 0;
  for (int i = 0; i < CALIB_SAMPLES; i++) {
    sum += rsFromVolts(readAoVolts());
    progressScreen(STR_CALIB, (i + 1) * 100 / CALIB_SAMPLES);
    delay(100);
  }
  r0 = (sum / CALIB_SAMPLES) / CLEAN_AIR_RATIO;
  Serial.printf("R0 = %.2f kOhm\n", r0);
}

void drawGraphFrame() {
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, TFT_DARKGREY);
}

void drawGraph() {
  tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, TFT_BLACK);
  // threshold lines on a 0..2000 ppm scale
  int yWarn = GRAPH_Y + GRAPH_H - (PPM_WARN * GRAPH_H / 2000);
  int yDanger = GRAPH_Y + GRAPH_H - (PPM_DANGER * GRAPH_H / 2000);
  tft.drawFastHLine(GRAPH_X, yWarn, GRAPH_W, TFT_OLIVE);
  tft.drawFastHLine(GRAPH_X, yDanger, GRAPH_W, TFT_MAROON);
  for (int i = 1; i < GRAPH_W; i++) {
    int a = history[(histHead + i - 1) % GRAPH_W];
    int b = history[(histHead + i) % GRAPH_W];
    int ya = GRAPH_Y + GRAPH_H - 1 - min(a, 2000) * (GRAPH_H - 1) / 2000;
    int yb = GRAPH_Y + GRAPH_H - 1 - min(b, 2000) * (GRAPH_H - 1) / 2000;
    tft.drawLine(GRAPH_X + i - 1, ya, GRAPH_X + i, yb, TFT_GREEN);
  }
}

void drawReadings(float volts, float ratio, float lpg, float smoke, Level level, bool doActive) {
  uint16_t color = level == LEVEL_DANGER ? TFT_RED : level == LEVEL_WARN ? TFT_ORANGE : TFT_GREEN;
  const char *label = level == LEVEL_DANGER ? STR_LEVEL_DANGER
                      : level == LEVEL_WARN ? STR_LEVEL_WARN
                                            : STR_LEVEL_OK;
  char buf[40];

  // Big LPG ppm number
  tft.fillRect(0, 32, 200, 60, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(color, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%d", (int)min(lpg, 9999.0f));
  tft.drawString(buf, 10, 36, 7);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(STR_PPM, 10 + tft.textWidth(buf, 7) + 6, 70, 2);
  tft.drawString(STR_LPG, 10 + tft.textWidth(buf, 7) + 6, 40, 2);

  // Level badge
  tft.fillRoundRect(205, 36, 108, 40, 6, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_BLACK, color);
  tft.drawString(label, 259, 56, 4);

  // Detail row
  tft.fillRect(0, 96, W, 48, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  snprintf(buf, sizeof(buf), "%s %d %s", STR_SMOKE, (int)min(smoke, 9999.0f), STR_PPM);
  tft.drawString(buf, 10, 98, 2);
  snprintf(buf, sizeof(buf), "%s %.2f V", STR_VOLT, volts);
  tft.drawString(buf, 170, 98, 2);
  snprintf(buf, sizeof(buf), "%s %.2f  R0 %.1fk", STR_RATIO, ratio, r0);
  tft.drawString(buf, 10, 120, 2);
  tft.setTextColor(doActive ? TFT_RED : TFT_DARKGREY, TFT_BLACK);
  tft.drawString(doActive ? STR_DO_ON : STR_DO_OFF, 230, 120, 2);
}

// ---------- Arduino ----------

void setup() {
  Serial.begin(115200);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLed(false, false, false);
  if (MQ2_DO_PIN >= 0) pinMode(MQ2_DO_PIN, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_AO_PIN, ADC_11db);  // ~0..3.1 V range

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  warmup();
  calibrate();

  tft.fillRect(0, 28, W, H - 28, TFT_BLACK);
  drawGraphFrame();
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last < READ_INTERVAL_MS) return;
  last = millis();

  float volts = readAoVolts();
  float ratio = rsFromVolts(volts) / r0;
  float lpg = ppmFromRatio(ratio, LPG_CURVE);
  float smoke = ppmFromRatio(ratio, SMOKE_CURVE);
  // MQ-2 DO goes LOW when gas passes the module's trimpot threshold
  bool doActive = MQ2_DO_PIN >= 0 && digitalRead(MQ2_DO_PIN) == LOW;

  Level level = LEVEL_OK;
  if (lpg >= PPM_DANGER || doActive) level = LEVEL_DANGER;
  else if (lpg >= PPM_WARN) level = LEVEL_WARN;

  // LED: green safe, yellow warning, blinking red danger
  if (level == LEVEL_OK) setLed(false, true, false);
  else if (level == LEVEL_WARN) setLed(true, true, false);
  else setLed((millis() / 500) % 2, false, false);

  history[histHead] = (uint16_t)min(lpg, 2000.0f);
  histHead = (histHead + 1) % GRAPH_W;

  drawReadings(volts, ratio, lpg, smoke, level, doActive);
  drawGraph();

  Serial.printf("AO=%.2fV Rs/R0=%.2f LPG=%.0fppm Smoke=%.0fppm DO=%d\n", volts, ratio, lpg, smoke,
                doActive);
}
