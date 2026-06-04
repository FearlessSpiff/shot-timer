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

// ── Pin ──────────────────────────────────────────────────────────────
#define SWITCH_PIN  1          // D1 / P0.03

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

// ── Switch state ──────────────────────────────────────────────────────
bool     lastSwitchPhysical = HIGH;  // raw pin state last loop
bool     switchPressed      = false; // debounced, one-shot press event
bool     switchHeld         = false; // true once hold threshold passed
uint32_t pressStartMs       = 0;     // when the current press began

// ─────────────────────────────────────────────────────────────────────
// Deep sleep — wakes on switch press (falling edge on pin D1)
// ─────────────────────────────────────────────────────────────────────
void goToDeepSleep(const char* reason) {
  Serial.print("[SLEEP] Reason: ");
  Serial.println(reason);
  Serial.flush();
  delay(10);

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
  sd_power_system_off();   // does not return; chip resets on wake
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
// Setup
// ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}  // wait up to 3 s for USB

  pinMode(SWITCH_PIN, INPUT_PULLUP);

  Serial.println("=== Switch Timer Ready ===");
  Serial.println("  Short press → start / pause");
  Serial.println("  Hold 2 s   → reset");
  Serial.println("  Limit: 99 s or 60 s idle → deep sleep");

  idleStartMs = millis();
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
    }
  }

  if (raw == HIGH && lastSwitchPhysical == LOW) {
    // Rising edge — button released
    delay(DEBOUNCE_MS);
    if (digitalRead(SWITCH_PIN) == HIGH) {
      if (!switchHeld) {
        // Short press: not a hold-reset, fire the press event
        switchPressed = true;
      }
      switchHeld = false;
    }
  }

  // Detect hold threshold while button is still down
  if (raw == LOW && !switchHeld) {
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
      case PAUSED:
        // Start / resume
        timerState  = RUNNING;
        runStartMs  = millis();
        lastLogMs   = millis();
        Serial.println(timerState == RUNNING
                       ? "[START] Timer running"
                       : "[RESUME] Timer resumed");
        // Print immediately so the state change is visible
        Serial.print("[TIME] ");
        Serial.print(currentSeconds());
        Serial.println(" s");
        break;

      case RUNNING:
        // Pause — freeze elapsed seconds
        elapsedSeconds = currentSeconds();
        timerState     = PAUSED;
        idleStartMs    = millis();
        Serial.print("[PAUSE] Paused at ");
        Serial.print(elapsedSeconds);
        Serial.println(" s");
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
