#pragma once

/**
 * Waveshare ESP32-S3-Touch-AMOLED-1.75C pin map.
 * Canonical copy — sketches may include via board_config.h or legacy pin_config.h.
 */
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

#ifndef MYNAH_BOOT_BUTTON_GPIO
#define MYNAH_BOOT_BUTTON_GPIO 0
#endif
