/**
 * shot_timer.ino
 * Seeed XIAO nRF52840 — Switch-controlled stopwatch with deep sleep
 *
 * Wiring:
 *   Switch leg A → D1 (P0.03)
 *   Switch leg B → GND
 *   (Internal pull-up enabled — no external resistor needed)
 *
 * Behaviour:
 *   Single press (while stopped) → Start timer
 *   Single press (while running) → Pause timer
 *   Hold 2 s                     → Reset timer to 0
 *   Timer reaches 99 s           → Deep sleep
 *   Timer paused for > 60 s      → Deep sleep
 */

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Pin ──────────────────────────────────────────────────────────────
#define SWITCH_PIN    1        // D1 / P0.03

// ── OLED ─────────────────────────────────────────────────────────────
#define OLED_WIDTH   128
#define OLED_HEIGHT   64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);

// ── Timing constants ─────────────────────────────────────────────────
#define DEBOUNCE_MS         20
#define HOLD_RESET_MS     2000   // hold duration to trigger reset
#define DISPLAY_INTERVAL_MS 1000 // display update interval while running
#define MAX_SECONDS         99   // sleep when timer hits this
#define IDLE_SLEEP_MS    60000UL // sleep if paused/stopped for this long

// ── State machine ─────────────────────────────────────────────────────
enum TimerState { STOPPED, RUNNING, PAUSED };
TimerState timerState = STOPPED;

// ── Counters & timestamps ─────────────────────────────────────────────
uint32_t elapsedSeconds  = 0;
uint32_t runStartMs      = 0;
uint32_t lastDisplayMs   = 0;
uint32_t idleStartMs     = 0;

// ── Coffee bean splash bitmap (64×64, centred on 128×64 display) ─────
static const uint8_t COFFEE_BITMAP[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x7F, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0xFF, 0xF0, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x07, 0xFF, 0xFC, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x03, 0xFF, 0xFF, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0x00,
  0x00, 0x00, 0x10, 0xFF, 0xFF, 0xC0, 0x00, 0x00,
  0x00, 0x00, 0x18, 0x7F, 0xFF, 0xE0, 0x00, 0x00,
  0x00, 0x00, 0x1C, 0x3F, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x00, 0x1F, 0x03, 0xFF, 0xF8, 0x00, 0x00,
  0x00, 0x00, 0x1F, 0x80, 0x07, 0xF8, 0x00, 0x00,
  0x00, 0x00, 0x1F, 0xE0, 0x01, 0xFC, 0x00, 0x00,
  0x00, 0x00, 0x0F, 0xFF, 0xF0, 0x7C, 0x00, 0x00,
  0x00, 0x00, 0x0F, 0xFF, 0xFC, 0x3C, 0x00, 0x00,
  0x00, 0x00, 0x07, 0xFF, 0xFF, 0x1C, 0x00, 0x00,
  0x00, 0x00, 0x03, 0xFF, 0xFF, 0x8C, 0x00, 0x00,
  0x00, 0x00, 0x01, 0xFF, 0xFF, 0xC0, 0x00, 0x00,
  0x00, 0x3F, 0xC0, 0xFF, 0xFF, 0xE0, 0x00, 0x00,
  0x00, 0x1F, 0xF0, 0x7F, 0xFF, 0xF0, 0x00, 0x00,
  0x00, 0x0F, 0xFC, 0x1F, 0xFF, 0xE0, 0x00, 0x00,
  0x00, 0x83, 0xFE, 0x03, 0xFF, 0x80, 0x00, 0x00,
  0x01, 0xE1, 0xFF, 0x00, 0x18, 0x00, 0x00, 0x00,
  0x01, 0xF0, 0xFF, 0x80, 0x00, 0x03, 0xF0, 0x00,
  0x01, 0xF8, 0x7F, 0x80, 0x00, 0x1F, 0xF8, 0x00,
  0x01, 0xFC, 0x7F, 0xC0, 0x00, 0x7F, 0xF8, 0x00,
  0x01, 0xFE, 0x3F, 0xE0, 0x01, 0xFF, 0xF1, 0x00,
  0x01, 0xFE, 0x3F, 0xE0, 0x03, 0xFF, 0xF1, 0x80,
  0x01, 0xFF, 0x3F, 0xF0, 0x07, 0xFF, 0xE3, 0x80,
  0x01, 0xFF, 0x3F, 0xF0, 0x0F, 0xFF, 0xC7, 0x80,
  0x00, 0xFF, 0x1F, 0xF0, 0x1F, 0xFF, 0xC7, 0x80,
  0x00, 0xFF, 0x9F, 0xF0, 0x3F, 0xFF, 0x8F, 0x80,
  0x00, 0xFF, 0x9F, 0xF8, 0x7F, 0xFE, 0x1F, 0x80,
  0x00, 0x7F, 0x8F, 0xF8, 0x7F, 0xF8, 0x3F, 0x00,
  0x00, 0x7F, 0xCF, 0xF8, 0xFF, 0xC0, 0xFF, 0x00,
  0x00, 0x3F, 0xC7, 0xF8, 0xFF, 0x03, 0xFF, 0x00,
  0x00, 0x3F, 0xE3, 0xF8, 0xFC, 0x1F, 0xFE, 0x00,
  0x00, 0x1F, 0xE1, 0xF8, 0xF8, 0x7F, 0xFC, 0x00,
  0x00, 0x0F, 0xF0, 0xF0, 0xF0, 0xFF, 0xFC, 0x00,
  0x00, 0x07, 0xFC, 0x70, 0xF1, 0xFF, 0xF8, 0x00,
  0x00, 0x03, 0xFE, 0x00, 0xE3, 0xFF, 0xF0, 0x00,
  0x00, 0x01, 0xFF, 0x00, 0xE7, 0xFF, 0xE0, 0x00,
  0x00, 0x00, 0x7F, 0x80, 0x47, 0xFF, 0x80, 0x00,
  0x00, 0x00, 0x0F, 0x00, 0x0F, 0xFF, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x0F, 0xFC, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x07, 0xE0, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ── Forward declarations ──────────────────────────────────────────────
void updateDisplay();

// ── Switch state ──────────────────────────────────────────────────────
bool     lastSwitchPhysical = HIGH;
bool     switchPressed      = false;
bool     switchHeld         = false;
bool     buttonDown         = false;
uint32_t pressStartMs       = 0;

// ─────────────────────────────────────────────────────────────────────
// Deep sleep — wakes on switch press (falling edge on pin D1)
// ─────────────────────────────────────────────────────────────────────
void goToDeepSleep() {
  oled.ssd1306_command(SSD1306_DISPLAYOFF);

  pinMode(SWITCH_PIN, INPUT_PULLUP);

#ifdef NRF52_SERIES
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), [](){}, FALLING);
  nrf_gpio_cfg_sense_input(
      g_ADigitalPinMap[SWITCH_PIN],
      NRF_GPIO_PIN_PULLUP,
      NRF_GPIO_PIN_SENSE_LOW);
  sd_power_system_off();
  NRF_POWER->SYSTEMOFF = 1;
  while (true) {}
#else
  while (true) { delay(1000); }
#endif
}

// ─────────────────────────────────────────────────────────────────────
// Reset the timer (does not change run state)
// ─────────────────────────────────────────────────────────────────────
void resetTimer() {
  elapsedSeconds = 0;
  runStartMs     = millis();
  lastDisplayMs  = millis();
  idleStartMs    = millis();
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────
// Current total seconds (elapsed + in-progress segment)
// ─────────────────────────────────────────────────────────────────────
uint32_t currentSeconds() {
  if (timerState == RUNNING) {
    return elapsedSeconds + (millis() - runStartMs) / 1000;
  }
  return elapsedSeconds;
}

// ─────────────────────────────────────────────────────────────────────
// 3×5 dot-matrix font
// Each row byte: bit 2 = left col, bit 1 = mid col, bit 0 = right col
// ─────────────────────────────────────────────────────────────────────
#define DOT_PITCH   12   // dot center-to-center (px)
#define DOT_RADIUS   4   // dot radius (px)
#define CHAR_GAP    12   // gap between characters (px) — matches DOT_PITCH

static const uint8_t DOT_GLYPHS[10][5] = {
  {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
  {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
  {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
  {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
  {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
  {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
  {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
  {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
  {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
  {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
};

void drawDotChar(int16_t x0, int16_t y0, uint8_t digit) {
  for (uint8_t row = 0; row < 5; row++) {
    uint8_t bits = DOT_GLYPHS[digit][row];
    for (uint8_t col = 0; col < 3; col++) {
      if (bits & (4 >> col)) {
        oled.fillCircle(x0 + col * DOT_PITCH, y0 + row * DOT_PITCH,
                        DOT_RADIUS, SSD1306_WHITE);
      }
    }
  }
}

// ─────────────────────────────────────────────────────────────────────
// Render seconds as dot-matrix digits centered on the OLED
// ─────────────────────────────────────────────────────────────────────
void updateDisplay() {
  oled.clearDisplay();

  uint32_t secs = currentSeconds();

  const int16_t charW  = 2 * DOT_PITCH + 2 * DOT_RADIUS;  // 32 px
  const int16_t charH  = 4 * DOT_PITCH + 2 * DOT_RADIUS;  // 56 px
  const int16_t totalW = 2 * charW + CHAR_GAP;             // fixed 2-digit width

  int16_t x0 = (OLED_WIDTH  - totalW) / 2 + DOT_RADIUS;
  int16_t y0 = (OLED_HEIGHT - charH)  / 2 + DOT_RADIUS;

  if (secs >= 10) {
    drawDotChar(x0,                    y0, secs / 10);
    drawDotChar(x0 + charW + CHAR_GAP, y0, secs % 10);
  } else {
    drawDotChar(x0 + charW + CHAR_GAP, y0, secs);  // units slot only
  }

  oled.display();
}

// ─────────────────────────────────────────────────────────────────────
// 1-second splash screen
// ─────────────────────────────────────────────────────────────────────
void showSplash() {
  oled.clearDisplay();
  // 64×64 bitmap centred horizontally on the 128×64 display
  oled.drawBitmap((OLED_WIDTH - 64) / 2, 0, COFFEE_BITMAP, 64, 64, SSD1306_WHITE);
  oled.display();
  delay(1500);
}

// ─────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────
void setup() {
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);

  showSplash();

  idleStartMs = millis();
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────
void loop() {
  // ── 1. Read & debounce switch ──────────────────────────────────────
  bool raw = digitalRead(SWITCH_PIN);

  if (raw == LOW && lastSwitchPhysical == HIGH) {
    delay(DEBOUNCE_MS);
    if (digitalRead(SWITCH_PIN) == LOW) {
      pressStartMs = millis();
      switchHeld   = false;
      buttonDown   = true;
    }
  }

  if (raw == HIGH && lastSwitchPhysical == LOW) {
    delay(DEBOUNCE_MS);
    if (digitalRead(SWITCH_PIN) == HIGH) {
      buttonDown = false;
      if (!switchHeld) {
        switchPressed = true;
      }
      switchHeld = false;
    }
  }

  if (buttonDown && !switchHeld) {
    if ((millis() - pressStartMs) >= HOLD_RESET_MS) {
      switchHeld = true;
      timerState = STOPPED;
      resetTimer();
    }
  }

  lastSwitchPhysical = raw;

  // ── 2. Handle short-press events ──────────────────────────────────
  if (switchPressed) {
    switchPressed = false;

    switch (timerState) {
      case STOPPED:
      case PAUSED:
        timerState    = RUNNING;
        runStartMs    = millis();
        lastDisplayMs = millis();
        updateDisplay();
        break;

      case RUNNING:
        elapsedSeconds = currentSeconds();
        timerState     = PAUSED;
        idleStartMs    = millis();
        updateDisplay();
        break;
    }
  }

  // ── 3. Periodic display update while running ───────────────────────
  if (timerState == RUNNING) {
    if ((millis() - lastDisplayMs) >= DISPLAY_INTERVAL_MS) {
      lastDisplayMs = millis();
      updateDisplay();

      if (currentSeconds() >= MAX_SECONDS) {
        goToDeepSleep();
      }
    }
  }

  // ── 4. Idle timeout (stopped or paused with no activity) ──────────
  if (timerState == STOPPED || timerState == PAUSED) {
    if ((millis() - idleStartMs) >= IDLE_SLEEP_MS) {
      goToDeepSleep();
    }
  }
}
