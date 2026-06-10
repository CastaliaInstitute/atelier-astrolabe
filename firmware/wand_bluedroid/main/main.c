#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gattc_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "wand-btd";

#define LCD_WIDTH 360
#define LCD_HEIGHT 360
#define LCD_CS GPIO_NUM_21
#define LCD_SCLK GPIO_NUM_40
#define LCD_SDIO0 GPIO_NUM_46
#define LCD_SDIO1 GPIO_NUM_45
#define LCD_SDIO2 GPIO_NUM_42
#define LCD_SDIO3 GPIO_NUM_41
#define LCD_RST GPIO_NUM_3
#define LCD_BL GPIO_NUM_5
#define BUTTON_BOOT GPIO_NUM_0
#define IIC_SDA GPIO_NUM_11
#define IIC_SCL GPIO_NUM_10
#define TP_INT GPIO_NUM_4
#define TP_RST GPIO_NUM_1
#define TCA9554_ADDR 0x20
#define TCA9554_LCD_RST_PIN 2
#define TCA9554_TOUCH_RST_PIN 1
#define CST92XX_ADDR 0x5A
#define CST92XX_REG_READ 0xD000
#define CST92XX_ACK 0xAB
#define CST816_ADDR 0x15
#define CST816_REG_STATUS 0x00
#define CST816_REG_CHIP_ID 0xA7
#define CST816_REG_FW_VERSION 0xA9
#define CST3530_ADDR 0x1A
#define CST3530_READ_COMMAND 0xD0070000
#define CST3530_CLEAR_COMMAND 0xD00002AB
#define ST77916_QSPI_WRITE_COLOR 0x32
#define ST77916_RAMWR 0x2C

#define RGB565(r, g, b) (uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3))

typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
    uint16_t delay_ms;
} lcd_init_cmd_t;

static const lcd_init_cmd_t k_st77916_base_init[] = {
    {0x01, {0}, 0, 120},
    {0xF0, {0x08}, 1, 0}, {0xF2, {0x08}, 1, 0}, {0x9B, {0x51}, 1, 0}, {0x86, {0x53}, 1, 0},
    {0xF2, {0x80}, 1, 0}, {0xF0, {0x00}, 1, 0}, {0xF0, {0x01}, 1, 0}, {0xF1, {0x01}, 1, 0},
    {0xB0, {0x54}, 1, 0}, {0xB1, {0x3F}, 1, 0}, {0xB2, {0x2A}, 1, 0}, {0xB4, {0x46}, 1, 0},
    {0xB5, {0x34}, 1, 0}, {0xB6, {0xD5}, 1, 0}, {0xB7, {0x30}, 1, 0}, {0xBA, {0x00}, 1, 0},
    {0xBB, {0x08}, 1, 0}, {0xBC, {0x08}, 1, 0}, {0xBD, {0x00}, 1, 0}, {0xC0, {0x80}, 1, 0},
    {0xC1, {0x10}, 1, 0}, {0xC2, {0x37}, 1, 0}, {0xC3, {0x80}, 1, 0}, {0xC4, {0x10}, 1, 0},
    {0xC5, {0x37}, 1, 0}, {0xC6, {0xA9}, 1, 0}, {0xC7, {0x41}, 1, 0}, {0xC8, {0x51}, 1, 0},
    {0xC9, {0xA9}, 1, 0}, {0xCA, {0x41}, 1, 0}, {0xCB, {0x51}, 1, 0}, {0xD0, {0x91}, 1, 0},
    {0xD1, {0x68}, 1, 0}, {0xD2, {0x69}, 1, 0}, {0xF5, {0x00, 0xA5}, 2, 0}, {0xDD, {0x3F}, 1, 0},
    {0xDE, {0x3F}, 1, 0}, {0xF1, {0x10}, 1, 0}, {0xF0, {0x00}, 1, 0}, {0xF0, {0x02}, 1, 0},
    {0xE0, {0x70, 0x09, 0x12, 0x0C, 0x0B, 0x27, 0x38, 0x54, 0x4E, 0x19, 0x15, 0x15, 0x2C, 0x2F}, 14, 0},
    {0xE1, {0x70, 0x08, 0x11, 0x0C, 0x0B, 0x27, 0x38, 0x43, 0x4C, 0x18, 0x14, 0x14, 0x2B, 0x2D}, 14, 0},
    {0xF0, {0x10}, 1, 0}, {0xF3, {0x10}, 1, 0}, {0xE0, {0x08}, 1, 0}, {0xE1, {0x00}, 1, 0},
    {0xE2, {0x00}, 1, 0}, {0xE3, {0x00}, 1, 0}, {0xE4, {0xE0}, 1, 0}, {0xE5, {0x06}, 1, 0},
    {0xE6, {0x21}, 1, 0}, {0xE7, {0x00}, 1, 0}, {0xE8, {0x05}, 1, 0}, {0xE9, {0x82}, 1, 0},
    {0xEA, {0xDF}, 1, 0}, {0xEB, {0x89}, 1, 0}, {0xEC, {0x20}, 1, 0}, {0xED, {0x14}, 1, 0},
    {0xEE, {0xFF}, 1, 0}, {0xEF, {0x00}, 1, 0}, {0xF8, {0xFF}, 1, 0}, {0xF9, {0x00}, 1, 0},
    {0xFA, {0x00}, 1, 0}, {0xFB, {0x30}, 1, 0}, {0xFC, {0x00}, 1, 0}, {0xFD, {0x00}, 1, 0},
    {0xFE, {0x00}, 1, 0}, {0xFF, {0x00}, 1, 0}, {0x60, {0x42}, 1, 0}, {0x61, {0xE0}, 1, 0},
    {0x62, {0x40}, 1, 0}, {0x63, {0x40}, 1, 0}, {0x64, {0x02}, 1, 0}, {0x65, {0x00}, 1, 0},
    {0x66, {0x40}, 1, 0}, {0x67, {0x03}, 1, 0}, {0x68, {0x00}, 1, 0}, {0x69, {0x00}, 1, 0},
    {0x6A, {0x00}, 1, 0}, {0x6B, {0x00}, 1, 0}, {0x70, {0x42}, 1, 0}, {0x71, {0xE0}, 1, 0},
    {0x72, {0x40}, 1, 0}, {0x73, {0x40}, 1, 0}, {0x74, {0x02}, 1, 0}, {0x75, {0x00}, 1, 0},
    {0x76, {0x40}, 1, 0}, {0x77, {0x03}, 1, 0}, {0x78, {0x00}, 1, 0}, {0x79, {0x00}, 1, 0},
    {0x7A, {0x00}, 1, 0}, {0x7B, {0x00}, 1, 0}, {0x80, {0x48}, 1, 0}, {0x81, {0x00}, 1, 0},
    {0x82, {0x05}, 1, 0}, {0x83, {0x02}, 1, 0}, {0x84, {0xDD}, 1, 0}, {0x85, {0x00}, 1, 0},
    {0x86, {0x00}, 1, 0}, {0x87, {0x00}, 1, 0}, {0x88, {0x48}, 1, 0}, {0x89, {0x00}, 1, 0},
    {0x8A, {0x07}, 1, 0}, {0x8B, {0x02}, 1, 0}, {0x8C, {0xDF}, 1, 0}, {0x8D, {0x00}, 1, 0},
    {0x8E, {0x00}, 1, 0}, {0x8F, {0x00}, 1, 0}, {0x90, {0x48}, 1, 0}, {0x91, {0x00}, 1, 0},
    {0x92, {0x09}, 1, 0}, {0x93, {0x02}, 1, 0}, {0x94, {0xE1}, 1, 0}, {0x95, {0x00}, 1, 0},
    {0x96, {0x00}, 1, 0}, {0x97, {0x00}, 1, 0}, {0x98, {0x48}, 1, 0}, {0x99, {0x00}, 1, 0},
    {0x9A, {0x0B}, 1, 0}, {0x9B, {0x02}, 1, 0}, {0x9C, {0xE3}, 1, 0}, {0x9D, {0x00}, 1, 0},
    {0x9E, {0x00}, 1, 0}, {0x9F, {0x00}, 1, 0}, {0xA0, {0x48}, 1, 0}, {0xA1, {0x00}, 1, 0},
    {0xA2, {0x04}, 1, 0}, {0xA3, {0x02}, 1, 0}, {0xA4, {0xDC}, 1, 0}, {0xA5, {0x00}, 1, 0},
    {0xA6, {0x00}, 1, 0}, {0xA7, {0x00}, 1, 0}, {0xA8, {0x48}, 1, 0}, {0xA9, {0x00}, 1, 0},
    {0xAA, {0x06}, 1, 0}, {0xAB, {0x02}, 1, 0}, {0xAC, {0xDE}, 1, 0}, {0xAD, {0x00}, 1, 0},
    {0xAE, {0x00}, 1, 0}, {0xAF, {0x00}, 1, 0}, {0xB0, {0x48}, 1, 0}, {0xB1, {0x00}, 1, 0},
    {0xB2, {0x08}, 1, 0}, {0xB3, {0x02}, 1, 0}, {0xB4, {0xE0}, 1, 0}, {0xB5, {0x00}, 1, 0},
    {0xB6, {0x00}, 1, 0}, {0xB7, {0x00}, 1, 0}, {0xB8, {0x48}, 1, 0}, {0xB9, {0x00}, 1, 0},
    {0xBA, {0x0A}, 1, 0}, {0xBB, {0x02}, 1, 0}, {0xBC, {0xE2}, 1, 0}, {0xBD, {0x00}, 1, 0},
    {0xBE, {0x00}, 1, 0}, {0xBF, {0x00}, 1, 0}, {0xC0, {0x12}, 1, 0}, {0xC1, {0xAA}, 1, 0},
    {0xC2, {0x65}, 1, 0}, {0xC3, {0x74}, 1, 0}, {0xC4, {0x47}, 1, 0}, {0xC5, {0x56}, 1, 0},
    {0xC6, {0x00}, 1, 0}, {0xC7, {0x88}, 1, 0}, {0xC8, {0x99}, 1, 0}, {0xC9, {0x33}, 1, 0},
    {0xD0, {0x21}, 1, 0}, {0xD1, {0xAA}, 1, 0}, {0xD2, {0x65}, 1, 0}, {0xD3, {0x74}, 1, 0},
    {0xD4, {0x47}, 1, 0}, {0xD5, {0x56}, 1, 0}, {0xD6, {0x00}, 1, 0}, {0xD7, {0x88}, 1, 0},
    {0xD8, {0x99}, 1, 0}, {0xD9, {0x33}, 1, 0}, {0xF3, {0x01}, 1, 0}, {0xF0, {0x00}, 1, 0},
    {0xF0, {0x01}, 1, 0}, {0xF1, {0x01}, 1, 0}, {0xA0, {0x0B}, 1, 0}, {0xA3, {0x2A}, 1, 0},
    {0xA5, {0xC3}, 1, 1}, {0xA3, {0x2B}, 1, 0}, {0xA5, {0xC3}, 1, 1}, {0xA3, {0x2C}, 1, 0},
    {0xA5, {0xC3}, 1, 1}, {0xA3, {0x2D}, 1, 0}, {0xA5, {0xC3}, 1, 1}, {0xA3, {0x2E}, 1, 0},
    {0xA5, {0xC3}, 1, 1}, {0xA3, {0x2F}, 1, 0}, {0xA5, {0xC3}, 1, 1}, {0xA3, {0x30}, 1, 0},
    {0xA5, {0xC3}, 1, 1}, {0xA3, {0x31}, 1, 0}, {0xA5, {0xC3}, 1, 1}, {0xA3, {0x32}, 1, 0},
    {0xA5, {0xC3}, 1, 1}, {0xA3, {0x33}, 1, 0}, {0xA5, {0xC3}, 1, 1}, {0xA0, {0x09}, 1, 0},
    {0xF1, {0x10}, 1, 0}, {0xF0, {0x00}, 1, 0}, {0x2A, {0x00, 0x00, 0x01, 0x67}, 4, 0},
    {0x2B, {0x01, 0x68, 0x01, 0x68}, 4, 0}, {0x4D, {0x00}, 1, 0}, {0x4E, {0x00}, 1, 0},
    {0x4F, {0x00}, 1, 0}, {0x4C, {0x01}, 1, 10}, {0x4C, {0x00}, 1, 0},
    {0x2A, {0x00, 0x00, 0x01, 0x67}, 4, 0}, {0x4C, {0x00}, 1, 0}, {0x2B, {0x00, 0x00, 0x01, 0x67}, 4, 0},
    {0x21, {0x00}, 1, 0}, {0x3A, {0x55}, 1, 0}, {0x11, {0x00}, 1, 120}, {0x29, {0x00}, 1, 0},
};

#if !defined(WAND_BOARD_WAVESHARE_S3_185B)
static const lcd_init_cmd_t k_waveshare185c_v2_init[] = {
  {0xF0, {0x28}, 1, 0}, {0xF2, {0x28}, 1, 0}, {0x73, {0xF0}, 1, 0}, {0x7C, {0xD1}, 1, 0},
  {0x83, {0xE0}, 1, 0}, {0x84, {0x61}, 1, 0}, {0xF2, {0x82}, 1, 0}, {0xF0, {0x00}, 1, 0},
  {0xF0, {0x01}, 1, 0}, {0xF1, {0x01}, 1, 0}, {0xB0, {0x56}, 1, 0}, {0xB1, {0x4D}, 1, 0},
  {0xB2, {0x24}, 1, 0}, {0xB4, {0x87}, 1, 0}, {0xB5, {0x44}, 1, 0}, {0xB6, {0x8B}, 1, 0},
  {0xB7, {0x40}, 1, 0}, {0xB8, {0x86}, 1, 0}, {0xBA, {0x00}, 1, 0}, {0xBB, {0x08}, 1, 0},
  {0xBC, {0x08}, 1, 0}, {0xBD, {0x00}, 1, 0}, {0xC0, {0x80}, 1, 0}, {0xC1, {0x10}, 1, 0},
  {0xC2, {0x37}, 1, 0}, {0xC3, {0x80}, 1, 0}, {0xC4, {0x10}, 1, 0}, {0xC5, {0x37}, 1, 0},
  {0xC6, {0xA9}, 1, 0}, {0xC7, {0x41}, 1, 0}, {0xC8, {0x01}, 1, 0}, {0xC9, {0xA9}, 1, 0},
  {0xCA, {0x41}, 1, 0}, {0xCB, {0x01}, 1, 0}, {0xD0, {0x91}, 1, 0}, {0xD1, {0x68}, 1, 0},
  {0xD2, {0x68}, 1, 0}, {0xF5, {0x00, 0xA5}, 2, 0}, {0xDD, {0x4F}, 1, 0}, {0xDE, {0x4F}, 1, 0},
  {0xF1, {0x10}, 1, 0}, {0xF0, {0x00}, 1, 0}, {0xF0, {0x02}, 1, 0},
  {0xE0, {0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
  {0xE1, {0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
  {0xF0, {0x10}, 1, 0}, {0xF3, {0x10}, 1, 0}, {0xE0, {0x07}, 1, 0}, {0xE1, {0x00}, 1, 0},
  {0xE2, {0x00}, 1, 0}, {0xE3, {0x00}, 1, 0}, {0xE4, {0xE0}, 1, 0}, {0xE5, {0x06}, 1, 0},
  {0xE6, {0x21}, 1, 0}, {0xE7, {0x01}, 1, 0}, {0xE8, {0x05}, 1, 0}, {0xE9, {0x02}, 1, 0},
  {0xEA, {0xDA}, 1, 0}, {0xEB, {0x00}, 1, 0}, {0xEC, {0x00}, 1, 0}, {0xED, {0x0F}, 1, 0},
  {0xEE, {0x00}, 1, 0}, {0xEF, {0x00}, 1, 0}, {0xF8, {0x00}, 1, 0}, {0xF9, {0x00}, 1, 0},
  {0xFA, {0x00}, 1, 0}, {0xFB, {0x00}, 1, 0}, {0xFC, {0x00}, 1, 0}, {0xFD, {0x00}, 1, 0},
  {0xFE, {0x00}, 1, 0}, {0xFF, {0x00}, 1, 0}, {0x60, {0x40}, 1, 0}, {0x61, {0x04}, 1, 0},
  {0x62, {0x00}, 1, 0}, {0x63, {0x42}, 1, 0}, {0x64, {0xD9}, 1, 0}, {0x65, {0x00}, 1, 0},
  {0x66, {0x00}, 1, 0}, {0x67, {0x00}, 1, 0}, {0x68, {0x00}, 1, 0}, {0x69, {0x00}, 1, 0},
  {0x6A, {0x00}, 1, 0}, {0x6B, {0x00}, 1, 0}, {0x70, {0x40}, 1, 0}, {0x71, {0x03}, 1, 0},
  {0x72, {0x00}, 1, 0}, {0x73, {0x42}, 1, 0}, {0x74, {0xD8}, 1, 0}, {0x75, {0x00}, 1, 0},
  {0x76, {0x00}, 1, 0}, {0x77, {0x00}, 1, 0}, {0x78, {0x00}, 1, 0}, {0x79, {0x00}, 1, 0},
  {0x7A, {0x00}, 1, 0}, {0x7B, {0x00}, 1, 0}, {0x80, {0x48}, 1, 0}, {0x81, {0x00}, 1, 0},
  {0x82, {0x06}, 1, 0}, {0x83, {0x02}, 1, 0}, {0x84, {0xD6}, 1, 0}, {0x85, {0x04}, 1, 0},
  {0x86, {0x00}, 1, 0}, {0x87, {0x00}, 1, 0}, {0x88, {0x48}, 1, 0}, {0x89, {0x00}, 1, 0},
  {0x8A, {0x08}, 1, 0}, {0x8B, {0x02}, 1, 0}, {0x8C, {0xD8}, 1, 0}, {0x8D, {0x04}, 1, 0},
  {0x8E, {0x00}, 1, 0}, {0x8F, {0x00}, 1, 0}, {0x90, {0x48}, 1, 0}, {0x91, {0x00}, 1, 0},
  {0x92, {0x0A}, 1, 0}, {0x93, {0x02}, 1, 0}, {0x94, {0xDA}, 1, 0}, {0x95, {0x04}, 1, 0},
  {0x96, {0x00}, 1, 0}, {0x97, {0x00}, 1, 0}, {0x98, {0x48}, 1, 0}, {0x99, {0x00}, 1, 0},
  {0x9A, {0x0C}, 1, 0}, {0x9B, {0x02}, 1, 0}, {0x9C, {0xDC}, 1, 0}, {0x9D, {0x04}, 1, 0},
  {0x9E, {0x00}, 1, 0}, {0x9F, {0x00}, 1, 0}, {0xA0, {0x48}, 1, 0}, {0xA1, {0x00}, 1, 0},
  {0xA2, {0x05}, 1, 0}, {0xA3, {0x02}, 1, 0}, {0xA4, {0xD5}, 1, 0}, {0xA5, {0x04}, 1, 0},
  {0xA6, {0x00}, 1, 0}, {0xA7, {0x00}, 1, 0}, {0xA8, {0x48}, 1, 0}, {0xA9, {0x00}, 1, 0},
  {0xAA, {0x07}, 1, 0}, {0xAB, {0x02}, 1, 0}, {0xAC, {0xD7}, 1, 0}, {0xAD, {0x04}, 1, 0},
  {0xAE, {0x00}, 1, 0}, {0xAF, {0x00}, 1, 0}, {0xB0, {0x48}, 1, 0}, {0xB1, {0x00}, 1, 0},
  {0xB2, {0x09}, 1, 0}, {0xB3, {0x02}, 1, 0}, {0xB4, {0xD9}, 1, 0}, {0xB5, {0x04}, 1, 0},
  {0xB6, {0x00}, 1, 0}, {0xB7, {0x00}, 1, 0}, {0xB8, {0x48}, 1, 0}, {0xB9, {0x00}, 1, 0},
  {0xBA, {0x0B}, 1, 0}, {0xBB, {0x02}, 1, 0}, {0xBC, {0xDB}, 1, 0}, {0xBD, {0x04}, 1, 0},
  {0xBE, {0x00}, 1, 0}, {0xBF, {0x00}, 1, 0}, {0xC0, {0x10}, 1, 0}, {0xC1, {0x47}, 1, 0},
  {0xC2, {0x56}, 1, 0}, {0xC3, {0x65}, 1, 0}, {0xC4, {0x74}, 1, 0}, {0xC5, {0x88}, 1, 0},
  {0xC6, {0x99}, 1, 0}, {0xC7, {0x01}, 1, 0}, {0xC8, {0xBB}, 1, 0}, {0xC9, {0xAA}, 1, 0},
  {0xD0, {0x10}, 1, 0}, {0xD1, {0x47}, 1, 0}, {0xD2, {0x56}, 1, 0}, {0xD3, {0x65}, 1, 0},
  {0xD4, {0x74}, 1, 0}, {0xD5, {0x88}, 1, 0}, {0xD6, {0x99}, 1, 0}, {0xD7, {0x01}, 1, 0},
  {0xD8, {0xBB}, 1, 0}, {0xD9, {0xAA}, 1, 0}, {0xF3, {0x01}, 1, 0}, {0xF0, {0x00}, 1, 0},
  {0x21, {0x00}, 1, 0}, {0x11, {0x00}, 1, 120}, {0x29, {0x00}, 1, 0},
};
#endif

static const uint8_t UUID_UART_SERVICE[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0xf0, 0xff, 0x40, 0x6e};
static const uint8_t UUID_UART_WRITE[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e};
static const uint8_t UUID_UART_NOTIFY[16] = {
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e};
static const uint8_t UUID_MAIN_SERVICE[16] = {
    0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf,
    0x47, 0x4e, 0x11, 0xd7, 0x28, 0xf7, 0x5b, 0xde};
static const uint8_t UUID_MAIN_WRITE[16] = {
    0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf,
    0x47, 0x4e, 0x11, 0xd7, 0x2a, 0xf7, 0x5b, 0xde};
static const uint8_t UUID_MAIN_NOTIFY[16] = {
    0xc7, 0x5d, 0x2a, 0x01, 0xe3, 0x65, 0x26, 0xaf,
    0x47, 0x4e, 0x11, 0xd7, 0x29, 0xf7, 0x5b, 0xde};

typedef struct {
    const char *name;
    const uint8_t *svc_uuid;
    const uint8_t *write_uuid;
    const uint8_t *notify_uuid;
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t write_handle;
    uint16_t notify_handle;
    uint16_t cccd_handle;
    bool found;
    bool subscribed;
} ring_service_t;

static ring_service_t s_uart = {
    .name = "uart",
    .svc_uuid = UUID_UART_SERVICE,
    .write_uuid = UUID_UART_WRITE,
    .notify_uuid = UUID_UART_NOTIFY,
};
static ring_service_t s_main = {
    .name = "main",
    .svc_uuid = UUID_MAIN_SERVICE,
    .write_uuid = UUID_MAIN_WRITE,
    .notify_uuid = UUID_MAIN_NOTIFY,
};

static esp_ble_scan_params_t s_scan_params = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x20,
    .scan_window = 0x20,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static uint32_t s_seen;
static esp_gatt_if_t s_gattc_if = ESP_GATT_IF_NONE;
static uint16_t s_conn_id;
static esp_bd_addr_t s_remote_bda;
static bool s_connecting;
static bool s_connected;
static int s_target_rssi;
static uint8_t s_pending_subscribes;
static bool s_sent_enable;
static uint8_t s_raw_attempt;
static bool s_tx_busy;
static TaskHandle_t s_rssi_task;

typedef struct {
    uint16_t handle;
    uint16_t len;
    uint8_t data[20];
    char label[24];
} tx_item_t;

typedef enum {
    CAL_IDLE,
    CAL_WAIT_FORWARD,
    CAL_WAIT_VERTICAL,
    CAL_WAIT_RIGHT,
    CAL_DONE,
} cal_stage_t;

typedef enum {
    TOUCH_NONE,
    TOUCH_CST92XX,
    TOUCH_CST3530,
    TOUCH_CST816,
} touch_proto_t;

#define TX_QUEUE_LEN 12
static tx_item_t s_tx_queue[TX_QUEUE_LEN];
static uint8_t s_tx_head;
static uint8_t s_tx_tail;
static uint8_t s_tx_count;

static spi_device_handle_t s_lcd_spi;
static uint16_t *s_fb;
static uint8_t *s_tx_line;
static bool s_lcd_ready;
static int16_t s_raw_x;
static int16_t s_raw_y;
static int16_t s_raw_z;
static float s_raw_g_x;
static float s_raw_g_y;
static float s_raw_g_z;
static float s_wand_x;
static float s_wand_y;
static float s_wand_z;
static float s_g_x;
static float s_g_y;
static float s_g_z;
static float s_center_x;
static float s_center_y;
static float s_center_z;
static float s_center_accum_x;
static float s_center_accum_y;
static float s_center_accum_z;
static float s_cal_up[3];
static float s_cal_down[3];
static float s_cal_right[3];
static float s_cal_left[3];
static float s_cal_forward[3];
static float s_cal_vertical[3];
static float s_cal_body_right[3];
static float s_cal_accum[3];
static bool s_cal_forward_valid;
static bool s_cal_vertical_valid;
static bool s_cal_body_right_valid;
static bool s_cal_sequence_active;
static uint32_t s_sample_count;
static uint32_t s_tap_count;
static TickType_t s_last_sample_tick;
static TickType_t s_last_tap_tick;
static TickType_t s_tap_flash_until;
static uint8_t s_center_samples_left;
static uint8_t s_cal_samples_left;
static cal_stage_t s_cal_stage = CAL_IDLE;
static cal_stage_t s_cal_capture_stage = CAL_IDLE;
static bool s_center_valid;
static bool s_cal_valid;
static bool s_touch_ok;
static touch_proto_t s_touch_proto = TOUCH_NONE;
static uint8_t s_touch_addr;
static uint8_t s_i2c_addrs[8];
static uint8_t s_i2c_count;
static uint8_t s_touch_n;
static int16_t s_touch_x[2];
static int16_t s_touch_y[2];
static uint32_t s_touch_events;
static uint32_t s_touch_bad_frames;
static uint8_t s_touch_irq_level = 1;
static TickType_t s_last_touch_tick;
static TickType_t s_last_cal_touch_tick;
static char s_pose_name[12] = "WAIT";
static uint8_t s_pose_conf;
static float s_pose_body_x;
static float s_pose_body_y;
static float s_pose_body_z = 1.0f;
static bool s_pose_static;
static uint8_t s_cross_step;
static const char *s_cross_last_pose;
static TickType_t s_cross_pose_since;
static TickType_t s_cross_complete_until;
static bool s_prev_raw_valid;
static int16_t s_prev_raw_x;
static int16_t s_prev_raw_y;
static int16_t s_prev_raw_z;
static const char *s_ring_status = "boot";

static void try_next_raw_mode(void);
static void request_recenter(TickType_t now);
static void calibration_button_press(TickType_t now);
static void start_touch_cal_capture(cal_stage_t stage, TickType_t now);
static void update_calibration(float gx, float gy, float gz);
static void update_pose(float gx, float gy, float gz);

static esp_err_t i2c_write_bytes(uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len) {
        i2c_master_write(cmd, data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(60));
    i2c_cmd_link_delete(cmd);
    return err;
}

static esp_err_t i2c_write_read_bytes(uint8_t addr, const uint8_t *wr, size_t wr_len, uint8_t *rd, size_t rd_len)
{
    if (!rd || rd_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (wr_len) {
        i2c_master_write(cmd, wr, wr_len, true);
    }
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (rd_len > 1) {
        i2c_master_read(cmd, rd, rd_len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, rd + rd_len - 1, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(60));
    i2c_cmd_link_delete(cmd);
    return err;
}

static bool i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

#if !defined(WAND_BOARD_WAVESHARE_S3_185B)
static void tca9554_write(uint8_t reg, uint8_t value)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, value, true);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tca9554 write reg=0x%02x failed: %s", reg, esp_err_to_name(err));
    }
}

static bool tca9554_read(uint8_t reg, uint8_t *value)
{
    if (!value) {
        return false;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TCA9554_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, value, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    esp_err_t err = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err == ESP_OK;
}

static void tca9554_set_pin(uint8_t pin, bool high)
{
    uint8_t out = 0;
    (void)tca9554_read(0x01, &out);
    const uint8_t mask = (uint8_t)(1u << (pin - 1u));
    out = high ? (uint8_t)(out | mask) : (uint8_t)(out & ~mask);
    tca9554_write(0x01, out);
}
#endif

static void direct_reset_pin(gpio_num_t pin, bool high)
{
    gpio_set_level(pin, high ? 1 : 0);
}

#if defined(WAND_BOARD_WAVESHARE_S3_185B)
esp_err_t esp_ble_gap_set_scan_params(esp_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_ble_gap_start_scanning(uint32_t duration)
{
    (void)duration;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t esp_ble_gap_stop_scanning(void)
{
    return ESP_OK;
}

esp_err_t esp_ble_gattc_open(esp_gatt_if_t gattc_if,
                             esp_bd_addr_t remote_bda,
                             esp_ble_addr_type_t remote_addr_type,
                             bool is_direct)
{
    (void)gattc_if;
    (void)remote_bda;
    (void)remote_addr_type;
    (void)is_direct;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

static void lcd_cs(bool active)
{
    gpio_set_level(LCD_CS, active ? 0 : 1);
}

static esp_err_t lcd_tx(uint8_t cmd, uint32_t addr, const void *data, size_t len, uint32_t flags)
{
    spi_transaction_ext_t trans = {0};
    trans.base.flags = flags;
    trans.base.cmd = cmd;
    trans.base.addr = addr;
    trans.base.tx_buffer = data;
    trans.base.length = len * 8;
    return spi_device_polling_transmit(s_lcd_spi, (spi_transaction_t *)&trans);
}

static void lcd_cmd_params(uint8_t cmd, const uint8_t *data, size_t len)
{
    if (!s_lcd_spi) {
        return;
    }
    lcd_cs(true);
    ESP_ERROR_CHECK(lcd_tx(0x02, ((uint32_t)cmd) << 8, len ? data : NULL, len,
                           SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR));
    lcd_cs(false);
}

static void lcd_apply_init_table(const lcd_init_cmd_t *table, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        lcd_cmd_params(table[i].cmd, table[i].data, table[i].len);
        if (table[i].delay_ms) {
            vTaskDelay(pdMS_TO_TICKS(table[i].delay_ms));
        }
    }
}

static void lcd_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint8_t col[4] = {x >> 8, x & 0xff, (uint8_t)((x + w - 1) >> 8), (uint8_t)((x + w - 1) & 0xff)};
    uint8_t row[4] = {y >> 8, y & 0xff, (uint8_t)((y + h - 1) >> 8), (uint8_t)((y + h - 1) & 0xff)};
    lcd_cmd_params(0x2a, col, sizeof(col));
    lcd_cmd_params(0x2b, row, sizeof(row));
    lcd_cmd_params(0x2c, NULL, 0);
}

static void lcd_flush(void)
{
    if (!s_lcd_ready || !s_fb || !s_tx_line) {
        return;
    }
    lcd_set_window(0, 0, LCD_WIDTH, LCD_HEIGHT);
    lcd_cs(true);
    bool first = true;
    const size_t chunk_px = 1024;
    size_t off = 0;
    const size_t total = LCD_WIDTH * LCD_HEIGHT;
    while (off < total) {
        size_t n = total - off;
        if (n > chunk_px) {
            n = chunk_px;
        }
        for (size_t i = 0; i < n; ++i) {
            uint16_t c = s_fb[off + i];
            s_tx_line[i * 2] = c >> 8;
            s_tx_line[i * 2 + 1] = c & 0xff;
        }
        if (first) {
            ESP_ERROR_CHECK(lcd_tx(ST77916_QSPI_WRITE_COLOR, ((uint32_t)ST77916_RAMWR) << 8,
                                   s_tx_line, n * 2, SPI_TRANS_MODE_QIO));
            first = false;
        } else {
            ESP_ERROR_CHECK(lcd_tx(0, 0, s_tx_line, n * 2,
                                   SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                                       SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY));
        }
        off += n;
    }
    lcd_cs(false);
}

static void fb_fill(uint16_t color)
{
    if (!s_fb) {
        return;
    }
    for (size_t i = 0; i < LCD_WIDTH * LCD_HEIGHT; ++i) {
        s_fb[i] = color;
    }
}

static void fb_px(int x, int y, uint16_t color)
{
    if (!s_fb || x < 0 || y < 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) {
        return;
    }
    s_fb[y * LCD_WIDTH + x] = color;
}

static void fb_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            fb_px(xx, yy, color);
        }
    }
}

static void fb_circle(int cx, int cy, int r, uint16_t color)
{
    int r2 = r * r;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            if (x * x + y * y <= r2) {
                fb_px(cx + x, cy + y, color);
            }
        }
    }
}

static void fb_ring(int cx, int cy, int r, uint16_t color)
{
    int outer = r * r;
    int inner = (r - 3) * (r - 3);
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            int d = x * x + y * y;
            if (d <= outer && d >= inner) {
                fb_px(cx + x, cy + y, color);
            }
        }
    }
}

static void fb_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        fb_px(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int edge2(int ax, int ay, int bx, int by, int px, int py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void fb_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t color)
{
    int min_x = x0;
    int max_x = x0;
    int min_y = y0;
    int max_y = y0;
    if (x1 < min_x) min_x = x1;
    if (x2 < min_x) min_x = x2;
    if (x1 > max_x) max_x = x1;
    if (x2 > max_x) max_x = x2;
    if (y1 < min_y) min_y = y1;
    if (y2 < min_y) min_y = y2;
    if (y1 > max_y) max_y = y1;
    if (y2 > max_y) max_y = y2;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= LCD_WIDTH) max_x = LCD_WIDTH - 1;
    if (max_y >= LCD_HEIGHT) max_y = LCD_HEIGHT - 1;

    int area = edge2(x0, y0, x1, y1, x2, y2);
    if (area == 0) {
        return;
    }
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            int w0 = edge2(x1, y1, x2, y2, x, y);
            int w1 = edge2(x2, y2, x0, y0, x, y);
            int w2 = edge2(x0, y0, x1, y1, x, y);
            if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                fb_px(x, y, color);
            }
        }
    }
}

static void fb_text(int x, int y, const char *text, uint16_t color, int scale);

static void project_axis_point(float x, float y, float z, int cx, int cy, float scale, int *sx, int *sy)
{
    *sx = cx + (int)lrintf((x + y * 0.42f) * scale);
    *sy = cy - (int)lrintf((z + y * 0.24f) * scale);
}

static void draw_axis_label(int cx, int cy, float x, float y, float z, const char *label, uint16_t color)
{
    int sx, sy;
    project_axis_point(x, y, z, cx, cy, 1.0f, &sx, &sy);
    fb_text(sx - 6, sy - 4, label, color, 1);
}

static void draw_axis_arrow(int cx, int cy, float x, float y, float z, const char *label, uint16_t color)
{
    int ox, oy;
    int tx, ty;
    project_axis_point(0.0f, 0.0f, 0.0f, cx, cy, 1.0f, &ox, &oy);
    project_axis_point(x, y, z, cx, cy, 1.0f, &tx, &ty);
    fb_line(ox, oy, tx, ty, color);
    fb_circle(tx, ty, 3, color);
    draw_axis_label(cx, cy, x * 1.08f, y * 1.08f, z * 1.08f, label, color);
}

static void draw_cal_target(int cx, int cy, float x, float y, float z, const char *label,
                            cal_stage_t stage, uint16_t color, uint16_t dim)
{
    int sx, sy;
    const bool active = s_cal_stage == stage;
    project_axis_point(x, y, z, cx, cy, 1.0f, &sx, &sy);
    fb_ring(sx, sy, active ? 28 : 22, active ? color : dim);
    fb_circle(sx, sy, active ? 8 : 5, active ? color : dim);
    fb_text(sx - 6, sy - 34, label, active ? color : dim, 1);
}

static void draw_projected_wand(int cx, int cy, float right, float forward, float up,
                                uint16_t accent, uint16_t text)
{
    float side = right * 0.55f;
    if (up > 1.0f) up = 1.0f;
    if (up < -1.0f) up = -1.0f;
    if (forward > 1.0f) forward = 1.0f;
    if (forward < -1.0f) forward = -1.0f;
    if (side > 0.55f) side = 0.55f;
    if (side < -0.55f) side = -0.55f;

    const float len = 88.0f;
    const float half_base = 10.0f;
    const float base_y = -48.0f;
    float tx3 = side * len;
    float ty3 = forward * len;
    float tz3 = up * len;
    float lx3 = -half_base + side * base_y;
    float ly3 = forward * base_y;
    float lz3 = up * base_y;
    float rx3 = half_base + side * base_y;
    float ry3 = forward * base_y;
    float rz3 = up * base_y;

    int tx, ty;
    int lx, ly;
    int rx, ry;
    int ridge_x, ridge_y;
    project_axis_point(tx3, ty3, tz3, cx, cy, 1.0f, &tx, &ty);
    project_axis_point(lx3, ly3, lz3, cx, cy, 1.0f, &lx, &ly);
    project_axis_point(rx3, ry3, rz3, cx, cy, 1.0f, &rx, &ry);
    project_axis_point(side * 18.0f, forward * 18.0f, up * 18.0f + 12.0f, cx, cy, 1.0f, &ridge_x, &ridge_y);

    fb_triangle(tx, ty, lx, ly, rx, ry, RGB565(22, 83, 86));
    fb_line(tx, ty, lx, ly, accent);
    fb_line(tx, ty, rx, ry, accent);
    fb_line(lx, ly, rx, ry, RGB565(42, 130, 126));
    fb_line(tx, ty, ridge_x, ridge_y, text);
    fb_circle(tx, ty, 4, RGB565(255, 236, 126));
    fb_line(cx, cy, tx, ty, RGB565(255, 236, 126));

    char line[16];
    snprintf(line, sizeof(line), "X:%+d", (int)lrintf(side * 100.0f));
    fb_text(cx - 62, cy + 70, line, text, 1);
    snprintf(line, sizeof(line), "Y:%+d", (int)lrintf(forward * 100.0f));
    fb_text(cx - 14, cy + 70, line, text, 1);
    snprintf(line, sizeof(line), "Z:%+d", (int)lrintf(up * 100.0f));
    fb_text(cx + 34, cy + 70, line, text, 1);
}

static uint8_t glyph5x7(char c, uint8_t row)
{
    static const uint8_t digits[10][7] = {
        {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e},
        {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e},
        {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f},
        {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e},
        {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02},
        {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e},
        {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e},
        {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e},
        {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c},
    };
    if (c >= '0' && c <= '9') {
        return digits[c - '0'][row];
    }
    switch (c) {
    case 'A': return (uint8_t[]){0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}[row];
    case 'B': return (uint8_t[]){0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e}[row];
    case 'C': return (uint8_t[]){0x0f,0x10,0x10,0x10,0x10,0x10,0x0f}[row];
    case 'D': return (uint8_t[]){0x1e,0x11,0x11,0x11,0x11,0x11,0x1e}[row];
    case 'E': return (uint8_t[]){0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f}[row];
    case 'F': return (uint8_t[]){0x1f,0x10,0x10,0x1e,0x10,0x10,0x10}[row];
    case 'G': return (uint8_t[]){0x0f,0x10,0x10,0x13,0x11,0x11,0x0f}[row];
    case 'H': return (uint8_t[]){0x11,0x11,0x11,0x1f,0x11,0x11,0x11}[row];
    case 'I': return (uint8_t[]){0x0e,0x04,0x04,0x04,0x04,0x04,0x0e}[row];
    case 'L': return (uint8_t[]){0x10,0x10,0x10,0x10,0x10,0x10,0x1f}[row];
    case 'M': return (uint8_t[]){0x11,0x1b,0x15,0x15,0x11,0x11,0x11}[row];
    case 'N': return (uint8_t[]){0x11,0x19,0x15,0x13,0x11,0x11,0x11}[row];
    case 'O': return (uint8_t[]){0x0e,0x11,0x11,0x11,0x11,0x11,0x0e}[row];
    case 'P': return (uint8_t[]){0x1e,0x11,0x11,0x1e,0x10,0x10,0x10}[row];
    case 'R': return (uint8_t[]){0x1e,0x11,0x11,0x1e,0x14,0x12,0x11}[row];
    case 'S': return (uint8_t[]){0x0f,0x10,0x10,0x0e,0x01,0x01,0x1e}[row];
    case 'T': return (uint8_t[]){0x1f,0x04,0x04,0x04,0x04,0x04,0x04}[row];
    case 'U': return (uint8_t[]){0x11,0x11,0x11,0x11,0x11,0x11,0x0e}[row];
    case 'V': return (uint8_t[]){0x11,0x11,0x11,0x11,0x0a,0x0a,0x04}[row];
    case 'W': return (uint8_t[]){0x11,0x11,0x11,0x15,0x15,0x1b,0x11}[row];
    case 'X': return (uint8_t[]){0x11,0x11,0x0a,0x04,0x0a,0x11,0x11}[row];
    case 'Y': return (uint8_t[]){0x11,0x11,0x0a,0x04,0x04,0x04,0x04}[row];
    case 'Z': return (uint8_t[]){0x1f,0x01,0x02,0x04,0x08,0x10,0x1f}[row];
    case '-': return row == 3 ? 0x1f : 0x00;
    case '+': return row == 3 ? 0x0e : (row == 1 || row == 2 || row == 4 || row == 5 ? 0x04 : 0);
    case ':': return row == 2 || row == 4 ? 0x04 : 0;
    default: return 0;
    }
}

static void fb_text(int x, int y, const char *text, uint16_t color, int scale)
{
    for (const char *p = text; *p; ++p) {
        char c = *p;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        for (uint8_t row = 0; row < 7; ++row) {
            uint8_t bits = glyph5x7(c, row);
            for (uint8_t col = 0; col < 5; ++col) {
                if (bits & (1u << (4 - col))) {
                    fb_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
        x += 6 * scale;
    }
}

static const char *cal_stage_name(cal_stage_t stage)
{
    switch (stage) {
    case CAL_WAIT_FORWARD: return "+Y";
    case CAL_WAIT_VERTICAL: return "+Z";
    case CAL_WAIT_RIGHT: return "+X";
    case CAL_DONE: return "DONE";
    default: return "CAL";
    }
}

static const char *touch_proto_name(void)
{
    switch (s_touch_proto) {
    case TOUCH_CST92XX: return "CST92";
    case TOUCH_CST3530: return "CST3530";
    case TOUCH_CST816: return "CST816";
    default: return "NONE";
    }
}

static void draw_wand_face(void)
{
    if (!s_lcd_ready || !s_fb) {
        return;
    }
    const uint16_t bg = RGB565(4, 7, 10);
    const uint16_t grid = RGB565(26, 48, 54);
    const uint16_t dim = RGB565(95, 122, 126);
    const uint16_t text = RGB565(224, 238, 230);
    const uint16_t accent = s_connected ? RGB565(84, 218, 196) : RGB565(255, 176, 80);
    fb_fill(bg);
    if (s_cal_stage != CAL_IDLE && s_cal_stage != CAL_DONE) {
        fb_text(96, 42, "CAL", RGB565(255, 236, 126), 2);
        fb_text(162, 42, cal_stage_name(s_cal_stage), RGB565(255, 236, 126), 2);
    }
    char line[40];
    const int cx = LCD_WIDTH / 2;
    const int cy = 190;
    for (int r = 32; r <= 96; r += 32) {
        for (int a = 0; a < 360; a += 3) {
            float rad = a * 0.01745329252f;
            fb_px(cx + (int)lrintf(cosf(rad) * r), cy + (int)lrintf(sinf(rad) * r), grid);
        }
    }
    fb_line(cx - 104, cy, cx + 104, cy, grid);
    fb_line(cx, cy - 104, cx, cy + 104, grid);
    draw_axis_arrow(cx, cy, 86.0f, 0.0f, 0.0f, "+X", RGB565(246, 112, 96));
    draw_axis_arrow(cx, cy, 0.0f, 86.0f, 0.0f, "+Y", RGB565(84, 218, 196));
    draw_axis_arrow(cx, cy, 0.0f, 0.0f, 86.0f, "+Z", RGB565(255, 236, 126));
    draw_axis_label(cx, cy, -94.0f, 0.0f, 0.0f, "-X", dim);
    draw_axis_label(cx, cy, 0.0f, -94.0f, 0.0f, "-Y", dim);
    draw_axis_label(cx, cy, 0.0f, 0.0f, -94.0f, "-Z", dim);
    draw_cal_target(cx, cy, 88.0f, 0.0f, 0.0f, "+X", CAL_WAIT_RIGHT, RGB565(246, 112, 96), dim);
    draw_cal_target(cx, cy, 0.0f, 88.0f, 0.0f, "+Y", CAL_WAIT_FORWARD, RGB565(84, 218, 196), dim);
    draw_cal_target(cx, cy, 0.0f, 0.0f, 88.0f, "+Z", CAL_WAIT_VERTICAL, RGB565(255, 236, 126), dim);
    snprintf(line, sizeof(line), "POSE:%s", s_pose_name);
    fb_text(cx - 54, cy - 134, line, s_pose_conf > 70 ? RGB565(255, 236, 126) : text, 1);
    snprintf(line, sizeof(line), "CONF:%u", s_pose_conf);
    fb_text(cx + 70, cy - 72, line, dim, 1);
    snprintf(line, sizeof(line), "RSSI:%+d", s_target_rssi);
    fb_text(cx + 86, cy + 18, line, dim, 1);
    snprintf(line, sizeof(line), "TAPS:%" PRIu32, s_tap_count);
    fb_text(cx - 148, cy + 18, line, dim, 1);
    draw_projected_wand(cx, cy, s_pose_body_x, s_pose_body_y, s_pose_body_z, accent, text);
    fb_circle(cx, cy, 5, text);
    if (s_touch_ok) {
        uint16_t touch_color = s_touch_n ? RGB565(255, 236, 126) : RGB565(78, 100, 104);
        for (uint8_t i = 0; i < s_touch_n && i < 2; ++i) {
            int tx = s_touch_x[i];
            int ty = s_touch_y[i];
            if (tx < 0) tx = 0;
            if (tx >= LCD_WIDTH) tx = LCD_WIDTH - 1;
            if (ty < 0) ty = 0;
            if (ty >= LCD_HEIGHT) ty = LCD_HEIGHT - 1;
            fb_ring(tx, ty, 18, touch_color);
            fb_line(tx - 26, ty, tx + 26, ty, touch_color);
            fb_line(tx, ty - 26, tx, ty + 26, touch_color);
        }
    }
    if (xTaskGetTickCount() < s_tap_flash_until) {
        fb_circle(cx, cy, 24, RGB565(255, 236, 126));
        if (xTaskGetTickCount() < s_cross_complete_until) {
            fb_text(126, 282, "CROSS", RGB565(255, 236, 126), 2);
        } else {
            fb_text(112, 282, s_cal_samples_left ? cal_stage_name(s_cal_capture_stage) : "CENTER", RGB565(255, 236, 126), 2);
        }
    }
    snprintf(line, sizeof(line), "CROSS:%u/4", s_cross_step);
    fb_text(cx - 30, cy + 92, line, s_cross_step ? RGB565(255, 236, 126) : dim, 1);
    snprintf(line, sizeof(line), "W X:%+d Y:%+d Z:%+d",
             (int)lrintf(s_wand_x * 100.0f),
             (int)lrintf(s_wand_y * 100.0f),
             (int)lrintf(s_wand_z * 100.0f));
    fb_text(42, 332, line, text, 1);
    snprintf(line, sizeof(line), "R X:%+d Y:%+d Z:%+d", s_raw_x, s_raw_y, s_raw_z);
    fb_text(42, 346, line, dim, 1);
    if (s_touch_ok) {
        snprintf(line, sizeof(line), "TOUCH:%u", s_touch_n);
        fb_text(cx - 34, cy + 128, line, s_touch_n ? RGB565(255, 236, 126) : dim, 1);
        if (s_touch_n) {
            snprintf(line, sizeof(line), "TX:%d TY:%d", s_touch_x[0], s_touch_y[0]);
            fb_text(cx - 58, cy + 142, line, RGB565(255, 236, 126), 1);
        } else {
            snprintf(line, sizeof(line), "%s:%02X IRQ:%u", touch_proto_name(), s_touch_addr, s_touch_irq_level);
            fb_text(cx - 58, cy + 142, line, dim, 1);
        }
    } else {
        fb_text(cx - 34, cy + 128, "TOUCH:--", RGB565(180, 72, 72), 1);
        if (s_i2c_count) {
            char *p = line;
            size_t left = sizeof(line);
            int n = snprintf(p, left, "I2C");
            p += n;
            left -= (size_t)n;
            for (uint8_t i = 0; i < s_i2c_count && left > 4; ++i) {
                n = snprintf(p, left, " %02X", s_i2c_addrs[i]);
                p += n;
                left -= (size_t)n;
            }
            fb_text(cx - 58, cy + 142, line, RGB565(180, 72, 72), 1);
        }
    }
    lcd_flush();
}

static void display_task(void *arg)
{
    (void)arg;
    while (true) {
        draw_wand_face();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void i2c_scan_bus(void)
{
    s_i2c_count = 0;
    for (uint8_t addr = 0x08; addr < 0x78; ++addr) {
        if (i2c_probe(addr)) {
            if (s_i2c_count < sizeof(s_i2c_addrs)) {
                s_i2c_addrs[s_i2c_count++] = addr;
            }
            ESP_LOGI(TAG, "i2c device 0x%02x", addr);
        }
    }
    if (i2c_probe(CST816_ADDR)) {
        s_touch_addr = CST816_ADDR;
        s_touch_proto = TOUCH_CST816;
        uint8_t chip_id = 0xff;
        uint8_t fw = 0xff;
        (void)i2c_write_read_bytes(CST816_ADDR, (const uint8_t[]){CST816_REG_CHIP_ID}, 1, &chip_id, 1);
        (void)i2c_write_read_bytes(CST816_ADDR, (const uint8_t[]){CST816_REG_FW_VERSION}, 1, &fw, 1);
        ESP_LOGI(TAG, "touch cst8xx chip=0x%02x fw=0x%02x", chip_id, fw);
    } else if (i2c_probe(CST3530_ADDR)) {
        s_touch_addr = CST3530_ADDR;
        s_touch_proto = TOUCH_CST3530;
    } else if (i2c_probe(CST92XX_ADDR)) {
        s_touch_addr = CST92XX_ADDR;
        s_touch_proto = TOUCH_CST92XX;
    } else {
        s_touch_addr = 0;
        s_touch_proto = TOUCH_NONE;
    }
    ESP_LOGI(TAG, "touch probe proto=%s addr=0x%02x count=%u",
             touch_proto_name(), s_touch_addr, s_i2c_count);
}

static bool touch_read_cst92xx(void)
{
    uint8_t reg[2] = {(uint8_t)(CST92XX_REG_READ >> 8), (uint8_t)(CST92XX_REG_READ & 0xff)};
    uint8_t buf[15] = {0};
    esp_err_t err = i2c_write_read_bytes(s_touch_addr, reg, sizeof(reg), buf, sizeof(buf));
    if (err != ESP_OK) {
        s_touch_ok = false;
        s_touch_n = 0;
        return false;
    }

    s_touch_ok = true;
    uint8_t ack[3] = {reg[0], reg[1], CST92XX_ACK};
    (void)i2c_write_bytes(s_touch_addr, ack, sizeof(ack));

    uint8_t count = buf[5] & 0x7f;
    if (buf[0] == CST92XX_ACK || buf[6] != CST92XX_ACK || count > 2) {
        ++s_touch_bad_frames;
        count = 0;
    }

    uint8_t parsed = 0;
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t *p = buf + (i * 5) + (i == 0 ? 0 : 2);
        uint8_t id = p[0] >> 4;
        uint8_t event = p[0] & 0x0f;
        if (event != 0x06 || id >= 2) {
            continue;
        }
        int16_t x = (int16_t)(((uint16_t)p[1] << 4) | (p[3] >> 4));
        int16_t y = (int16_t)(((uint16_t)p[2] << 4) | (p[3] & 0x0f));
        if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
            continue;
        }
        s_touch_x[parsed] = x;
        s_touch_y[parsed] = y;
        ++parsed;
    }

    if (parsed && s_touch_n == 0) {
        ++s_touch_events;
    }
    s_touch_n = parsed;
    if (parsed) {
        s_last_touch_tick = xTaskGetTickCount();
    }
    return true;
}

static bool touch_read_cst816(void)
{
    uint8_t reg = CST816_REG_STATUS;
    uint8_t buf[13] = {0};
    esp_err_t err = i2c_write_read_bytes(s_touch_addr, &reg, 1, buf, sizeof(buf));
    if (err != ESP_OK) {
        s_touch_ok = false;
        s_touch_n = 0;
        return false;
    }
    s_touch_ok = true;

    uint8_t count = buf[2] & 0x0f;
    if (buf[2] == 0x00 || buf[2] == 0xff || count == 0 || count > 1) {
        s_touch_n = 0;
        return true;
    }

    int16_t x = (int16_t)((((uint16_t)buf[3] & 0x0f) << 8) | buf[4]);
    int16_t y = (int16_t)((((uint16_t)buf[5] & 0x0f) << 8) | buf[6]);
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
        ++s_touch_bad_frames;
        s_touch_n = 0;
        return true;
    }

    if (s_touch_n == 0) {
        ++s_touch_events;
    }
    s_touch_x[0] = x;
    s_touch_y[0] = y;
    s_touch_n = 1;
    s_last_touch_tick = xTaskGetTickCount();
    return true;
}

static bool touch_read_cst3530(void)
{
    uint8_t cmd[4] = {
        (uint8_t)(CST3530_READ_COMMAND >> 24),
        (uint8_t)(CST3530_READ_COMMAND >> 16),
        (uint8_t)(CST3530_READ_COMMAND >> 8),
        (uint8_t)(CST3530_READ_COMMAND),
    };
    uint8_t buf[32] = {0};
    esp_err_t err = i2c_write_read_bytes(s_touch_addr, cmd, sizeof(cmd), buf, sizeof(buf));
    if (err != ESP_OK) {
        s_touch_ok = false;
        s_touch_n = 0;
        return false;
    }
    s_touch_ok = true;

    uint8_t clear[4] = {
        (uint8_t)(CST3530_CLEAR_COMMAND >> 24),
        (uint8_t)(CST3530_CLEAR_COMMAND >> 16),
        (uint8_t)(CST3530_CLEAR_COMMAND >> 8),
        (uint8_t)(CST3530_CLEAR_COMMAND),
    };
    (void)i2c_write_bytes(s_touch_addr, clear, sizeof(clear));

    uint8_t parsed = 0;
    if (buf[2] == 0xff) {
        uint8_t count = buf[3] & 0x0f;
        uint8_t key_count = (buf[3] & 0xf0) >> 4;
        if (count <= 2) {
            for (uint8_t i = 0; i < count; ++i) {
                uint16_t idx = (uint16_t)(key_count + i) * 5u;
                if (idx + 8 >= sizeof(buf)) {
                    break;
                }
                uint8_t event = buf[idx + 8] >> 4;
                if (event == 0x00) {
                    continue;
                }
                int16_t x = (int16_t)(buf[idx + 4] + (((uint16_t)buf[idx + 7] & 0x0f) << 8));
                int16_t y = (int16_t)(buf[idx + 5] + (((uint16_t)buf[idx + 7] & 0xf0) << 4));
                if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) {
                    continue;
                }
                s_touch_x[parsed] = x;
                s_touch_y[parsed] = y;
                ++parsed;
            }
        } else {
            ++s_touch_bad_frames;
        }
    }

    if (parsed && s_touch_n == 0) {
        ++s_touch_events;
    }
    s_touch_n = parsed;
    if (parsed) {
        s_last_touch_tick = xTaskGetTickCount();
    }
    return true;
}

static bool touch_read(void)
{
    s_touch_irq_level = (uint8_t)gpio_get_level(TP_INT);
    switch (s_touch_proto) {
    case TOUCH_CST92XX:
        return touch_read_cst92xx();
    case TOUCH_CST3530:
        return touch_read_cst3530();
    case TOUCH_CST816:
        return touch_read_cst816();
    default:
        s_touch_ok = false;
        s_touch_n = 0;
        return false;
    }
}

static bool touch_near_cal_target(int tx, int ty, float x, float y, float z)
{
    int sx, sy;
    project_axis_point(x, y, z, LCD_WIDTH / 2, LCD_HEIGHT / 2, 1.0f, &sx, &sy);
    const int dx = tx - sx;
    const int dy = ty - sy;
    return dx * dx + dy * dy <= 42 * 42;
}

static void touch_calibration_press(int tx, int ty, TickType_t now)
{
    if (now - s_last_cal_touch_tick < pdMS_TO_TICKS(500)) {
        return;
    }
    if (touch_near_cal_target(tx, ty, 88.0f, 0.0f, 0.0f)) {
        s_last_cal_touch_tick = now;
        start_touch_cal_capture(CAL_WAIT_RIGHT, now);
    } else if (touch_near_cal_target(tx, ty, 0.0f, 88.0f, 0.0f)) {
        s_last_cal_touch_tick = now;
        start_touch_cal_capture(CAL_WAIT_FORWARD, now);
    } else if (touch_near_cal_target(tx, ty, 0.0f, 0.0f, 88.0f)) {
        s_last_cal_touch_tick = now;
        start_touch_cal_capture(CAL_WAIT_VERTICAL, now);
    }
}

static void touch_task(void *arg)
{
    (void)arg;
    i2c_scan_bus();
    while (true) {
        (void)touch_read();
        if (s_touch_n) {
            touch_calibration_press(s_touch_x[0], s_touch_y[0], xTaskGetTickCount());
        }
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static void recenter_button_task(void *arg)
{
    (void)arg;
    bool was_pressed = false;
    TickType_t last_press = 0;
    while (true) {
        bool pressed = gpio_get_level(BUTTON_BOOT) == 0;
        TickType_t now = xTaskGetTickCount();
        if (pressed && !was_pressed && now - last_press > pdMS_TO_TICKS(350)) {
            calibration_button_press(now);
            last_press = now;
        }
        was_pressed = pressed;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void display_begin(void)
{
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = IIC_SDA,
        .scl_io_num = IIC_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
        .clk_flags = 0,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    gpio_config_t out = {
#if defined(WAND_BOARD_WAVESHARE_S3_185B)
        .pin_bit_mask = (1ULL << LCD_CS) | (1ULL << LCD_BL) | (1ULL << LCD_RST) | (1ULL << TP_RST),
#else
        .pin_bit_mask = (1ULL << LCD_CS) | (1ULL << LCD_BL),
#endif
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

#if defined(WAND_BOARD_WAVESHARE_S3_185B)
    direct_reset_pin(TP_RST, false);
    vTaskDelay(pdMS_TO_TICKS(30));
    direct_reset_pin(TP_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));
    direct_reset_pin(LCD_RST, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    direct_reset_pin(LCD_RST, true);
    vTaskDelay(pdMS_TO_TICKS(120));
#else
    tca9554_write(0x03, 0x00);
    tca9554_set_pin(TCA9554_TOUCH_RST_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(30));
    tca9554_set_pin(TCA9554_TOUCH_RST_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(120));
    tca9554_set_pin(TCA9554_LCD_RST_PIN, false);
    vTaskDelay(pdMS_TO_TICKS(10));
    tca9554_set_pin(TCA9554_LCD_RST_PIN, true);
    vTaskDelay(pdMS_TO_TICKS(120));
#endif

    lcd_cs(false);
    gpio_set_level(LCD_BL, 1);
    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BUTTON_BOOT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));
    gpio_config_t touch_int = {
        .pin_bit_mask = 1ULL << TP_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&touch_int));

    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_SDIO0,
        .miso_io_num = LCD_SDIO1,
        .sclk_io_num = LCD_SCLK,
        .quadwp_io_num = LCD_SDIO2,
        .quadhd_io_num = LCD_SDIO3,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = 4096,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
        .intr_flags = 0,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t devcfg = {
        .command_bits = 8,
        .address_bits = 24,
        .dummy_bits = 0,
        .mode = 0,
        .clock_speed_hz = 40000000,
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &devcfg, &s_lcd_spi));
    lcd_apply_init_table(k_st77916_base_init, sizeof(k_st77916_base_init) / sizeof(k_st77916_base_init[0]));
#if !defined(WAND_BOARD_WAVESHARE_S3_185B)
    lcd_apply_init_table(k_waveshare185c_v2_init,
                         sizeof(k_waveshare185c_v2_init) / sizeof(k_waveshare185c_v2_init[0]));
#endif
    const uint8_t madctl = 0x00;
    lcd_cmd_params(0x36, &madctl, 1);
    s_fb = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_tx_line = heap_caps_aligned_alloc(16, 2048, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_fb || !s_tx_line) {
        ESP_LOGE(TAG, "display allocation failed fb=%p line=%p", s_fb, s_tx_line);
        return;
    }
    s_lcd_ready = true;
    ESP_LOGI(TAG, "display ready");
    xTaskCreate(display_task, "wand_display", 4096, NULL, 4, NULL);
    xTaskCreate(touch_task, "wand_touch", 3072, NULL, 5, NULL);
    xTaskCreate(recenter_button_task, "wand_recenter", 2048, NULL, 5, NULL);
}

static void log_heap(const char *label)
{
    ESP_LOGI(TAG, "%s heap internal=%u largest=%u psram=%u",
             label,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        printf("%s%02x", i ? " " : "", data[i]);
    }
}

static void dump_hex(const char *label, const uint8_t *data, uint8_t len)
{
    printf("%s ", label);
    print_hex(data, len);
    printf("\n");
}

static void request_recenter(TickType_t now)
{
    s_center_accum_x = 0.0f;
    s_center_accum_y = 0.0f;
    s_center_accum_z = 0.0f;
    s_center_samples_left = 8;
    s_last_tap_tick = now;
    s_tap_flash_until = now + pdMS_TO_TICKS(900);
    ++s_tap_count;
    s_ring_status = "tap center";
    ESP_LOGI(TAG, "tap detected; collecting center samples");
}

static void update_center(float gx, float gy, float gz)
{
    if (!s_center_valid) {
        s_center_x = gx;
        s_center_y = gy;
        s_center_z = gz;
        s_center_valid = true;
        return;
    }
    if (!s_center_samples_left) {
        return;
    }
    s_center_accum_x += gx;
    s_center_accum_y += gy;
    s_center_accum_z += gz;
    --s_center_samples_left;
    if (!s_center_samples_left) {
        s_center_x = s_center_accum_x / 8.0f;
        s_center_y = s_center_accum_y / 8.0f;
        s_center_z = s_center_accum_z / 8.0f;
        s_ring_status = "centered";
        ESP_LOGI(TAG, "center set g x=%.3f y=%.3f z=%.3f", s_center_x, s_center_y, s_center_z);
    }
}

static float vec_dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float vec_len(const float v[3])
{
    return sqrtf(vec_dot(v, v));
}

static float vec_cos_to(const float a[3], const float b[3])
{
    const float la = vec_len(a);
    const float lb = vec_len(b);
    if (la < 0.001f || lb < 0.001f) {
        return -1.0f;
    }
    return vec_dot(a, b) / (la * lb);
}

static void update_cross_sequence(const char *pose, uint8_t conf, bool is_static, TickType_t now)
{
    static const char *seq[] = {"NOSEUP", "NOSEDN", "LEFT", "RIGHT"};
    const TickType_t hold = pdMS_TO_TICKS(260);
    const TickType_t timeout = pdMS_TO_TICKS(3000);

    if (!is_static || conf < 65 || strcmp(pose, "MOVE") == 0 || strcmp(pose, "FLAT") == 0) {
        return;
    }
    if (s_cross_last_pose != pose) {
        s_cross_last_pose = pose;
        s_cross_pose_since = now;
        return;
    }
    if (now - s_cross_pose_since < hold) {
        return;
    }
    if (s_cross_step > 0 && now - s_cross_pose_since > timeout) {
        s_cross_step = 0;
    }
    if (strcmp(pose, seq[s_cross_step]) == 0) {
        ++s_cross_step;
        s_cross_pose_since = now + hold;
        if (s_cross_step >= sizeof(seq) / sizeof(seq[0])) {
            s_cross_step = 0;
            ++s_tap_count;
            s_cross_complete_until = now + pdMS_TO_TICKS(1400);
            s_tap_flash_until = s_cross_complete_until;
            s_ring_status = "cross";
        }
    } else if (strcmp(pose, seq[0]) == 0) {
        s_cross_step = 1;
        s_cross_pose_since = now + hold;
    } else {
        s_cross_step = 0;
    }
}

static void update_gravity_pose(float gx, float gy, float gz)
{
    const float mag = sqrtf(gx * gx + gy * gy + gz * gz);
    const float motion = fabsf(mag - 1.0f);
    s_pose_static = motion < 0.18f;

    float bx = 0.0f;
    float by = 0.0f;
    float bz = 1.0f;
    if (s_cal_valid) {
        bx = gx;
        by = gy;
        bz = gz;
    } else if (s_center_valid) {
        bx = gy - s_center_y;
        const float center[3] = {s_center_x, s_center_y, s_center_z};
        const float c_len = vec_len(center);
        if (mag > 0.001f && c_len > 0.001f) {
            by = (gx * s_center_x + gy * s_center_y + gz * s_center_z) / (mag * c_len);
        }
        bz = -(gx - s_center_x);
    } else {
        bx = gy;
        by = gz;
        bz = -gx;
    }

    if (bx > 1.0f) bx = 1.0f;
    if (bx < -1.0f) bx = -1.0f;
    if (by > 1.0f) by = 1.0f;
    if (by < -1.0f) by = -1.0f;
    if (bz > 1.0f) bz = 1.0f;
    if (bz < -1.0f) bz = -1.0f;
    s_pose_body_x = bx;
    s_pose_body_y = by;
    s_pose_body_z = bz;

    const char *name = "MOVE";
    float conf = 1.0f - motion / 0.18f;
    if (conf < 0.0f) conf = 0.0f;
    if (s_pose_static) {
        const float ax = fabsf(bx);
        const float ay = fabsf(by);
        const float az = fabsf(bz);
        if (ax >= ay && ax >= az) {
            name = bx >= 0.0f ? "X+" : "X-";
        } else if (ay >= ax && ay >= az) {
            name = by >= 0.0f ? "Y+" : "Y-";
        } else {
            name = bz >= 0.0f ? "Z+" : "Z-";
        }
    }
    strncpy(s_pose_name, name, sizeof(s_pose_name) - 1);
    s_pose_name[sizeof(s_pose_name) - 1] = '\0';
    s_pose_conf = (uint8_t)lrintf(conf * 100.0f);
    update_cross_sequence(s_pose_name, s_pose_conf, s_pose_static, xTaskGetTickCount());
}

static void update_pose(float gx, float gy, float gz)
{
    update_gravity_pose(gx, gy, gz);
    return;

    const float g[3] = {gx, gy, gz};
    float best = -2.0f;
    const char *name = "WAIT";

    if (s_cal_valid) {
        struct {
            const char *name;
            const float *v;
        } poses[] = {
            {"UP", s_cal_up},
            {"DOWN", s_cal_down},
            {"RIGHT", s_cal_right},
            {"LEFT", s_cal_left},
        };
        for (size_t i = 0; i < sizeof(poses) / sizeof(poses[0]); ++i) {
            float score = vec_cos_to(g, poses[i].v);
            if (score > best) {
                best = score;
                name = poses[i].name;
            }
        }
    } else {
        float ax = fabsf(gx);
        float ay = fabsf(gy);
        float az = fabsf(gz);
        if (ax >= ay && ax >= az) {
            best = ax;
            name = gx >= 0.0f ? "X+" : "X-";
        } else if (ay >= ax && ay >= az) {
            best = ay;
            name = gy >= 0.0f ? "Y+" : "Y-";
        } else {
            best = az;
            name = gz >= 0.0f ? "Z+" : "Z-";
        }
    }

    if (!s_cal_valid && s_center_valid) {
        const float rel_x = gx - s_center_x;
        const float rel_y = gy - s_center_y;
        const float ax = fabsf(rel_x);
        const float ay = fabsf(rel_y);
        const float tilt = sqrtf(rel_x * rel_x + rel_y * rel_y);
        if (tilt < 0.16f) {
            name = "FLAT";
            best = 1.0f - (tilt / 0.16f);
        } else if (ax >= ay) {
            name = rel_x < 0.0f ? "UP" : "DOWN";
            best = ax / 0.85f;
        } else {
            name = rel_y < 0.0f ? "LEFT" : "RIGHT";
            best = ay / 0.85f;
        }
    }

    if (best < -1.0f) {
        best = -1.0f;
    }
    if (best > 1.0f) {
        best = 1.0f;
    }
    strncpy(s_pose_name, name, sizeof(s_pose_name) - 1);
    s_pose_name[sizeof(s_pose_name) - 1] = '\0';
    if (s_cal_valid) {
        s_pose_conf = (uint8_t)lrintf(((best + 1.0f) * 0.5f) * 100.0f);
    } else {
        s_pose_conf = (uint8_t)lrintf(best * 100.0f);
    }
}

static void start_cal_capture(cal_stage_t stage, TickType_t now)
{
    s_cal_capture_stage = stage;
    s_cal_accum[0] = 0.0f;
    s_cal_accum[1] = 0.0f;
    s_cal_accum[2] = 0.0f;
    s_cal_samples_left = 5;
    s_tap_flash_until = now + pdMS_TO_TICKS(1200);
    s_ring_status = cal_stage_name(stage);
    ESP_LOGI(TAG, "calibration capture %s", cal_stage_name(stage));
}

static void start_touch_cal_capture(cal_stage_t stage, TickType_t now)
{
    if (s_cal_samples_left) {
        return;
    }
    s_cal_sequence_active = false;
    s_cal_stage = stage;
    start_cal_capture(stage, now);
}

static void calibration_button_press(TickType_t now)
{
    if (s_cal_samples_left) {
        return;
    }
    if (s_cal_stage == CAL_IDLE || s_cal_stage == CAL_DONE) {
        s_cal_valid = false;
        s_cal_forward_valid = false;
        s_cal_vertical_valid = false;
        s_cal_body_right_valid = false;
        s_cal_sequence_active = true;
        s_cal_stage = CAL_WAIT_FORWARD;
        start_cal_capture(s_cal_stage, now);
        ESP_LOGI(TAG, "calibration start; sampling level +Y");
        return;
    }
    start_cal_capture(s_cal_stage, now);
}

static void finish_cal_capture(float gx, float gy, float gz)
{
    float *dst = NULL;
    switch (s_cal_capture_stage) {
    case CAL_WAIT_FORWARD:
        dst = s_cal_forward;
        s_cal_forward_valid = true;
        if (s_cal_sequence_active) {
            s_cal_stage = CAL_WAIT_VERTICAL;
            s_ring_status = "tap +Z";
        }
        break;
    case CAL_WAIT_VERTICAL:
        dst = s_cal_vertical;
        s_cal_vertical_valid = true;
        if (s_cal_sequence_active) {
            s_cal_stage = CAL_WAIT_RIGHT;
            s_ring_status = "tap +X";
        }
        break;
    case CAL_WAIT_RIGHT:
        dst = s_cal_body_right;
        s_cal_body_right_valid = true;
        if (s_cal_sequence_active) {
            s_cal_stage = CAL_DONE;
            s_cal_sequence_active = false;
        }
        break;
    default:
        break;
    }
    if (dst) {
        dst[0] = gx;
        dst[1] = gy;
        dst[2] = gz;
        ESP_LOGI(TAG, "cal %s set g x=%.3f y=%.3f z=%.3f",
                 cal_stage_name(s_cal_capture_stage), gx, gy, gz);
    }
    s_cal_valid = s_cal_forward_valid && s_cal_vertical_valid && s_cal_body_right_valid;
    if (s_cal_valid) {
        s_cal_stage = CAL_DONE;
        s_cal_sequence_active = false;
        s_ring_status = "cal done";
    } else if (!s_cal_sequence_active) {
        s_cal_stage = CAL_IDLE;
        s_ring_status = "cal partial";
    }
    s_cal_capture_stage = CAL_IDLE;
    s_tap_flash_until = xTaskGetTickCount() + pdMS_TO_TICKS(900);
}

static void update_calibration(float gx, float gy, float gz)
{
    if (!s_cal_samples_left) {
        return;
    }
    s_cal_accum[0] += gx;
    s_cal_accum[1] += gy;
    s_cal_accum[2] += gz;
    --s_cal_samples_left;
    if (!s_cal_samples_left) {
        finish_cal_capture(s_cal_accum[0] / 5.0f, s_cal_accum[1] / 5.0f, s_cal_accum[2] / 5.0f);
    }
}

static bool uuid128_eq(const esp_bt_uuid_t *uuid, const uint8_t want[16])
{
    return uuid->len == ESP_UUID_LEN_128 && memcmp(uuid->uuid.uuid128, want, 16) == 0;
}

static bool bytes_have(const uint8_t *data, uint8_t len, uint8_t a, uint8_t b)
{
    for (uint8_t i = 0; i + 1 < len; ++i) {
        if (data[i] == a && data[i + 1] == b) {
            return true;
        }
    }
    return false;
}

static bool name_looks_like_ring(const uint8_t *name, uint8_t len)
{
    if (len >= 5 && memcmp(name, "COLMI", 5) == 0) {
        return true;
    }
    for (uint8_t i = 0; i + 2 < len; ++i) {
        if (name[i] == 'R' && name[i + 1] == '0' && name[i + 2] == '2') {
            return true;
        }
    }
    return false;
}

static uint8_t checksum(const uint8_t *data, size_t len_without_crc)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < len_without_crc; ++i) {
        sum += data[i];
    }
    return (uint8_t)(sum & 0xff);
}

static void make_command(uint8_t cmd, uint8_t a, uint8_t b, uint8_t out[16])
{
    memset(out, 0, 16);
    out[0] = cmd;
    out[1] = a;
    out[2] = b;
    out[15] = checksum(out, 15);
}

static int16_t parse_i12(uint8_t hi, uint8_t lo_nibble)
{
    int16_t v = (int16_t)(((uint16_t)hi << 4) | (lo_nibble & 0x0f));
    if (v & 0x0800) {
        v -= 0x1000;
    }
    return v;
}

static int16_t be16_at(const uint8_t *data, uint16_t offset)
{
    return (int16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
}

static int16_t le16_at(const uint8_t *data, uint16_t offset)
{
    return (int16_t)(((uint16_t)data[offset + 1] << 8) | data[offset]);
}

static void map_raw_to_wand(float rx, float ry, float rz, float *wx, float *wy, float *wz)
{
    if (s_cal_valid) {
        const float raw[3] = {rx, ry, rz};
        const float mag = vec_len(raw);
        const float lx = vec_len(s_cal_body_right);
        const float ly = vec_len(s_cal_forward);
        const float lz = vec_len(s_cal_vertical);
        if (mag > 0.001f && lx > 0.001f && ly > 0.001f && lz > 0.001f) {
            *wx = vec_dot(raw, s_cal_body_right) / (mag * lx);
            *wy = vec_dot(raw, s_cal_forward) / (mag * ly);
            *wz = vec_dot(raw, s_cal_vertical) / (mag * lz);
            return;
        }
    }

    /*
     * Sensor package axes are not wand body axes. On the wand mount, the
     * table-level forward pose reports mostly raw Z, so treat that as +Y.
     */
    *wx = ry;
    *wy = rz;
    *wz = -rx;
}

static void parse_notify(const uint8_t *data, uint16_t len)
{
    printf("RX ");
    print_hex(data, len);
    printf("\n");
    if (len >= 10 && data[1] == 0x03) {
        int16_t raw_y = parse_i12(data[2], data[3]);
        int16_t raw_z = parse_i12(data[4], data[5]);
        int16_t raw_x = parse_i12(data[6], data[7]);
        s_raw_x = raw_x;
        s_raw_y = raw_y;
        s_raw_z = raw_z;
        const float ring_x = raw_x / 512.0f;
        const float ring_y = raw_y / 512.0f;
        const float ring_z = raw_z / 512.0f;
        s_raw_g_x = ring_x;
        s_raw_g_y = ring_y;
        s_raw_g_z = ring_z;
        map_raw_to_wand(ring_x, ring_y, ring_z, &s_wand_x, &s_wand_y, &s_wand_z);
        s_g_x = s_wand_x;
        s_g_y = s_wand_y;
        s_g_z = s_wand_z;
        s_last_sample_tick = xTaskGetTickCount();
        ++s_sample_count;
        TickType_t now = s_last_sample_tick;
        bool tapped = false;
        update_pose(s_g_x, s_g_y, s_g_z);
        update_calibration(s_raw_g_x, s_raw_g_y, s_raw_g_z);
        if (s_prev_raw_valid && s_sample_count > 8 && !s_cal_samples_left &&
            (s_cal_stage == CAL_IDLE || s_cal_stage == CAL_DONE)) {
            int jerk = abs(raw_x - s_prev_raw_x) + abs(raw_y - s_prev_raw_y) + abs(raw_z - s_prev_raw_z);
            if (jerk > 220 && now - s_last_tap_tick > pdMS_TO_TICKS(650) && !s_center_samples_left) {
                request_recenter(now);
                tapped = true;
            }
        }
        s_prev_raw_x = raw_x;
        s_prev_raw_y = raw_y;
        s_prev_raw_z = raw_z;
        s_prev_raw_valid = true;
        if (!tapped && !s_cal_samples_left) {
            update_center(s_g_x, s_g_y, s_g_z);
        }
        if (now >= s_tap_flash_until && !s_center_samples_left && !s_cal_samples_left &&
            (s_cal_stage == CAL_IDLE || s_cal_stage == CAL_DONE)) {
            s_ring_status = "ring streaming";
        }
        ESP_LOGI(TAG, "IMU cmd=0x%02x rssi=%d raw x=%d y=%d z=%d g x=%.3f y=%.3f z=%.3f",
                 data[0], s_target_rssi, raw_x, raw_y, raw_z,
                 raw_x / 512.0f, raw_y / 512.0f, raw_z / 512.0f);
    } else if (len >= 3 && (data[0] & 0x7f) == 0x03) {
        ESP_LOGI(TAG, "battery=%u charging=%u", data[1], data[2]);
    } else if (len >= 2 && data[0] == 0xa1 && data[1] == 0xff) {
        s_ring_status = "raw rejected";
        try_next_raw_mode();
    } else if (len >= 3 && data[0] == 0x73) {
        if (data[1] == 0x12 && len >= 12) {
            ESP_LOGI(TAG,
                     "motion notify rssi=%d type=0x%02x be[4,6,10]=%d,%d,%d le[4,6,10]=%d,%d,%d",
                     s_target_rssi, data[1],
                     be16_at(data, 4), be16_at(data, 6), be16_at(data, 10),
                     le16_at(data, 4), le16_at(data, 6), le16_at(data, 10));
        } else {
            ESP_LOGI(TAG, "device notify rssi=%d type=0x%02x value=0x%02x", s_target_rssi, data[1], data[2]);
        }
    }
}

static void reset_service_handles(void)
{
    s_uart.start_handle = s_uart.end_handle = 0;
    s_uart.write_handle = s_uart.notify_handle = s_uart.cccd_handle = 0;
    s_uart.found = s_uart.subscribed = false;
    s_main.start_handle = s_main.end_handle = 0;
    s_main.write_handle = s_main.notify_handle = s_main.cccd_handle = 0;
    s_main.found = s_main.subscribed = false;
    s_pending_subscribes = 0;
    s_sent_enable = false;
    s_raw_attempt = 0;
    s_tx_busy = false;
    s_tx_head = s_tx_tail = s_tx_count = 0;
}

static void send_to_ring(const uint8_t *data, uint16_t len, const char *label)
;
static void send_to_handle_queued(uint16_t handle, const uint8_t *data, uint16_t len, const char *label);

static void pump_tx_queue(void)
{
    if (s_tx_busy || s_tx_count == 0 || !s_connected) {
        return;
    }

    tx_item_t *item = &s_tx_queue[s_tx_head];
    s_tx_busy = true;
    printf("TX %s h=%u ", item->label, item->handle);
    print_hex(item->data, item->len);
    printf("\n");
    esp_err_t err = esp_ble_gattc_write_char(s_gattc_if, s_conn_id, item->handle, item->len, item->data,
                                             ESP_GATT_WRITE_TYPE_RSP, ESP_GATT_AUTH_REQ_NONE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s handle=%u failed: %s", item->label, item->handle, esp_err_to_name(err));
        s_tx_busy = false;
        s_tx_head = (uint8_t)((s_tx_head + 1) % TX_QUEUE_LEN);
        --s_tx_count;
        pump_tx_queue();
    }
}

static void send_to_handle_queued(uint16_t handle, const uint8_t *data, uint16_t len, const char *label)
{
    if (!s_connected || len > sizeof(s_tx_queue[0].data)) {
        return;
    }

    if (!handle || s_tx_count >= TX_QUEUE_LEN) {
        ESP_LOGW(TAG, "drop TX %s handle=%u queued=%u", label, handle, s_tx_count);
        return;
    }

    tx_item_t *item = &s_tx_queue[s_tx_tail];
    item->handle = handle;
    item->len = len;
    memcpy(item->data, data, len);
    snprintf(item->label, sizeof(item->label), "%s", label);
    s_tx_tail = (uint8_t)((s_tx_tail + 1) % TX_QUEUE_LEN);
    ++s_tx_count;
    pump_tx_queue();
}

static void send_to_ring(const uint8_t *data, uint16_t len, const char *label)
{
    uint16_t handle = s_uart.write_handle ? s_uart.write_handle : s_main.write_handle;
    send_to_handle_queued(handle, data, len, label);
}

static void send_framed_payload(uint8_t cmd, const uint8_t *payload, uint8_t payload_len, const char *label)
{
    uint8_t packet[16] = {0};
    packet[0] = cmd;
    if (payload_len > 14) {
        payload_len = 14;
    }
    memcpy(&packet[1], payload, payload_len);
    packet[15] = checksum(packet, 15);
    send_to_ring(packet, sizeof(packet), label);
}

static void try_next_raw_mode(void)
{
    if (s_raw_attempt == 1) {
        static const uint8_t raw_alt[] = {0x04, 0x04};
        s_raw_attempt = 2;
        ESP_LOGI(TAG, "raw IMU rejected; trying alternate raw mode");
        send_framed_payload(0xa1, raw_alt, sizeof(raw_alt), "raw-imu-alt");
    } else if (s_raw_attempt == 2) {
        static const uint8_t camera_on[] = {0x04};
        s_raw_attempt = 3;
        ESP_LOGI(TAG, "alternate raw mode rejected; trying camera gesture stream");
        send_framed_payload(0x02, camera_on, sizeof(camera_on), "camera-on");
    } else {
        ESP_LOGW(TAG, "raw sensor command rejected by this ring firmware");
    }
}

static void rssi_task(void *arg)
{
    (void)arg;
    while (s_connected) {
        esp_ble_gap_read_rssi(s_remote_bda);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    s_rssi_task = NULL;
    vTaskDelete(NULL);
}

static void ring_ready(void)
{
    if (s_sent_enable) {
        return;
    }
    s_sent_enable = true;
    s_ring_status = "raw enable";
    ESP_LOGI(TAG, "ring connected handles uart write=%u notify=%u cccd=%u main write=%u notify=%u cccd=%u",
             s_uart.write_handle, s_uart.notify_handle, s_uart.cccd_handle,
             s_main.write_handle, s_main.notify_handle, s_main.cccd_handle);
    log_heap("after ring connect");
    uint8_t raw_alt[16];
    make_command(0xa1, 0x04, 0x04, raw_alt);
    s_raw_attempt = 1;
    send_to_handle_queued(s_uart.write_handle, raw_alt, sizeof(raw_alt), "uart-raw-a10404-first");
    if (!s_rssi_task) {
        xTaskCreate(rssi_task, "ring_rssi", 2048, NULL, 5, &s_rssi_task);
    }
}

static void maybe_ready_after_subscribe(void)
{
    if (s_pending_subscribes == 0) {
        ring_ready();
    }
}

static void subscribe_service(ring_service_t *svc)
{
    if (!svc->notify_handle || !svc->cccd_handle) {
        return;
    }
    esp_err_t err = esp_ble_gattc_register_for_notify(s_gattc_if, s_remote_bda, svc->notify_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "%s register_for_notify failed: %s", svc->name, esp_err_to_name(err));
        return;
    }
    ++s_pending_subscribes;
}

static void scan_db_for_service(ring_service_t *svc)
{
    if (!svc->found) {
        return;
    }

    uint16_t count = 0;
    esp_gatt_status_t status = esp_ble_gattc_get_attr_count(s_gattc_if, s_conn_id, ESP_GATT_DB_ALL,
                                                            svc->start_handle, svc->end_handle,
                                                            0, &count);
    if (status != ESP_GATT_OK || count == 0) {
        ESP_LOGW(TAG, "%s get_attr_count failed status=%d count=%u", svc->name, status, count);
        return;
    }

    esp_gattc_db_elem_t *db = calloc(count, sizeof(*db));
    if (!db) {
        ESP_LOGE(TAG, "%s db allocation failed count=%u", svc->name, count);
        return;
    }

    status = esp_ble_gattc_get_db(s_gattc_if, s_conn_id, svc->start_handle, svc->end_handle, db, &count);
    if (status != ESP_GATT_OK) {
        ESP_LOGW(TAG, "%s get_db failed status=%d", svc->name, status);
        free(db);
        return;
    }

    for (uint16_t i = 0; i < count; ++i) {
        esp_gattc_db_elem_t *e = &db[i];
        if (e->type == ESP_GATT_DB_CHARACTERISTIC) {
            ESP_LOGI(TAG, "%s chr handle=%u props=0x%02x uuid_len=%u",
                     svc->name, e->attribute_handle, e->properties, e->uuid.len);
            if (uuid128_eq(&e->uuid, svc->write_uuid)) {
                svc->write_handle = e->attribute_handle;
            } else if (uuid128_eq(&e->uuid, svc->notify_uuid)) {
                svc->notify_handle = e->attribute_handle;
            }
        } else if (e->type == ESP_GATT_DB_DESCRIPTOR &&
                   e->uuid.len == ESP_UUID_LEN_16 &&
                   e->uuid.uuid.uuid16 == ESP_GATT_UUID_CHAR_CLIENT_CONFIG) {
            svc->cccd_handle = e->attribute_handle;
            ESP_LOGI(TAG, "%s cccd handle=%u", svc->name, svc->cccd_handle);
        }
    }

    free(db);
    subscribe_service(svc);
}

static void handle_search_complete(esp_ble_gattc_cb_param_t *param)
{
    ESP_LOGI(TAG, "service search complete status=%u", param->search_cmpl.status);
    scan_db_for_service(&s_uart);
    scan_db_for_service(&s_main);
    if (s_pending_subscribes == 0) {
        ring_ready();
    }
}

static void start_scan(void)
{
    if (s_gattc_if == ESP_GATT_IF_NONE || s_connected || s_connecting) {
        return;
    }
    esp_err_t err = esp_ble_gap_set_scan_params(&s_scan_params);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set scan params failed: %s", esp_err_to_name(err));
    }
}

static void log_adv(const esp_ble_gap_cb_param_t *param, bool candidate)
{
    const uint8_t *adv = param->scan_rst.ble_adv;
    const uint8_t adv_len = param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len;
    uint8_t name_len = 0;
    uint8_t mfg_len = 0;
    uint8_t uuid16_len = 0;
    uint8_t *name = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
    if (!name) {
        name = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
    }
    esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mfg_len);
    uint8_t *uuid16 = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid16_len);
    if (!uuid16) {
        uuid16 = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_16SRV_PART, &uuid16_len);
    }

    if (!candidate && (s_seen % 512) != 0) {
        return;
    }

    ESP_LOGI(TAG, "%sadv " ESP_BD_ADDR_STR " rssi=%d evt=%u adv_len=%u name=%.*s uuid16_len=%u mfg_len=%u seen=%" PRIu32,
             candidate ? "RING " : "",
             ESP_BD_ADDR_HEX(param->scan_rst.bda),
             param->scan_rst.rssi,
             param->scan_rst.ble_evt_type,
             adv_len,
             name_len,
             name ? (const char *)name : "",
             uuid16_len,
             mfg_len,
             s_seen);
    if (candidate || uuid16_len || mfg_len) {
        dump_hex("ADVRAW", adv, adv_len);
    }
}

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        ESP_LOGI(TAG, "scanning for COLMI/R02");
        s_ring_status = "scanning";
        ESP_ERROR_CHECK(esp_ble_gap_start_scanning(0));
        log_heap("after scan start");
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) {
            break;
        }
        ++s_seen;
        uint8_t name_len = 0;
        uint8_t mfg_len = 0;
        uint8_t uuid16_len = 0;
        const uint8_t *adv = param->scan_rst.ble_adv;
        uint8_t adv_len = param->scan_rst.adv_data_len + param->scan_rst.scan_rsp_len;
        uint8_t *name = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
        if (!name) {
            name = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
        }
        uint8_t *mfg = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE, &mfg_len);
        uint8_t *uuid16 = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_16SRV_CMPL, &uuid16_len);
        if (!uuid16) {
            uuid16 = esp_ble_resolve_adv_data((uint8_t *)adv, ESP_BLE_AD_TYPE_16SRV_PART, &uuid16_len);
        }

        bool candidate = (name && name_looks_like_ring(name, name_len)) ||
                         (uuid16 && bytes_have(uuid16, uuid16_len, 0xe7, 0xfe)) ||
                         (mfg && bytes_have(mfg, mfg_len, 0xfe, 0xe7)) ||
                         bytes_have(adv, adv_len, 0xe7, 0xfe) ||
                         bytes_have(adv, adv_len, 0xfe, 0xe7);
        log_adv(param, candidate);
        if (!candidate || s_connecting || s_connected || s_gattc_if == ESP_GATT_IF_NONE) {
            break;
        }

        s_connecting = true;
        s_target_rssi = param->scan_rst.rssi;
        memcpy(s_remote_bda, param->scan_rst.bda, sizeof(s_remote_bda));
        ESP_LOGI(TAG, "ring candidate " ESP_BD_ADDR_STR " addr_type=%u rssi=%d",
                 ESP_BD_ADDR_HEX(s_remote_bda), param->scan_rst.ble_addr_type, s_target_rssi);
        s_ring_status = "connecting";
        esp_ble_gap_stop_scanning();
        esp_err_t err = esp_ble_gattc_open(s_gattc_if, s_remote_bda, param->scan_rst.ble_addr_type, true);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gattc open failed: %s", esp_err_to_name(err));
            s_connecting = false;
            start_scan();
        }
        break;
    }
    case ESP_GAP_BLE_READ_RSSI_COMPLETE_EVT:
        if (param->read_rssi_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            s_target_rssi = param->read_rssi_cmpl.rssi;
            ESP_LOGI(TAG, "RSSI " ESP_BD_ADDR_STR " %d dBm",
                     ESP_BD_ADDR_HEX(param->read_rssi_cmpl.remote_addr), s_target_rssi);
        } else {
            ESP_LOGW(TAG, "read RSSI failed status=%u", param->read_rssi_cmpl.status);
        }
        break;
    default:
        break;
    }
}

static void gattc_cb(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                     esp_ble_gattc_cb_param_t *param)
{
    switch (event) {
    case ESP_GATTC_REG_EVT:
        if (param->reg.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "gattc register failed status=%u", param->reg.status);
            return;
        }
        s_gattc_if = gattc_if;
        ESP_LOGI(TAG, "gattc registered if=%u", s_gattc_if);
        start_scan();
        break;
    case ESP_GATTC_OPEN_EVT:
        s_connecting = false;
        if (param->open.status != ESP_GATT_OK) {
            ESP_LOGE(TAG, "open failed status=%u", param->open.status);
            start_scan();
            break;
        }
        ESP_LOGI(TAG, "open ok mtu=%u", param->open.mtu);
        break;
    case ESP_GATTC_CONNECT_EVT:
        s_connected = true;
        s_ring_status = "connected";
        s_conn_id = param->connect.conn_id;
        memcpy(s_remote_bda, param->connect.remote_bda, sizeof(s_remote_bda));
        ESP_LOGI(TAG, "connected conn_id=%u remote=" ESP_BD_ADDR_STR,
                 s_conn_id, ESP_BD_ADDR_HEX(s_remote_bda));
        reset_service_handles();
        log_heap("after gap connect");
        esp_ble_gattc_send_mtu_req(gattc_if, s_conn_id);
        break;
    case ESP_GATTC_DIS_SRVC_CMPL_EVT:
        ESP_LOGI(TAG, "service discovery complete status=%u", param->dis_srvc_cmpl.status);
        if (param->dis_srvc_cmpl.status == ESP_GATT_OK) {
            esp_ble_gattc_search_service(gattc_if, param->dis_srvc_cmpl.conn_id, NULL);
        }
        break;
    case ESP_GATTC_CFG_MTU_EVT:
        ESP_LOGI(TAG, "mtu status=%u mtu=%u", param->cfg_mtu.status, param->cfg_mtu.mtu);
        break;
    case ESP_GATTC_SEARCH_RES_EVT:
        if (uuid128_eq(&param->search_res.srvc_id.uuid, UUID_UART_SERVICE)) {
            s_uart.found = true;
            s_uart.start_handle = param->search_res.start_handle;
            s_uart.end_handle = param->search_res.end_handle;
            ESP_LOGI(TAG, "uart service start=%u end=%u", s_uart.start_handle, s_uart.end_handle);
        } else if (uuid128_eq(&param->search_res.srvc_id.uuid, UUID_MAIN_SERVICE)) {
            s_main.found = true;
            s_main.start_handle = param->search_res.start_handle;
            s_main.end_handle = param->search_res.end_handle;
            ESP_LOGI(TAG, "main service start=%u end=%u", s_main.start_handle, s_main.end_handle);
        }
        break;
    case ESP_GATTC_SEARCH_CMPL_EVT:
        handle_search_complete(param);
        break;
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
        ESP_LOGI(TAG, "register notify status=%u handle=%u",
                 param->reg_for_notify.status, param->reg_for_notify.handle);
        uint16_t notify_en = 1;
        uint16_t cccd = 0;
        if (param->reg_for_notify.handle == s_uart.notify_handle) {
            cccd = s_uart.cccd_handle;
            s_uart.subscribed = param->reg_for_notify.status == ESP_GATT_OK;
        } else if (param->reg_for_notify.handle == s_main.notify_handle) {
            cccd = s_main.cccd_handle;
            s_main.subscribed = param->reg_for_notify.status == ESP_GATT_OK;
        }
        if (param->reg_for_notify.status == ESP_GATT_OK && cccd) {
            esp_ble_gattc_write_char_descr(gattc_if, s_conn_id, cccd, sizeof(notify_en),
                                           (uint8_t *)&notify_en, ESP_GATT_WRITE_TYPE_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
        } else if (s_pending_subscribes > 0) {
            --s_pending_subscribes;
            maybe_ready_after_subscribe();
        }
        break;
    }
    case ESP_GATTC_WRITE_DESCR_EVT:
        ESP_LOGI(TAG, "descriptor write status=%u handle=%u", param->write.status, param->write.handle);
        if (s_pending_subscribes > 0) {
            --s_pending_subscribes;
        }
        maybe_ready_after_subscribe();
        break;
    case ESP_GATTC_NOTIFY_EVT:
        parse_notify(param->notify.value, param->notify.value_len);
        break;
    case ESP_GATTC_WRITE_CHAR_EVT:
        ESP_LOGI(TAG, "char write status=%u handle=%u", param->write.status, param->write.handle);
        if (s_tx_count > 0) {
            s_tx_head = (uint8_t)((s_tx_head + 1) % TX_QUEUE_LEN);
            --s_tx_count;
        }
        s_tx_busy = false;
        pump_tx_queue();
        break;
    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "disconnect reason=0x%02x remote=" ESP_BD_ADDR_STR,
                 param->disconnect.reason, ESP_BD_ADDR_HEX(param->disconnect.remote_bda));
        s_connected = false;
        s_connecting = false;
        s_ring_status = "disconnected";
        reset_service_handles();
        start_scan();
        break;
    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(ret);
    }

    ESP_LOGI(TAG, "starting Bluedroid Colmi client");
    log_heap("boot");
    display_begin();
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_ble_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_ble_gattc_register_callback(gattc_cb));
    ESP_ERROR_CHECK(esp_ble_gatt_set_local_mtu(64));
    ESP_ERROR_CHECK(esp_ble_gattc_app_register(0));
}
