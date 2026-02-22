/*
  InputDevice (ATmega328P, 3.3V, internal 8MHz)
  - I2C スレーブとして、入力状態を 2バイト固定で返す
  - ボタン 5個 + ロータリーエンコーダ（A/B, スイッチ無し）

  I2C 応答フォーマット（Read 2 bytes）
    Byte0: enc_delta (int8_t)
      - 前回 Read 以降の差分
      - -128..127 に飽和（超えたら status=001 を立てる）
      - Read したタイミングで g_encAcc を 0 にリセット（差分式）

    Byte1: 上位3bit=status, 下位5bit=buttons
      bit0: center
      bit1: up
      bit2: right
      bit3: down
      bit4: left
      status:
        000 = 正常
        001 = overflow（飽和/内部飽和などの異常を検出）
*/

#include <Arduino.h>
#include <Wire.h>

#include <util/atomic.h>
#include <avr/interrupt.h>

// ================================
// ユーザー設定
// ================================
static const uint8_t I2C_ADDR = 0x12;

// ボタン（INPUT_PULLUP）
// 物理的には「押すとGNDへ落ちる」
// 出力（ビットマスク）は「押下=1」に揃える
static const uint8_t PIN_BTN_CENTER = PD1;
static const uint8_t PIN_BTN_UP     = PD2;
static const uint8_t PIN_BTN_RIGHT  = PD3;
static const uint8_t PIN_BTN_DOWN   = PD4;
static const uint8_t PIN_BTN_LEFT   = PD0;

// エンコーダ（A/B, INPUT_PULLUP）
static const uint8_t PIN_ENC_A = PD6; // PCINT22
static const uint8_t PIN_ENC_B = PD7; // PCINT23

// 1クリック（カチッ）あたりの「有効遷移数」
// エンコーダ個体によって 1/2/4 等があるので、実測で合わせる。
static const int8_t ENC_STEPS_PER_NOTCH = 2;

// 右回りを + にしたい場合 true（符号反転）
// もし逆だったら false にするだけでOK
static const bool ENC_DIR_REVERSE = true;

// ボタンのサンプリング周期（ms）
static const uint16_t BTN_SAMPLE_MS = 1;

// ================================
// 共有状態（ISR + I2C）
// ================================
// エンコーダ：差分累積（Readしたら0にリセット）
volatile int16_t g_encAcc = 0;

// エンコーダ：クリック換算前の途中経過（クォドラチャステップ）
// ※ これを I2C Read 毎に 0 にすると、ゆっくり回した時に反応しなくなるので保持する
volatile int8_t g_qstepAcc = 0;

// status（3bit）: 000=OK, 001=overflow
volatile uint8_t g_status3 = 0;

// ボタン（5bit, 押下=1）
volatile uint8_t g_btnMask5 = 0;

// ================================
// ヘルパ
// ================================
static inline void set_overflow_flag() {
  g_status3 = 0x01; // 001
}

static inline int8_t clamp_i16_to_i8(int16_t v, bool &clamped) {
  if (v > 127)  { clamped = true; return 127; }
  if (v < -128) { clamped = true; return -128; }
  clamped = false;
  return (int8_t)v;
}

// g_encAcc を int16 範囲で飽和加算（wrap防止）
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
// エンコーダ：クォドラチャデコード（PCINT）
// ================================
volatile uint8_t g_prevAB = 0;

static inline uint8_t readAB() {
  uint8_t a = (uint8_t)digitalRead(PIN_ENC_A);
  uint8_t b = (uint8_t)digitalRead(PIN_ENC_B);
  return (uint8_t)((a << 1) | b);
}

// A/Bの状態遷移から +1/-1 のクォドラチャステップを生成する
// 不正遷移（チャタ/ノイズ）は 0 扱いで捨てる
static inline int8_t quadrature_step(uint8_t prevAB, uint8_t currAB) {
  const uint8_t t = (uint8_t)((prevAB << 2) | currAB);

  // 0..15 の遷移に対するステップ（標準Gray系列前提）
  static const int8_t stepTable[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
  };

  int8_t s = stepTable[t];

  // 回転方向の符号を反転（右回りを + に合わせたい時に使う）
  if (ENC_DIR_REVERSE) s = (int8_t)-s;

  return s;
}

// PCINTが来たらA/B変化を1回処理する
void isr_encoder_change() {
  const uint8_t curr = readAB();
  const uint8_t prev = g_prevAB;
  g_prevAB = curr;

  int8_t qstep = quadrature_step(prev, curr);
  if (qstep == 0) return;

  // クリック換算用にクォドラチャステップを累積
  g_qstepAcc = (int8_t)(g_qstepAcc + qstep);

  // クリック単位（±1）へ変換
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

// Port D（PCINT[23:16]）のピン変化割り込み
ISR(PCINT2_vect) {
  isr_encoder_change();
}

// ================================
// ボタン：簡易デバウンス（8サンプルのシフトレジスタ）
// ================================
struct Debounce8 {
  uint8_t hist = 0xFF;      // pullup=1 なので未押下を 1 とする
  bool stablePressed = false;

  void update(bool pinHigh) {
    hist = (uint8_t)((hist << 1) | (pinHigh ? 1 : 0));
    if (hist == 0x00) stablePressed = true;   // 連続0 -> 押下安定
    else if (hist == 0xFF) stablePressed = false; // 連続1 -> 未押下安定
  }
};

Debounce8 db_center, db_up, db_right, db_down, db_left;

// 押下状態を 5bit にパック（押下=1）
static inline uint8_t packButtons5() {
  uint8_t m = 0;
  if (db_center.stablePressed) m |= (1 << 0);
  if (db_up.stablePressed)     m |= (1 << 1);
  if (db_right.stablePressed)  m |= (1 << 2);
  if (db_down.stablePressed)   m |= (1 << 3);
  if (db_left.stablePressed)   m |= (1 << 4);
  return m;
}

static inline void updateButtonsDebounced() {
  db_center.update(digitalRead(PIN_BTN_CENTER));
  db_up.update(digitalRead(PIN_BTN_UP));
  db_right.update(digitalRead(PIN_BTN_RIGHT));
  db_down.update(digitalRead(PIN_BTN_DOWN));
  db_left.update(digitalRead(PIN_BTN_LEFT));

  const uint8_t m = packButtons5();

  // g_btnMask5 はISRからも参照され得るので、原子更新にする
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    g_btnMask5 = m;
  }
}

// ================================
// I2C：Read要求が来たら 2バイト返す
// ================================
void onI2CRequest() {
  int16_t encSnap;
  uint8_t btnSnap;
  uint8_t stSnap;

  // 共有変数をスナップショットし、必要なものだけクリアする
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    encSnap = g_encAcc;
    g_encAcc = 0;     // 差分式：読んだら0にする
    // g_qstepAcc は保持（途中経過を捨てない）
    btnSnap = g_btnMask5;
    stSnap  = g_status3;
    g_status3 = 0;    // statusは読んだらクリア（必要なら保持方式に変更可）
  }

  // encSnap を int8 に飽和
  bool clamped = false;
  int8_t encOut = clamp_i16_to_i8(encSnap, clamped);
  if (clamped) stSnap = 0x01; // overflow

  const uint8_t out0 = (uint8_t)encOut;
  const uint8_t out1 = (uint8_t)(((stSnap & 0x07) << 5) | (btnSnap & 0x1F));

  Wire.write(out0);
  Wire.write(out1);
}

// ================================
// setup / loop
// ================================
uint32_t lastBtnMs = 0;

void setup() {
  // ボタン入力
  pinMode(PIN_BTN_CENTER, INPUT_PULLUP);
  pinMode(PIN_BTN_UP,     INPUT_PULLUP);
  pinMode(PIN_BTN_RIGHT,  INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN,   INPUT_PULLUP);
  pinMode(PIN_BTN_LEFT,   INPUT_PULLUP);

  // エンコーダ入力
  pinMode(PIN_ENC_A, INPUT_PULLUP);
  pinMode(PIN_ENC_B, INPUT_PULLUP);
  g_prevAB = readAB();

  // PCINT（Port D）を有効化：PD6/PD7（PCINT22/23）
  PCICR  |= (1 << PCIE2);
  PCMSK2 |= (1 << PCINT22);
  PCMSK2 |= (1 << PCINT23);

  // I2Cスレーブ開始
  Wire.begin(I2C_ADDR);
  Wire.onRequest(onI2CRequest);

  // デバウンス初期化（短時間で安定状態に寄せる）
  for (int i = 0; i < 16; i++) {
    updateButtonsDebounced();
    delay(1);
  }
  lastBtnMs = millis();
}

void loop() {
  // ボタンは一定周期で更新
  const uint32_t now = millis();
  if ((uint32_t)(now - lastBtnMs) >= BTN_SAMPLE_MS) {
    lastBtnMs = now;
    updateButtonsDebounced();
  }
}