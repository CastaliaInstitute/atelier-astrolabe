// Upstream: https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75C
// examples/Arduino-v3.3.5/examples/01_HelloWorld/01_HelloWorld.ino
//
// PlatformIO note: vendor bundle targets Arduino-ESP32 3.x. For stock PIO
// (core 2.x), GFX Library is taken from the registry at 1.5.0; CO5300 ctor
// needs explicit `ips = false` before width/height (see moononournation/Arduino_GFX v1.5.0).

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>

#include "pin_config.h"

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_CO5300 *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, false, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);

void setup(void) {
  Serial.begin(115200);

  Wire.begin(IIC_SDA, IIC_SCL);

  Serial.println("Arduino_GFX Hello World example");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  gfx->fillScreen(RGB565_BLACK);

  gfx->setBrightness(128);

  gfx->setCursor(10, 10);
  gfx->setTextColor(RGB565_RED);
  gfx->println("Hello World!");

  delay(5000);
}

void loop() {
  gfx->setCursor(random(gfx->width()), random(gfx->height()));
  gfx->setTextColor(random(0xffff), random(0xffff));
  gfx->setTextSize(random(6), random(6), random(2));
  gfx->println("Hello World!");
  Serial.println("loop");
  delay(200);
}
