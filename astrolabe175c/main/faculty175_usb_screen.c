#include "faculty175_usb_screen.h"

#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "faculty175_board.h"

static const char *TAG = "faculty175_usb_screen";
static StaticSemaphore_t s_frame_mutex_storage;
static SemaphoreHandle_t s_frame_mutex;
static portMUX_TYPE s_frame_init_mux = portMUX_INITIALIZER_UNLOCKED;
static uint16_t *s_frame;
static int s_frame_width;
static int s_frame_height;

static SemaphoreHandle_t frame_mutex(void)
{
    if (s_frame_mutex == NULL) {
        portENTER_CRITICAL(&s_frame_init_mux);
        if (s_frame_mutex == NULL) {
            s_frame_mutex = xSemaphoreCreateMutexStatic(&s_frame_mutex_storage);
        }
        portEXIT_CRITICAL(&s_frame_init_mux);
    }
    return s_frame_mutex;
}

bool faculty175_usb_screen_active(void)
{
    SemaphoreHandle_t mutex = frame_mutex();
    if (mutex == NULL || xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    const bool active = s_frame != NULL;
    xSemaphoreGive(mutex);
    return active;
}

void faculty175_usb_screen_stop(void)
{
    SemaphoreHandle_t mutex = frame_mutex();
    if (mutex == NULL || xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    uint16_t *old = s_frame;
    s_frame = NULL;
    s_frame_width = 0;
    s_frame_height = 0;
    xSemaphoreGive(mutex);
    heap_caps_free(old);
}

void faculty175_usb_screen_draw_face(uint32_t anim_ms)
{
    (void)anim_ms;
    SemaphoreHandle_t mutex = frame_mutex();
    if (mutex != NULL && xSemaphoreTake(mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_frame != NULL) {
            const int x = (FACULTY175_LCD_W - s_frame_width) / 2;
            const int y = (FACULTY175_LCD_H - s_frame_height) / 2;
            faculty175_display_fill_rgb565(0);
            faculty175_display_draw_rgb565(s_frame, x, y, s_frame_width, s_frame_height);
            xSemaphoreGive(mutex);
            return;
        }
        xSemaphoreGive(mutex);
    }
    faculty175_display_fill_rgb565(0);
    const uint16_t accent = faculty175_display_rgb888(82, 210, 190);
    const uint16_t dim = faculty175_display_rgb888(112, 126, 134);
    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2 - 24, 92, dim);
    faculty175_display_draw_text("USB SCREEN", 177, 212, accent);
    faculty175_display_draw_text("WAITING FOR PI", 164, 252, dim);
}

esp_err_t faculty175_usb_screen_show_jpeg(const uint8_t *jpeg, size_t length)
{
    if (jpeg == NULL || length < 64 || length > FACULTY175_USB_SCREEN_MAX_JPEG ||
        jpeg[0] != 0xff || jpeg[1] != 0xd8) {
        return ESP_ERR_INVALID_ARG;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_dec_handle_t decoder = NULL;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK || decoder == NULL) {
        return ESP_FAIL;
    }

    jpeg_dec_io_t *io = heap_caps_calloc(1, sizeof(*io), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    jpeg_dec_header_info_t *info = heap_caps_aligned_alloc(16, sizeof(*info),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (io == NULL || info == NULL) {
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return ESP_ERR_NO_MEM;
    }
    io->inbuf = (uint8_t *)jpeg;
    io->inbuf_len = (int)length;
    const uint8_t *base = jpeg;
    jpeg_error_t jerr = jpeg_dec_parse_header(decoder, io, info);
    if (jerr != JPEG_ERR_OK || info->width <= 0 || info->height <= 0 ||
        info->width > FACULTY175_LCD_W || info->height > FACULTY175_LCD_H) {
        ESP_LOGW(TAG, "JPEG header rejected err=%d size=%dx%d", (int)jerr, info->width, info->height);
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return ESP_ERR_INVALID_SIZE;
    }

    const int consumed = io->inbuf_len - io->inbuf_remain;
    io->inbuf = (uint8_t *)(base + consumed);
    io->inbuf_len = io->inbuf_remain;
    int out_bytes = 0;
    if (jpeg_dec_get_outbuf_len(decoder, &out_bytes) != JPEG_ERR_OK || out_bytes <= 0) {
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return ESP_FAIL;
    }
    uint8_t *rgb = heap_caps_aligned_alloc(16, (size_t)out_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t pixels = (size_t)info->width * (size_t)info->height;
    uint16_t *rgb565 = heap_caps_malloc(pixels * sizeof(*rgb565), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (rgb == NULL || rgb565 == NULL) {
        heap_caps_free(rgb);
        heap_caps_free(rgb565);
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return ESP_ERR_NO_MEM;
    }
    io->outbuf = rgb;
    io->out_size = out_bytes;
    jerr = jpeg_dec_process(decoder, io);
    jpeg_dec_close(decoder);
    free(io);
    if (jerr != JPEG_ERR_OK) {
        heap_caps_free(rgb);
        heap_caps_free(rgb565);
        free(info);
        return ESP_FAIL;
    }
    for (size_t i = 0; i < pixels; ++i) {
        rgb565[i] = faculty175_display_rgb888(rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]);
    }
    SemaphoreHandle_t mutex = frame_mutex();
    if (mutex == NULL || xSemaphoreTake(mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        heap_caps_free(rgb);
        heap_caps_free(rgb565);
        free(info);
        return ESP_ERR_TIMEOUT;
    }
    uint16_t *old = s_frame;
    s_frame = rgb565;
    s_frame_width = info->width;
    s_frame_height = info->height;
    xSemaphoreGive(mutex);
    ESP_LOGD(TAG, "frame %dx%d jpeg=%u", info->width, info->height, (unsigned)length);
    heap_caps_free(rgb);
    heap_caps_free(old);
    free(info);
    return ESP_OK;
}
