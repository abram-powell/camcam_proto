/*
 * Exposure Triangle — Arduino Encoder/Button Handler
 * State Library of NSW / Max Dupain Exhibition
 *
 * Hardware:
 *   3x continuous-rotation rotary encoders with detents (CLK, DT pins)
 *   4–6x illuminated momentary buttons (active-low with INPUT_PULLUP)
 *
 * Serial output (115200 baud):
 *   Continuous:  A:127,S:64,I:200\n        (every loop if values changed)
 *   On button:   A:127,S:64,I:200,BTN:2\n  (BTN field only on press, not release)
 *
 * Encoder behaviour:
 *   Clamped accumulator 0–255. Clockwise = increase, counter-clockwise = decrease.
 *   Hard stops at 0 and 255 — knob continues to rotate freely at extremes.
 *   Increment of 3 per detent → full range traversed in ~43 detents (~1.5 rotations).
 */

// ---- Pin assignments ----
// Aperture encoder
const int ENC_A_CLK = 2;
const int ENC_A_DT  = 3;

// Shutter speed encoder
const int ENC_S_CLK = 4;
const int ENC_S_DT  = 5;

// ISO encoder
const int ENC_I_CLK = 6;
const int ENC_I_DT  = 7;

// Image select buttons (active-low, INPUT_PULLUP)
// Add/remove entries to match physical button count (4–6)
const int BTN_PINS[]  = { 8, 9, 10, 11 };
const int BTN_COUNT   = 4;

// ---- Encoder increment per detent ----
// Range 0–255, full traverse in ~1.5 rotations at increment 3
const int ENC_INCREMENT = 3;

// ---- Encoder state ----
struct Encoder {
  int       pin_clk;
  int       pin_dt;
  int       value;       // clamped accumulator 0–255
  int       last_clk;    // previous CLK state for edge detection
};

Encoder encAperture = { ENC_A_CLK, ENC_A_DT, 127, HIGH };
Encoder encShutter  = { ENC_S_CLK, ENC_S_DT, 127, HIGH };
Encoder encISO      = { ENC_I_CLK, ENC_I_DT, 127, HIGH };

// ---- Button debounce state ----
struct Button {
  int  pin;
  bool last_state;       // last debounced state (HIGH = not pressed with pullup)
  unsigned long last_change_ms;
};

Button buttons[BTN_COUNT];

const unsigned long DEBOUNCE_MS = 30;

// ---- Previous values (for change detection) ----
int prev_aperture = -1;
int prev_shutter  = -1;
int prev_iso      = -1;

// -------- Setup --------
void setup() {
  Serial.begin(115200);

  // Encoder pins — internal pullups
  pinMode(ENC_A_CLK, INPUT_PULLUP);
  pinMode(ENC_A_DT,  INPUT_PULLUP);
  pinMode(ENC_S_CLK, INPUT_PULLUP);
  pinMode(ENC_S_DT,  INPUT_PULLUP);
  pinMode(ENC_I_CLK, INPUT_PULLUP);
  pinMode(ENC_I_DT,  INPUT_PULLUP);

  // Seed lastClk from current pin state to avoid phantom step on startup
  encAperture.last_clk = digitalRead(ENC_A_CLK);
  encShutter.last_clk  = digitalRead(ENC_S_CLK);
  encISO.last_clk      = digitalRead(ENC_I_CLK);

  // Button pins
  for (int i = 0; i < BTN_COUNT; i++) {
    buttons[i].pin           = BTN_PINS[i];
    buttons[i].last_state    = HIGH;  // not pressed
    buttons[i].last_change_ms = 0;
    pinMode(BTN_PINS[i], INPUT_PULLUP);
  }
}

// -------- Read one encoder and update its accumulator --------
void readEncoder(Encoder &enc) {
  int clk = digitalRead(enc.pin_clk);

  // Rising edge on CLK
  if (clk == HIGH && enc.last_clk == LOW) {
    int dt = digitalRead(enc.pin_dt);
    if (dt == LOW) {
      // Clockwise — increase
      enc.value = min(255, enc.value + ENC_INCREMENT);
    } else {
      // Counter-clockwise — decrease
      enc.value = max(0, enc.value - ENC_INCREMENT);
    }
  }

  enc.last_clk = clk;
}

// -------- Send current values on Serial --------
void sendValues(int btn_index) {
  // Format: A:127,S:64,I:200\n  or  A:127,S:64,I:200,BTN:2\n
  Serial.print("A:");
  Serial.print(encAperture.value);
  Serial.print(",S:");
  Serial.print(encShutter.value);
  Serial.print(",I:");
  Serial.print(encISO.value);

  if (btn_index >= 0) {
    Serial.print(",BTN:");
    Serial.print(btn_index + 1);  // 1-indexed to match browser expectations
  }

  Serial.print("\n");
}

// -------- Main loop --------
void loop() {
  // Read all three encoders
  readEncoder(encAperture);
  readEncoder(encShutter);
  readEncoder(encISO);

  // Check if any encoder value changed — send continuous update
  bool changed = (encAperture.value != prev_aperture ||
                  encShutter.value  != prev_shutter  ||
                  encISO.value      != prev_iso);

  if (changed) {
    sendValues(-1);
    prev_aperture = encAperture.value;
    prev_shutter  = encShutter.value;
    prev_iso      = encISO.value;
  }

  // Poll buttons with debounce
  unsigned long now = millis();
  for (int i = 0; i < BTN_COUNT; i++) {
    int state = digitalRead(buttons[i].pin);

    if (state != buttons[i].last_state) {
      buttons[i].last_change_ms = now;
    }

    // Debounce settled — detect press (HIGH→LOW transition with pullup)
    if ((now - buttons[i].last_change_ms) >= DEBOUNCE_MS) {
      if (state == LOW && buttons[i].last_state == HIGH) {
        // Button just pressed — send BTN event
        sendValues(i);
      }
    }

    buttons[i].last_state = state;
  }
}
