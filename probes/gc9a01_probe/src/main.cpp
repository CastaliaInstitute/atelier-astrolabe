#include <Arduino.h>
#include <Arduino_GFX_Library.h>

struct Candidate {
  const char *name;
  int8_t mosi;
  int8_t sclk;
  int8_t dc;
  int8_t cs;
  int8_t rst;
  int8_t bl;
  bool bl_on_high;
};

static const Candidate kCandidates[] = {
    {"esp-c3-lcdkit", 0, 1, 2, 7, GFX_NOT_DEFINED, 5, true},
    {"esp-c3-lcd-ev-board", 0, 1, 4, 10, GFX_NOT_DEFINED, 5, true},
    {"gc9a01-breakout-a", 6, 4, 3, 7, 2, 10, true},
    {"gc9a01-breakout-b", 7, 6, 2, 10, 3, 5, true},
    {"gc9a01-breakout-c", 4, 6, 2, 7, 3, 10, true},
    {"c3-supermini-common", 6, 4, 2, 7, 3, 10, false},
};

static void set_backlight(const Candidate &c, bool on) {
  if (c.bl == GFX_NOT_DEFINED) {
    return;
  }
  pinMode(c.bl, OUTPUT);
  digitalWrite(c.bl, (on == c.bl_on_high) ? HIGH : LOW);
}

static void draw_probe(Arduino_GFX &gfx, const Candidate &c, int idx) {
  gfx.fillScreen(BLACK);
  delay(150);
  gfx.fillRect(0, 0, 240, 80, RED);
  gfx.fillRect(0, 80, 240, 80, GREEN);
  gfx.fillRect(0, 160, 240, 80, BLUE);
  gfx.setTextColor(WHITE, BLACK);
  gfx.setTextSize(2);
  gfx.setCursor(18, 92);
  gfx.print("GC9A01");
  gfx.setCursor(18, 118);
  gfx.printf("%d %s", idx, c.name);
  gfx.setCursor(18, 144);
  gfx.printf("M%d C%d D%d", c.mosi, c.sclk, c.dc);
  gfx.setCursor(18, 170);
  gfx.printf("CS%d BL%d", c.cs, c.bl);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("gc9a01 probe: cycling pin maps");
}

void loop() {
  for (size_t i = 0; i < sizeof(kCandidates) / sizeof(kCandidates[0]); ++i) {
    const Candidate &c = kCandidates[i];
    Serial.printf("probe %u: %s mosi=%d sclk=%d dc=%d cs=%d rst=%d bl=%d on_high=%d\n",
                  static_cast<unsigned>(i), c.name, c.mosi, c.sclk, c.dc, c.cs, c.rst, c.bl,
                  c.bl_on_high ? 1 : 0);
    set_backlight(c, true);
    Arduino_DataBus *bus = new Arduino_ESP32SPI(c.dc, c.cs, c.sclk, c.mosi, GFX_NOT_DEFINED);
    Arduino_GFX *gfx = new Arduino_GC9A01(bus, c.rst, 0, true, 240, 240);
    if (gfx->begin(40000000)) {
      draw_probe(*gfx, c, static_cast<int>(i));
    } else {
      Serial.println("probe: gfx begin failed");
    }
    delay(4500);
    set_backlight(c, false);
    delete gfx;
    delete bus;
    delay(400);
  }
}
