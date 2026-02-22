#include <Wire.h>

// ================================
// Config
// ================================
static const uint8_t I2C_ADDR = 0x12;     // InputDevice (ATmega) I2C address
static const int I2C_SDA_PIN = 5;         // XIAO ESP32-S3 D4 = GPIO5
static const int I2C_SCL_PIN = 6;         // XIAO ESP32-S3 D5 = GPIO6
static const uint32_t I2C_FREQ = 100000;  // 100kHz (safe)

// Polling interval
static const uint32_t POLL_MS = 50;

// Button bit mapping (Byte1 low 5 bits)
enum ButtonBits : uint8_t {
  BTN_CENTER = 1 << 0,
  BTN_UP     = 1 << 1,
  BTN_RIGHT  = 1 << 2,
  BTN_DOWN   = 1 << 3,
  BTN_LEFT   = 1 << 4,
};

// ================================
// Helpers
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
  // Print bits4..0
  for (int i = 4; i >= 0; --i) {
    Serial.print((b5 >> i) & 1);
  }
}

static void printStatus(uint8_t st3) {
  Serial.print("status=");
  Serial.print(st3, DEC);
  if (st3 == 0) Serial.print("(OK)");
  else if (st3 == 1) Serial.print("(OVF)");
  else Serial.print("(UNK)");
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
  Serial.printf("I2C addr=0x%02X SDA(GPIO%d) SCL(GPIO%d) freq=%lu\n",
                I2C_ADDR, I2C_SDA_PIN, I2C_SCL_PIN, (unsigned long)I2C_FREQ);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(I2C_FREQ);
}

void loop() {
  static uint32_t lastMs = 0;
  static int8_t lastEnc = 0;
  static uint8_t lastByte1 = 0;

  uint32_t now = millis();
  if (now - lastMs < POLL_MS) return;
  lastMs = now;

  // Request 2 bytes
  Wire.requestFrom((int)I2C_ADDR, 2);
  if (Wire.available() < 2) {
    Serial.println("I2C read failed: not enough bytes (check wiring/address/pullups)");
    // Drain if any
    while (Wire.available()) (void)Wire.read();
    return;
  }

  uint8_t b0 = Wire.read();
  uint8_t b1 = Wire.read();

  int8_t enc = (int8_t)b0;
  uint8_t buttons5 = b1 & 0x1F;
  uint8_t status3  = (b1 >> 5) & 0x07;

  // Print only when something changes OR encoder nonzero OR overflow flag
  bool changed = (enc != 0) || (b1 != lastByte1) || (enc != lastEnc) || (status3 != 0);
  if (changed) {
    Serial.print("enc_delta=");
    Serial.print(enc);
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

  lastEnc = enc;
  lastByte1 = b1;
}