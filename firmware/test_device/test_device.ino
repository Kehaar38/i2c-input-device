#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================================
// I2C Config
// ================================
static const uint8_t I2C_ADDR_INPUT = 0x12;     // InputDevice (ATmega) I2C address
static const uint8_t I2C_ADDR_OLED  = 0x3C;     // SSD1306 OLED address

static const int I2C_SDA_PIN = 5;               // XIAO ESP32-S3 D4 = GPIO5
static const int I2C_SCL_PIN = 6;               // XIAO ESP32-S3 D5 = GPIO6
static const uint32_t I2C_FREQ = 100000;        // 100kHz

// Polling interval
static const uint32_t POLL_MS = 50;

// ================================
// OLED Config
// ================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// OLED更新周期（頻繁に更新しすぎるとちらつくので控えめに）
static const uint32_t OLED_UPDATE_MS = 100;

// ================================
// Protocol / Buttons
// Byte1 low 5 bits = buttons (pressed=1)
// bit0:center, bit1:up, bit2:right, bit3:down, bit4:left
// Byte1 high 3 bits = status (000 OK, 001 OVF)
// ================================
enum ButtonBits : uint8_t {
  BTN_CENTER = 1 << 0,
  BTN_UP     = 1 << 1,
  BTN_RIGHT  = 1 << 2,
  BTN_DOWN   = 1 << 3,
  BTN_LEFT   = 1 << 4,
};

// 長押し判定（ms）
static const uint32_t LONG_PRESS_MS = 800;

// ================================
// Helpers (Serial)
// ================================
static void printButtons(uint8_t b5) {
  Serial.print("buttons[");
  Serial.print((b5 & BTN_CENTER) ? "C" : "-");
  Serial.print((b5 & BTN_UP)     ? "U" : "-");
  Serial.print((b5 & BTN_RIGHT)  ? "R" : "-");
  Serial.print((b5 & BTN_DOWN)   ? "D" : "-");
  Serial.print((b5 & BTN_LEFT)   ? "L" : "-");
  Serial.print("] ");
}

static void printBinary5(uint8_t b5) {
  for (int i = 4; i >= 0; --i) Serial.print((b5 >> i) & 1);
}

static void printStatus(uint8_t st3) {
  Serial.print("status=");
  Serial.print(st3, DEC);
  if (st3 == 0) Serial.print("(OK)");
  else if (st3 == 1) Serial.print("(OVF)");
  else Serial.print("(UNK)");
}

// ================================
// Helpers (OLED)
// ================================
static void makeButtonsString(char out[8], uint8_t b5) {
  // 表示例: [--R--]
  // 並び: C U R D L
  out[0] = '[';
  out[1] = (b5 & BTN_CENTER) ? 'C' : '-';
  out[2] = (b5 & BTN_UP)     ? 'U' : '-';
  out[3] = (b5 & BTN_RIGHT)  ? 'R' : '-';
  out[4] = (b5 & BTN_DOWN)   ? 'D' : '-';
  out[5] = (b5 & BTN_LEFT)   ? 'L' : '-';
  out[6] = ']';
  out[7] = '\0';
}

static void drawCenteredText(int16_t y, const String &text, uint8_t textSize) {
  display.setTextSize(textSize);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_WIDTH - (int16_t)w) / 2;
  if (x < 0) x = 0;
  display.setCursor(x, y);
  display.print(text);
}

static void updateOLED(uint8_t buttons5, int32_t encTotal) {
  char btnStr[8];
  makeButtonsString(btnStr, buttons5);

  display.clearDisplay();

  // 見出し
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("BUTTONS");

  display.setCursor(0, 14);
  display.print("  ");
  display.println(btnStr);

  display.setCursor(0, 32);
  display.println("ENC_TOTAL");

  // 数値は中央に大きめで表示
  drawCenteredText(46, String(encTotal), 2);

  display.display();
}

// ================================
// Main
// ================================
void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println();
  Serial.println("InputDevice I2C Reader (XIAO ESP32-S3)");
  Serial.println("Reading 2 bytes: [enc_delta(int8), status3+buttons5]");
  Serial.printf("I2C(Input)=0x%02X  OLED=0x%02X  SDA(GPIO%d) SCL(GPIO%d) freq=%lu\n",
                I2C_ADDR_INPUT, I2C_ADDR_OLED, I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_FREQ);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ);

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, I2C_ADDR_OLED)) {
    Serial.println("SSD1306 allocation failed");
    while (true) delay(100);
  }
  display.clearDisplay();
  display.display();
}

// ================================
// Loop
// ================================
void loop() {
  static uint32_t lastPollMs = 0;
  static uint32_t lastOledMs = 0;

  // 前回値（変化検出用）
  static int8_t  lastEncDelta = 0;
  static uint8_t lastByte1 = 0;

  // ロータリー累積（合計）
  static int32_t encTotal = 0;

  // センター長押し判定用
  static bool centerPrevPressed = false;
  static uint32_t centerPressStartMs = 0;
  static bool longPressFired = false;

  uint32_t now = millis();
  if (now - lastPollMs < POLL_MS) return;
  lastPollMs = now;

  // 2バイト読む
  int n = Wire.requestFrom((int)I2C_ADDR_INPUT, 2);
  if (n < 2 || Wire.available() < 2) {
    Serial.println("I2C read failed: not enough bytes (check wiring/address/pullups)");
    while (Wire.available()) (void)Wire.read();
    return;
  }

  uint8_t b0 = Wire.read();
  uint8_t b1 = Wire.read();

  int8_t encDelta = (int8_t)b0;
  uint8_t buttons5 = b1 & 0x1F;
  uint8_t status3  = (b1 >> 5) & 0x07;

  // delta を累積
  if (encDelta != 0) encTotal += (int32_t)encDelta;

  // センターボタン長押しで encTotal リセット
  bool centerPressed = (buttons5 & BTN_CENTER) != 0;

  if (centerPressed && !centerPrevPressed) {
    // 押し始め
    centerPressStartMs = now;
    longPressFired = false;
  } else if (!centerPressed && centerPrevPressed) {
    // 離した
    longPressFired = false;
  }

  if (centerPressed && !longPressFired) {
    if (now - centerPressStartMs >= LONG_PRESS_MS) {
      encTotal = 0;
      longPressFired = true;

      Serial.println("enc_total reset (CENTER long press)");
    }
  }

  centerPrevPressed = centerPressed;

  // Serial出力（変化がある時だけ）
  bool changed = (encDelta != 0) || (b1 != lastByte1) || (encDelta != lastEncDelta) || (status3 != 0);
  if (changed) {
    Serial.print("enc_delta=");
    Serial.print(encDelta);

    Serial.print("  enc_total=");
    Serial.print(encTotal);

    Serial.print("  ");
    printStatus(status3);
    Serial.print("  ");
    printButtons(buttons5);
    Serial.print("bits=");
    printBinary5(buttons5);
    Serial.print("  raw=[");
    Serial.printf("%02X %02X", b0, b1);
    Serial.println("]");
  }

  // OLEDは一定周期で更新（変化が無くても追従する）
  if (now - lastOledMs >= OLED_UPDATE_MS) {
    lastOledMs = now;
    updateOLED(buttons5, encTotal);
  }

  lastEncDelta = encDelta;
  lastByte1 = b1;
}