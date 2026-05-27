#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

static constexpr int LCD_MOSI = 7;
static constexpr int LCD_SCLK = 6;
static constexpr int LCD_DC = 2;
static constexpr int LCD_CS = 10;
static constexpr int LCD_RST = 3;
static constexpr int LCD_BL = 5;

struct I2cPins {
  const char *name;
  int sda;
  int scl;
  int irq;
  int rst;
};

static const I2cPins kPinSets[] = {
    {"elecrow-c3", 4, 5, 0, -1},
    {"c3-default", 8, 9, -1, -1},
    {"free-0-1", 0, 1, -1, -1},
    {"free-1-0", 1, 0, -1, -1},
    {"free-4-8", 4, 8, 0, -1},
    {"free-8-4", 8, 4, 0, -1},
    {"usb-18-19", 18, 19, -1, -1},
    {"usb-19-18", 19, 18, -1, -1},
};

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
static bool touch_ok = false;
static I2cPins found = {};
static uint8_t found_addr = 0;
static uint8_t found_chip = 0;
static uint8_t found_fw = 0;

static constexpr uint8_t CST816_SLAVE_ADDRESS = 0x15;
static constexpr uint8_t CST816_REG_STATUS = 0x00;
static constexpr uint8_t CST816_REG_CHIP_ID = 0xA7;
static constexpr uint8_t CST816_REG_FW_VERSION = 0xA9;

static void draw_header(const char *line1, const char *line2 = nullptr) {
  gfx->fillScreen(BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(18, 34);
  gfx->print("Touch probe");
  gfx->setTextSize(1);
  gfx->setCursor(18, 68);
  gfx->print(line1);
  if (line2) {
    gfx->setCursor(18, 84);
    gfx->print(line2);
  }
}

static bool address_present(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static uint8_t find_touch_address() {
  static const uint8_t candidates[] = {
      CST816_SLAVE_ADDRESS,
      0x38,
      0x5A,
      0x1A,
      0x14,
      0x5D,
  };
  for (uint8_t addr : candidates) {
    if (address_present(addr)) {
      return addr;
    }
  }
  return 0;
}

static void draw_scan_row(int row, const I2cPins &pins, uint8_t addr) {
  const int y = 106 + row * 16;
  gfx->setCursor(18, y);
  gfx->setTextColor(addr ? GREEN : DARKGREY, BLACK);
  gfx->printf("%s SDA%d SCL%d", pins.name, pins.sda, pins.scl);
  if (addr) {
    gfx->printf(" @%02X", addr);
  }
}

static bool try_cst(const I2cPins &pins, uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(CST816_REG_CHIP_ID);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) {
    return false;
  }
  const uint8_t chip = Wire.read();
  Wire.beginTransmission(addr);
  Wire.write(CST816_REG_FW_VERSION);
  Wire.endTransmission(false);
  found_fw = Wire.requestFrom(static_cast<int>(addr), 1) == 1 ? Wire.read() : 0;
  if (chip != 0xB4 && chip != 0xB5 && chip != 0xB6 && chip != 0xB7 && chip != 0x20) {
    Serial.printf("  unexpected CST chip id 0x%02x fw 0x%02x\n", chip, found_fw);
    return false;
  }
  found = pins;
  found_addr = addr;
  found_chip = chip;
  touch_ok = true;
  return true;
}

static void scan_touch() {
  draw_header("Scanning I2C pin maps", "Touch screen while waiting");
  for (size_t i = 0; i < sizeof(kPinSets) / sizeof(kPinSets[0]); ++i) {
    const I2cPins &pins = kPinSets[i];
    Serial.printf("scan %u: %s sda=%d scl=%d irq=%d rst=%d\n",
                  static_cast<unsigned>(i), pins.name, pins.sda, pins.scl, pins.irq, pins.rst);
    Wire.end();
    delay(20);
    Wire.begin(pins.sda, pins.scl);
    Wire.setClock(100000);
    delay(80);
    const uint8_t addr = find_touch_address();
    draw_scan_row(static_cast<int>(i), pins, addr);
    Serial.printf("  addr=0x%02x\n", addr);
    if (addr == CST816_SLAVE_ADDRESS && try_cst(pins, addr)) {
      return;
    }
    delay(350);
  }
}

static void draw_found() {
  gfx->fillScreen(BLACK);
  gfx->setTextColor(GREEN, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(22, 32);
  gfx->print("TOUCH FOUND");
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(22, 72);
  gfx->printf("CST8xx id 0x%02X fw 0x%02X", found_chip, found_fw);
  gfx->setCursor(22, 90);
  gfx->printf("addr 0x%02X SDA %d SCL %d", found_addr, found.sda, found.scl);
  gfx->setCursor(22, 108);
  gfx->printf("INT %d  RST %d", found.irq, found.rst);
  gfx->setCursor(22, 136);
  gfx->print("Touch screen");
}

static void draw_point(int x, int y) {
  gfx->fillCircle(x, y, 8, RED);
  gfx->drawCircle(x, y, 14, WHITE);
  gfx->fillRect(0, 210, 240, 24, BLACK);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(1);
  gfx->setCursor(32, 216);
  gfx->printf("x=%d y=%d", x, y);
}

static bool read_touch_point(int &x, int &y) {
  uint8_t buffer[7] = {};
  Wire.beginTransmission(found_addr);
  Wire.write(CST816_REG_STATUS);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(found_addr), 7) != 7) {
    return false;
  }
  for (uint8_t &b : buffer) {
    b = Wire.read();
  }
  const uint8_t points = buffer[2] & 0x0F;
  if (points == 0 || points > 1 || buffer[2] == 0xFF) {
    return false;
  }
  x = ((buffer[3] & 0x0F) << 8) | buffer[4];
  y = ((buffer[5] & 0x0F) << 8) | buffer[6];
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_GC9A01(bus, LCD_RST, 0, true, 240, 240);
  gfx->begin(40000000);
  scan_touch();
  if (touch_ok) {
    draw_found();
  } else {
    draw_header("No CST touch found", "Need photo/pinout next");
    Serial.println("No CST touch found on candidate pins.");
  }
}

void loop() {
  if (!touch_ok) {
    delay(1000);
    return;
  }
  int x = 0;
  int y = 0;
  if (read_touch_point(x, y)) {
    Serial.printf("touch x=%d y=%d\n", x, y);
    draw_found();
    draw_point(constrain(x, 0, 239), constrain(y, 0, 239));
  }
  delay(35);
}
