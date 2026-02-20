#include <Wire.h>
#include <Arduino.h>

// ================================
// User config
// ================================
static const uint8_t I2C_ADDR = 0x12;

// Buttons (INPUT_PULLUP)
// bit0:center, bit1:up, bit2:right, bit3:down, bit4:left
static const uint8_t PIN_BTN_CENTER = 4;
static const uint8_t PIN_BTN_UP     = 5;
static const uint8_t PIN_BTN_RIGHT  = 6;
static const uint8_t PIN_BTN_DOWN   = 7;
static const uint8_t PIN_BTN_LEFT   = 8;

// Rotary encoder pins (quadrature A/B). No switch.
static const uint8_t PIN_ENC_A = 2; // INT0
static const uint8_t PIN_ENC_B = 3; // INT1

// Many encoders: 1 detent = 4 valid quadrature steps (4x decode).
// Some are 2. You can tune after you test.
static const int8_t ENC_STEPS_PER_NOTCH = 4;

// How often to sample buttons for debounce (ms)
static const uint16_t BTN_SAMPLE_MS = 1;

// ================================
// Shared state (ISR + I2C)
// ================================
volatile int16_t g_encAcc = 0;        // accumulated "notches" (can be >127)
volatile int8_t  g_qstepAcc = 0;      // accumulated quadrature steps toward one notch
volatile uint8_t g_status3 = 0;       // 3-bit status (000 normal, 001 overflow)
volatile uint8_t g_btnMask5 = 0;      // 5-bit buttons (pressed=1)

// ================================
// Helpers
// ================================
static inline void set_overflow_flag() {
  // status 001
    g_status3 = 0x01;
    }

    static inline int8_t clamp_i16_to_i8(int16_t v, bool &clamped) {
      if (v > 127) { clamped = true; return 127; }
        if (v < -128) { clamped = true; return -128; }
          clamped = false;
            return (int8_t)v;
            }

            // Safe saturating add for int16 accumulation (prevents wrap).
            static inline void encAcc_add_saturating(int16_t delta) {
              int32_t tmp = (int32_t)g_encAcc + (int32_t)delta;
                if (tmp > 32767) {
                    g_encAcc = 32767;
                        set_overflow_flag();
                          } else if (tmp < -32768) {
                              g_encAcc = -32768;
                                  set_overflow_flag();
                                    } else {
                                        g_encAcc = (int16_t)tmp;
                                          }
                                          }

                                          // ================================
                                          // Encoder ISR (quadrature 4x step)
                                          // ================================
                                          // Use a small state table: prev(2bit) -> curr(2bit)
                                          // valid transitions yield +1/-1 "quadrature step"
                                          volatile uint8_t g_prevAB = 0;

                                          static inline uint8_t readAB() {
                                            uint8_t a = (uint8_t)digitalRead(PIN_ENC_A);
                                              uint8_t b = (uint8_t)digitalRead(PIN_ENC_B);
                                                return (uint8_t)((a << 1) | b);
                                                }

                                                void isr_encoder_change() {
                                                  uint8_t curr = readAB();
                                                    uint8_t prev = g_prevAB;
                                                      g_prevAB = curr;

                                                        // transition code: prev<<2 | curr (0..15)
                                                          uint8_t t = (uint8_t)((prev << 2) | curr);

                                                            // Table for quadrature: +1, -1, or 0 (invalid/no move)
                                                              // This table assumes standard Gray sequence.
                                                                static const int8_t stepTable[16] = {
                                                                    0, -1, +1,  0,
                                                                       +1,  0,  0, -1,
                                                                          -1,  0,  0, +1,
                                                                              0, +1, -1,  0
                                                                                };

                                                                                  int8_t qstep = stepTable[t];
                                                                                    if (qstep == 0) return;

                                                                                      // Accumulate quadrature steps and convert to "notches"
                                                                                        int8_t qs = g_qstepAcc + qstep;
                                                                                          g_qstepAcc = qs;

                                                                                            if (ENC_STEPS_PER_NOTCH > 0) {
                                                                                                while (g_qstepAcc >= ENC_STEPS_PER_NOTCH) {
                                                                                                      g_qstepAcc -= ENC_STEPS_PER_NOTCH;
                                                                                                            encAcc_add_saturating(+1);
                                                                                                                }
                                                                                                                    while (g_qstepAcc <= -ENC_STEPS_PER_NOTCH) {
                                                                                                                          g_qstepAcc += ENC_STEPS_PER_NOTCH;
                                                                                                                                encAcc_add_saturating(-1);
                                                                                                                                    }
                                                                                                                                      }
                                                                                                                                      }

                                                                                                                                      // ================================
                                                                                                                                      // Button debounce (5 buttons)
                                                                                                                                      // shift-register debounce: 8-sample history
                                                                                                                                      // pressed when history becomes all 0 (because INPUT_PULLUP)
                                                                                                                                      // ================================
                                                                                                                                      struct Debounce8 {
                                                                                                                                        uint8_t hist = 0xFF; // start released (pullup=1)
                                                                                                                                          bool stablePressed = false;

                                                                                                                                            void update(bool pinHigh) {
                                                                                                                                                hist = (uint8_t)((hist << 1) | (pinHigh ? 1 : 0));
                                                                                                                                                    // stable pressed if last 8 samples are 0
                                                                                                                                                        if (hist == 0x00) stablePressed = true;
                                                                                                                                                            // stable released if last 8 samples are 1
                                                                                                                                                                else if (hist == 0xFF) stablePressed = false;
                                                                                                                                                                  }
                                                                                                                                                                  };

                                                                                                                                                                  Debounce8 db_center, db_up, db_right, db_down, db_left;

                                                                                                                                                                  // ================================
                                                                                                                                                                  // I2C onRequest: return 2 bytes
                                                                                                                                                                  // Byte0: int8 enc_delta (clamped), then reset accum to 0
                                                                                                                                                                  // Byte1: (status3<<5) | buttons5
                                                                                                                                                                  // Also clears status after read (optional; here we clear to 000 on each read)
                                                                                                                                                                  // ================================
                                                                                                                                                                  void onI2CRequest() {
                                                                                                                                                                    int16_t encSnap;
                                                                                                                                                                      uint8_t btnSnap;
                                                                                                                                                                        uint8_t stSnap;

                                                                                                                                                                          cli();
                                                                                                                                                                            encSnap = g_encAcc;
                                                                                                                                                                              g_encAcc = 0;        // reset-on-read as requested
                                                                                                                                                                                g_qstepAcc = 0;      // also reset partial steps to avoid odd carry
                                                                                                                                                                                  btnSnap = g_btnMask5;
                                                                                                                                                                                    stSnap  = g_status3;
                                                                                                                                                                                      g_status3 = 0;       // clear status after reporting (so overflow is edge-detected)
                                                                                                                                                                                        sei();

                                                                                                                                                                                          bool clamped = false;
                                                                                                                                                                                            int8_t encOut = clamp_i16_to_i8(encSnap, clamped);
                                                                                                                                                                                              if (clamped) {
                                                                                                                                                                                                  // If encSnap doesn't fit, mark overflow in the returned status
                                                                                                                                                                                                      stSnap = 0x01;
                                                                                                                                                                                                        }

                                                                                                                                                                                                          uint8_t out0 = (uint8_t)encOut;
                                                                                                                                                                                                            uint8_t out1 = (uint8_t)(((stSnap & 0x07) << 5) | (btnSnap & 0x1F));

                                                                                                                                                                                                              Wire.write(out0);
                                                                                                                                                                                                                Wire.write(out1);
                                                                                                                                                                                                                }

                                                                                                                                                                                                                // ================================
                                                                                                                                                                                                                // Setup/Loop
                                                                                                                                                                                                                // ================================
                                                                                                                                                                                                                uint32_t lastBtnMs = 0;

                                                                                                                                                                                                                static inline void updateButtonsDebounced() {
                                                                                                                                                                                                                  db_center.update(digitalRead(PIN_BTN_CENTER));
                                                                                                                                                                                                                    db_up.update(digitalRead(PIN_BTN_UP));
                                                                                                                                                                                                                      db_right.update(digitalRead(PIN_BTN_RIGHT));
                                                                                                                                                                                                                        db_down.update(digitalRead(PIN_BTN_DOWN));
                                                                                                                                                                                                                          db_left.update(digitalRead(PIN_BTN_LEFT));

                                                                                                                                                                                                                            // Pack (pressed=1)
                                                                                                                                                                                                                              uint8_t m = 0;
                                                                                                                                                                                                                                if (db_center.stablePressed) m |= (1 << 0);
                                                                                                                                                                                                                                  if (db_up.stablePressed)     m |= (1 << 1);
                                                                                                                                                                                                                                    if (db_right.stablePressed)  m |= (1 << 2);
                                                                                                                                                                                                                                      if (db_down.stablePressed)   m |= (1 << 3);
                                                                                                                                                                                                                                        if (db_left.stablePressed)   m |= (1 << 4);

                                                                                                                                                                                                                                          cli();
                                                                                                                                                                                                                                            g_btnMask5 = m;
                                                                                                                                                                                                                                              sei();
                                                                                                                                                                                                                                              }

                                                                                                                                                                                                                                              void setup() {
                                                                                                                                                                                                                                                // Buttons
                                                                                                                                                                                                                                                  pinMode(PIN_BTN_CENTER, INPUT_PULLUP);
                                                                                                                                                                                                                                                    pinMode(PIN_BTN_UP,     INPUT_PULLUP);
                                                                                                                                                                                                                                                      pinMode(PIN_BTN_RIGHT,  INPUT_PULLUP);
                                                                                                                                                                                                                                                        pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
                                                                                                                                                                                                                                                          pinMode(PIN_BTN_LEFT,   INPUT_PULLUP);

                                                                                                                                                                                                                                                            // Encoder
                                                                                                                                                                                                                                                              pinMode(PIN_ENC_A, INPUT_PULLUP);
                                                                                                                                                                                                                                                                pinMode(PIN_ENC_B, INPUT_PULLUP);
                                                                                                                                                                                                                                                                  g_prevAB = readAB();

                                                                                                                                                                                                                                                                    // Attach interrupts on both lines
                                                                                                                                                                                                                                                                      attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), isr_encoder_change, CHANGE);
                                                                                                                                                                                                                                                                        attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), isr_encoder_change, CHANGE);

                                                                                                                                                                                                                                                                          // I2C slave
                                                                                                                                                                                                                                                                            Wire.begin(I2C_ADDR);
                                                                                                                                                                                                                                                                              Wire.onRequest(onI2CRequest);

                                                                                                                                                                                                                                                                                // Prime debounce history quickly
                                                                                                                                                                                                                                                                                  for (int i = 0; i < 16; i++) {
                                                                                                                                                                                                                                                                                      updateButtonsDebounced();
                                                                                                                                                                                                                                                                                          delay(1);
                                                                                                                                                                                                                                                                                            }
                                                                                                                                                                                                                                                                                              lastBtnMs = millis();
                                                                                                                                                                                                                                                                                              }

                                                                                                                                                                                                                                                                                              void loop() {
                                                                                                                                                                                                                                                                                                uint32_t now = millis();
                                                                                                                                                                                                                                                                                                  if ((uint32_t)(now - lastBtnMs) >= BTN_SAMPLE_MS) {
                                                                                                                                                                                                                                                                                                      lastBtnMs = now;
                                                                                                                                                                                                                                                                                                          updateButtonsDebounced();
                                                                                                                                                                                                                                                                                                            }

                                                                                                                                                                                                                                                                                                              // (Optional) you can add diagnostics over Serial here during early bring-up
                                                                                                                                                                                                                                                                                                              }