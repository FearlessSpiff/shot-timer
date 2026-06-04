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
#include <Adafruit_TinyUSB.h>
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
#define LOG_INTERVAL_MS   1000   // log every second
#define MAX_SECONDS         99   // sleep when timer hits this
#define IDLE_SLEEP_MS    60000UL // sleep if paused/stopped for this long

// ── State machine ─────────────────────────────────────────────────────
enum TimerState { STOPPED, RUNNING, PAUSED };
TimerState timerState = STOPPED;

// ── Counters & timestamps ─────────────────────────────────────────────
uint32_t elapsedSeconds  = 0;    // accumulated full seconds
uint32_t runStartMs      = 0;    // millis() when last run segment started
uint32_t lastLogMs       = 0;    // millis() of last 1-second log
uint32_t idleStartMs     = 0;    // millis() when we entered idle (stopped/paused)

// ── Forward declarations ──────────────────────────────────────────────
void updateDisplay();

// ── Switch state ──────────────────────────────────────────────────────
bool     lastSwitchPhysical = HIGH;  // raw pin state last loop
bool     switchPressed      = false; // debounced, one-shot press event
bool     switchHeld         = false; // true once hold threshold passed
bool     buttonDown         = false; // true after confirmed press, false after confirmed release
uint32_t pressStartMs       = 0;     // when the current press began

// ─────────────────────────────────────────────────────────────────────
// Deep sleep — wakes on switch press (falling edge on pin D1)
// ─────────────────────────────────────────────────────────────────────
void goToDeepSleep(const char* reason) {
  Serial.print("[SLEEP] Reason: ");
  Serial.println(reason);
  Serial.flush();

  oled.ssd1306_command(SSD1306_DISPLAYOFF);

  // Configure the switch pin as a wake source (active-low)
  pinMode(SWITCH_PIN, INPUT_PULLUP);

  // nRF52840: use the Adafruit/Seeed BSP sleep helper
  // sd_power_system_off() puts the chip into System OFF (deepest sleep).
  // GPIO SENSE will restart execution from the reset vector on wake.
#ifdef NRF52_SERIES
  // Attach a dummy interrupt so the SENSE register is armed
  attachInterrupt(digitalPinToInterrupt(SWITCH_PIN), [](){}, FALLING);
  nrf_gpio_cfg_sense_input(
      g_ADigitalPinMap[SWITCH_PIN],
      NRF_GPIO_PIN_PULLUP,
      NRF_GPIO_PIN_SENSE_LOW);
  sd_power_system_off();        // succeeds if SoftDevice is running
  NRF_POWER->SYSTEMOFF = 1;    // fallback: direct register, no SoftDevice needed
  // If we reach here, both sleep attempts failed — log and halt
  Serial.println("[SLEEP] ERROR: sleep failed, halted");
  Serial.flush();
  while (true) {}
#else
  // Fallback for simulation / other targets: busy-wait
  while (true) { delay(1000); }
#endif
}

// ─────────────────────────────────────────────────────────────────────
// Reset the timer (does not change run state)
// ─────────────────────────────────────────────────────────────────────
void resetTimer() {
  elapsedSeconds = 0;
  runStartMs     = millis();
  lastLogMs      = millis();
  idleStartMs    = millis();
  Serial.println("[RESET] Timer reset to 0");
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
#define CHAR_GAP     8   // gap between characters (px)

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

// x0/y0 = center of top-left dot of the character
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
  uint8_t  len  = (secs >= 10) ? 2 : 1;

  // Pixel dimensions of one character bounding box
  const int16_t charW = 2 * DOT_PITCH + 2 * DOT_RADIUS;  // 32 px
  const int16_t charH = 4 * DOT_PITCH + 2 * DOT_RADIUS;  // 56 px

  int16_t totalW = len * charW + (len - 1) * CHAR_GAP;
  int16_t x0     = (OLED_WIDTH  - totalW) / 2 + DOT_RADIUS;
  int16_t y0     = (OLED_HEIGHT - charH)  / 2 + DOT_RADIUS;

  if (len == 2) {
    drawDotChar(x0,                    y0, secs / 10);
    drawDotChar(x0 + charW + CHAR_GAP, y0, secs % 10);
  } else {
    drawDotChar(x0, y0, secs);
  }

  oled.display();
}

// ─────────────────────────────────────────────────────────────────────
// Setup
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}  // wait up to 3 s for USB

  pinMode(SWITCH_PIN, INPUT_PULLUP);

  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("[OLED] Init failed");
  } else {
    Serial.println("[OLED] Ready");
  }

  Serial.println("=== Switch Timer Ready ===");
  Serial.println("  Short press → start / pause");
  Serial.println("  Hold 2 s   → reset");
  Serial.println("  Limit: 99 s or 60 s idle → deep sleep");

  idleStartMs = millis();
  updateDisplay();
}

// ─────────────────────────────────────────────────────────────────────
// Loop
// ─────────────────────────────────────────────────────────────────────
void loop() {
  uint32_t now = millis();

  // ── 1. Read & debounce switch ──────────────────────────────────────
  bool raw = digitalRead(SWITCH_PIN);

  if (raw == LOW && lastSwitchPhysical == HIGH) {
    // Falling edge — start debounce window
    delay(DEBOUNCE_MS);
    if (digitalRead(SWITCH_PIN) == LOW) {
      // Confirmed press
      pressStartMs   = millis();
      switchHeld     = false;
      buttonDown     = true;
    }
  }

  if (raw == HIGH && lastSwitchPhysical == LOW) {
    // Rising edge — button released
    delay(DEBOUNCE_MS);
    if (digitalRead(SWITCH_PIN) == HIGH) {
      buttonDown = false;
      if (!switchHeld) {
        // Short press: not a hold-reset, fire the press event
        switchPressed = true;
      }
      switchHeld = false;
    }
  }

  // Detect hold threshold while button is still down
  if (buttonDown && !switchHeld) {
    if ((millis() - pressStartMs) >= HOLD_RESET_MS) {
      switchHeld = true;
      // ── Hold action: reset ──
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
      case PAUSED: {
        bool wasPaused = (timerState == PAUSED);
        timerState  = RUNNING;
        runStartMs  = millis();
        lastLogMs   = millis();
        Serial.println(wasPaused ? "[RESUME] Timer resumed" : "[START] Timer running");
        Serial.print("[TIME] ");
        Serial.print(currentSeconds());
        Serial.println(" s");
        updateDisplay();
        break;
      }

      case RUNNING:
        // Pause — freeze elapsed seconds
        elapsedSeconds = currentSeconds();
        timerState     = PAUSED;
        idleStartMs    = millis();
        Serial.print("[PAUSE] Paused at ");
        Serial.print(elapsedSeconds);
        Serial.println(" s");
        updateDisplay();
        break;
    }
  }

  // ── 3. Periodic 1-second log while running ─────────────────────────
  if (timerState == RUNNING) {
    uint32_t secs = currentSeconds();

    if ((millis() - lastLogMs) >= LOG_INTERVAL_MS) {
      lastLogMs = millis();
      Serial.print("[TIME] ");
      Serial.print(secs);
      Serial.println(" s");
      updateDisplay();

      // Check 99-second ceiling
      if (secs >= MAX_SECONDS) {
        Serial.print("[LIMIT] Reached ");
        Serial.print(MAX_SECONDS);
        Serial.println(" s");
        goToDeepSleep("99 s limit reached");
      }
    }
  }

  // ── 4. Idle timeout (stopped or paused with no activity) ──────────
  if (timerState == STOPPED || timerState == PAUSED) {
    if ((millis() - idleStartMs) >= IDLE_SLEEP_MS) {
      goToDeepSleep("idle for 60 s");
    }
  }
}
