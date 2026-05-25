#pragma once

#define XPOWERS_CHIP_AXP2101

#if defined(ASTROLABE_WAVESHARE_S3_185)

#define LCD_SDIO0 46
#define LCD_SDIO1 45
#define LCD_SDIO2 42
#define LCD_SDIO3 41
#define LCD_SCLK 40
#define LCD_RESET -1
#define LCD_CS 21
#define LCD_BL 5
#define LCD_WIDTH 360
#define LCD_HEIGHT 360

#define IIC_SDA 11
#define IIC_SCL 10
#define TP_INT 4
#define TP_RST -1

#define MYNAH_TCA9554_ADDR 0x20
#define MYNAH_EXIO_TOUCH_RST 1
#define MYNAH_EXIO_LCD_RST 2

#if defined(ASTROLABE_WAVESHARE_S3_185_V2)
#define ASTROLABE_AUDIO_CODEC_ES8311 1
#define ASTROLABE_MIC_CODEC_ES7210 1
#define PIN_I2S_BCLK 48
#define PIN_I2S_LRCK 38
#define PIN_I2S_DOUT 47
#define PIN_I2S_MCLK 2
#define PIN_MIC_BCLK 48
#define PIN_MIC_LRCK 38
#define PIN_MIC_DIN 39
#define PIN_MIC_MCLK 2

#define PA 15
#else
#define ASTROLABE_AUDIO_CODEC_PCM5101 1
#define ASTROLABE_MIC_I2S_DIGITAL 1
#define PIN_I2S_BCLK 48
#define PIN_I2S_LRCK 38
#define PIN_I2S_DOUT 47
#define PIN_I2S_MCLK -1
#define PIN_MIC_BCLK 15
#define PIN_MIC_LRCK 2
#define PIN_MIC_DIN 39
#define PIN_MIC_MCLK -1

#define PA -1
#endif

#define PIN_ES7210_BCLK PIN_MIC_BCLK
#define PIN_ES7210_LRCK PIN_MIC_LRCK
#define PIN_ES7210_DIN PIN_MIC_DIN
#define PIN_ES7210_MCLK PIN_MIC_MCLK
#define PIN_ES8311_DOUT PIN_I2S_DOUT

#else

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

#endif

/** Side BOOT button (strap GPIO); Waveshare 1.75C — see vendor LVGL+AXP examples. */
#ifndef MYNAH_BOOT_BUTTON_GPIO
#define MYNAH_BOOT_BUTTON_GPIO 0
#endif
