#include <Arduino.h>
#include <Arduino_GFX_Library.h>

static constexpr int LCD_MOSI = 7;
static constexpr int LCD_SCLK = 6;
static constexpr int LCD_DC = 2;
static constexpr int LCD_CS = 10;
static constexpr int LCD_RST = 3;
static constexpr int LCD_BL = 5;
static constexpr int W = 240;
static constexpr int H = 240;

Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
uint16_t *fb = nullptr;

static void label(const char *title, const char *detail) {
  gfx->setTextColor(WHITE, BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(18, 92);
  gfx->print(title);
  gfx->setTextSize(1);
  gfx->setCursor(18, 122);
  gfx->print(detail);
}

static void fill_fb_pattern() {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      uint16_t c = BLACK;
      if (y < 80) {
        c = RED;
      } else if (y < 160) {
        c = GREEN;
      } else {
        c = BLUE;
      }
      if ((x / 12 + y / 12) & 1) {
        c ^= 0x39E7;
      }
      fb[y * W + x] = c;
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  fb = static_cast<uint16_t *>(heap_caps_malloc(W * H * sizeof(uint16_t), MALLOC_CAP_8BIT));
  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
  gfx = new Arduino_GC9A01(bus, LCD_RST, 0, true, W, H);
  gfx->begin(40000000);
  fill_fb_pattern();
}

void loop() {
  Serial.println("mode 1 direct primitives");
  gfx->fillScreen(BLACK);
  gfx->fillRect(0, 0, W, 80, RED);
  gfx->fillRect(0, 80, W, 80, GREEN);
  gfx->fillRect(0, 160, W, 80, BLUE);
  label("DIRECT", "fillRect primitives");
  delay(4000);

  Serial.println("mode 2 draw16bitRGBBitmap");
  gfx->fillScreen(BLACK);
  gfx->draw16bitRGBBitmap(0, 0, fb, W, H);
  label("RGB BLIT", "draw16bitRGBBitmap");
  delay(4000);

  Serial.println("mode 3 draw16bitBeRGBBitmap");
  gfx->fillScreen(BLACK);
  gfx->draw16bitBeRGBBitmap(0, 0, fb, W, H);
  label("BE BLIT", "draw16bitBeRGBBitmap");
  delay(4000);
}
