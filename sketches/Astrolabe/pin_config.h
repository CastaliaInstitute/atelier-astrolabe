#pragma once

#if defined(ASTROLABE_PLATFORM_C3_128) && ASTROLABE_PLATFORM_C3_128

#define LCD_MOSI 7
#define LCD_SCLK 6
#define LCD_RESET 3
#define LCD_CS 10
#define LCD_DC 2
#define LCD_BL 5
#define LCD_WIDTH 240
#define LCD_HEIGHT 240

#define IIC_SDA 4
#define IIC_SCL 5
#define TP_INT 0
#define TP_RST -1

#define PIN_ES7210_BCLK -1
#define PIN_ES7210_LRCK -1
#define PIN_ES7210_DIN -1
#define PIN_ES7210_MCLK -1
#define PIN_ES8311_DOUT -1
#define PA -1

#ifndef MYNAH_BOOT_BUTTON_GPIO
#define MYNAH_BOOT_BUTTON_GPIO 9
#endif

#else

#define XPOWERS_CHIP_AXP2101

#define LCD_SDIO0 4
#define LCD_SDIO1 5
#define LCD_SDIO2 6
#define LCD_SDIO3 7
#define LCD_SCLK 38
#define LCD_RESET 2
#define LCD_CS 12
#define LCD_WIDTH 466
#define LCD_HEIGHT 466

#define IIC_SDA 15
#define IIC_SCL 14
#define TP_INT 11
#define TP_RST 2

#define PIN_ES7210_BCLK 9
#define PIN_ES7210_LRCK 45
#define PIN_ES7210_DIN 10
#define PIN_ES7210_MCLK 16
#define PIN_ES8311_DOUT 8

#define PA 46

/** Side BOOT button (strap GPIO); Waveshare 1.75C — see vendor LVGL+AXP examples. */
#ifndef MYNAH_BOOT_BUTTON_GPIO
#define MYNAH_BOOT_BUTTON_GPIO 0
#endif

#endif
