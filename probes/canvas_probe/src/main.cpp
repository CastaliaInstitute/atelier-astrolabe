#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include "pm_display.h"

static constexpr int LCD_MOSI = 7;
static constexpr int LCD_SCLK = 6;
static constexpr int LCD_DC = 2;
static constexpr int LCD_CS = 10;
static constexpr int LCD_RST = 3;
static constexpr int LCD_BL = 5;
static constexpr int W = 240;
static constexpr int H = 240;

Arduino_DataBus *bus = nullptr;
Arduino_GC9A01 *tft = nullptr;
PmDisplayCanvas *canvas = nullptr;

static void draw_canvas_pattern(const char *title, int frame) {
  canvas->fillScreen(BLACK);
  canvas->fillRect(0, 0, W, 80, RED);
  canvas->fillRect(0, 80, W, 80, GREEN);
  canvas->fillRect(0, 160, W, 80, BLUE);
  for (int y = 0; y < H; y += 16) {
    canvas->drawFastHLine(0, y, W, WHITE);
  }
  for (int x = 0; x < W; x += 16) {
    canvas->drawFastVLine(x, 0, H, WHITE);
  }
  canvas->setTextColor(WHITE, BLACK);
  canvas->setTextSize(2);
  canvas->setCursor(18, 92);
  canvas->print(title);
  canvas->setTextSize(1);
  canvas->setCursor(18, 122);
  canvas->printf("PmDisplayCanvas %d", frame);
  canvas->flush();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED);
  tft = new Arduino_GC9A01(bus, LCD_RST, 0, true, W, H);
  canvas = new PmDisplayCanvas(W, H, tft);
  canvas->begin(40000000);
}

void loop() {
  static int frame = 0;
  draw_canvas_pattern("CANVAS", frame++);
  delay(1000);
}
