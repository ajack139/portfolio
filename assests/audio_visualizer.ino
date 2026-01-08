#include <LedControl.h>            // External MAX7219 matrix
#include "Arduino_LED_Matrix.h"    // On-board UNO R4 matrix
#include <math.h>                  // for sin()

// -------------------- ON-BOARD MATRIX OBJECT --------------------
ArduinoLEDMatrix builtInMatrix;

// -------------------- PINS & CONSTANTS --------------------
const int DIN_PIN    = 11;  // MAX7219 DIN
const int CLK_PIN    = 13;  // MAX7219 CLK
const int CS_PIN     = 10;  // MAX7219 CS / LOAD
const int NUM_MATS   = 4;   // 4 chained 8x8 blocks => 32x8 display

const int MIC_PIN    = A0;  // microphone OUT
const int BUTTON_PIN = 2;   // mode button (other leg to GND)

// 🔊 Audio tuning – reactive but not insane
const float QUIET_THRESHOLD = 25.0;   // was 20.0 – ignore more tiny buzz  // <<<
const float MAX_LEVEL_DIFF  = 180.0;  // reach full bar at lower volume

// -------------------- GLOBALS --------------------
LedControl lc(DIN_PIN, CLK_PIN, CS_PIN, NUM_MATS);

int   currentMode   = 0;     // 0 = bar, 1 = wave, 2 = pulse
const int NUM_MODES = 3;

float smoothedLevel     = 0;     // shared by all modes
float filteredCentered  = 0;     // for software filtering of noise      // <<<
int   lastButton        = HIGH;

const int TOTAL_COLUMNS = NUM_MATS * 8;

// Mic baseline calibration
bool  baselineSet   = false;
long  calibSum      = 0;
long  calibCount    = 0;
float baseline      = 512.0;

// Animation state
float wavePhase = 0.0;       // for sine wave visualizer
int   pulseStep = 0;         // for square pulse visualizer

// -------------------- ON-BOARD MATRIX FRAMES (from Arduino docs) --------------------
// "happy" (smiley face)
const uint32_t HAPPY_FRAME[] = {
  0x00019819,
  0x80000001,
  0x0081f8000
};

// "heart"
const uint32_t HEART_FRAME[] = {
  0x3184a444,
  0x44042081,
  0x100a0040
};

// -------------------- DRAW ONBOARD MATRIX BASED ON MODE --------------------
void drawBuiltInMatrix(int mode) {
  if (mode == 0) {
    // Mode 0 = heart (bar visualizer)
    builtInMatrix.loadFrame(HEART_FRAME);
  } else {
    // Mode 1 & 2 = smiley (non-bar modes)
    builtInMatrix.loadFrame(HAPPY_FRAME);
  }
}

// -------------------- MODE 0: SOLID BAR ON EXTERNAL MATRIX --------------------
void showMode0(int barHeight) {
  for (int d = 0; d < NUM_MATS; d++) {
    for (int row = 0; row < 8; row++) {
      // logical bottom(0) to top(7)
      int  rowBottom = 7 - row;
      byte value     = (rowBottom < barHeight) ? 0xFF : 0x00;  // full row on or off

      // flip physical row so bars grow UP on your mounted panel
      lc.setRow(d, 7 - row, value);
    }
  }
}

// -------------------- MODE 1: SINE WAVE VISUALIZER --------------------
void showWaveMode(int barHeight) {
  for (int d = 0; d < NUM_MATS; d++) {
    for (int row = 0; row < 8; row++) {
      int  rowBottom = 7 - row;   // logical bottom index
      byte value     = 0;

      for (int c = 0; c < 8; c++) {
        int globalCol = d * 8 + c;

        // Wave factor based on column + phase (0..1)
        float angle  = wavePhase + globalCol * 0.5;         // spacing of the wave
        float factor = (sin(angle) + 1.0) / 2.0;            // map -1..1 -> 0..1

        // Local column height: scaled by current barHeight
        int colHeight = (int)(barHeight * factor + 0.5);
        if (colHeight > 8) colHeight = 8;
        if (colHeight < 0) colHeight = 0;

        if (rowBottom < colHeight) {
          bitSet(value, c);
        }
      }

      // Flip physical row so wave peaks go UP
      lc.setRow(d, 7 - row, value);
    }
  }
}

// -------------------- MODE 2: SQUARE PULSE WAVE VISUALIZER --------------------
void showPulseMode(int barHeight) {
  for (int d = 0; d < NUM_MATS; d++) {
    for (int row = 0; row < 8; row++) {
      int  rowBottom = 7 - row;   // logical bottom
      byte value     = 0;

      for (int c = 0; c < 8; c++) {
        int globalCol = d * 8 + c;

        // Square wave pattern in space: blocks of 4 columns on, 4 off
        bool high = ((globalCol + pulseStep) % 8) < 4;

        // When "high", show full barHeight; when "low", show nothing
        int colHeight = high ? barHeight : 0;
        if (colHeight > 8) colHeight = 8;

        if (rowBottom < colHeight) {
          bitSet(value, c);
        }
      }

      // Flip physical row so pulses go UP
      lc.setRow(d, 7 - row, value);
    }
  }
}

// -------------------- SETUP --------------------
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Init external MAX7219 matrices
  for (int d = 0; d < NUM_MATS; d++) {
    lc.shutdown(d, false);     // wake up
    lc.setIntensity(d, 8);     // brightness 0–15
    lc.clearDisplay(d);
  }

  // Init built-in matrix
  builtInMatrix.begin();

  Serial.begin(9600);
  Serial.println("Calibrating microphone baseline... stay quiet for a moment.");
}

// -------------------- LOOP --------------------
void loop() {
  // --- Button: change modes ---
  int button = digitalRead(BUTTON_PIN);
  if (button == LOW && lastButton == HIGH) {
    currentMode = (currentMode + 1) % NUM_MODES;  // 0 → 1 → 2 → 0 ...
    delay(200);   // debounce
  }
  lastButton = button;

  // ---------- STEP 1: BASELINE CALIBRATION (once at start) ----------
  if (!baselineSet) {
    int reading = analogRead(MIC_PIN);
    calibSum   += reading;
    calibCount++;

    if (calibCount >= 500) {   // ~500 samples
      baseline    = (float)calibSum / (float)calibCount;
      baselineSet = true;
      Serial.print("Baseline set to: ");
      Serial.println(baseline);
    }

    delay(1);
    return;  // don’t do visualizer until baseline is set
  }

  // ---------- STEP 2: READ MIC & CENTER ----------
  long sum = 0;
  for (int i = 0; i < 16; i++) {
    sum += analogRead(MIC_PIN);
  }

  float raw      = sum / 16.0;
  float centered = raw - baseline;
  if (centered < 0) centered = -centered;   // abs()

  // 🧼 SOFTWARE LOW-PASS FILTER TO REDUCE BUZZ / JITTER  // <<<
  filteredCentered = 0.7 * filteredCentered + 0.3 * centered;  // smooth
  centered         = filteredCentered;                          // use filtered value

  // Debug (can be commented out later)
  Serial.print("raw: ");
  Serial.print(raw);
  Serial.print("  centered(filtered): ");
  Serial.println(centered);

  // ---------- STEP 3: BAR VALUE (live, beat-reactive) ----------
  float levelAbove = 0;
  if (centered > QUIET_THRESHOLD) {
    levelAbove = centered - QUIET_THRESHOLD;
  }

  if (levelAbove <= 0) {
    // quiet → decay but not instantly dead
    smoothedLevel *= 0.35;   // smaller → faster drop, bigger → smoother
  } else {
    // sound → strong, fast attack
    smoothedLevel = 0.3 * smoothedLevel + 0.7 * levelAbove;
  }

  if (smoothedLevel < 1.0) smoothedLevel = 0;

  float fractionBar = smoothedLevel / MAX_LEVEL_DIFF;
  if (fractionBar < 0)   fractionBar = 0;
  if (fractionBar > 1.0) fractionBar = 1.0;
  int barHeight = (int)(fractionBar * 8.0 + 0.5);   // 0–8

  // ---------- STEP 4: EXTERNAL MATRIX OUTPUT ----------
  if (currentMode == 0) {
    // Mode 0: solid bar visualizer
    showMode0(barHeight);
  } else if (currentMode == 1) {
    // Mode 1: moving sine wave visualizer
    showWaveMode(barHeight);

    wavePhase += 0.25;   // speed of sine wave motion
    if (wavePhase > 6.28318) { // ~2π
      wavePhase -= 6.28318;
    }
  } else {
    // Mode 2: square pulse wave visualizer
    showPulseMode(barHeight);

    pulseStep++;         // speed of pulse movement
    if (pulseStep >= 8) {
      pulseStep = 0;
    }
  }

  // ---------- STEP 5: ONBOARD MATRIX ICON ----------
  drawBuiltInMatrix(currentMode);

  delay(30);
}
