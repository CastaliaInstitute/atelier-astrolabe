#include "faculty175_face_eye.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/uvc_host.h"

#include "faculty175_board.h"

#define EYE_USB_VID 0x303a
#define EYE_USB_PID 0x8000
#define EYE_FRAME_MAX_W 480
#define EYE_FRAME_MAX_H 320
#define EYE_JPEG_CAPACITY (256u * 1024u)
#define EYE_CAPTURE_DIVISOR 5u
#define EYE_USB_TASK_PRIORITY 15
#define EYE_STREAM_TASK_PRIORITY 12
#define EYE_DECODE_TASK_PRIORITY 3

typedef enum {
    EYE_STATE_OFF = 0,
    EYE_STATE_WAITING,
    EYE_STATE_STREAMING,
    EYE_STATE_ERROR,
} eye_state_t;

static const char *TAG = "faculty175_eye";

static SemaphoreHandle_t s_jpeg_lock;
static SemaphoreHandle_t s_frame_lock;
static SemaphoreHandle_t s_disconnected;
static TaskHandle_t s_decode_task;
static uint8_t *s_jpeg_pending;
static uint8_t *s_jpeg_work;
static size_t s_jpeg_pending_len;
static uint16_t *s_frame_front;
static uint16_t *s_frame_back;
static int s_frame_w;
static int s_frame_h;
static volatile eye_state_t s_state = EYE_STATE_OFF;
static volatile esp_err_t s_last_error = ESP_OK;
static volatile uint32_t s_frames_received;
static volatile uint32_t s_frames_decoded;
static volatile bool s_started;
static volatile bool s_host_installed;
static volatile bool s_uvc_installed;
static volatile bool s_stream_open;
static volatile uint16_t s_stream_w;
static volatile uint16_t s_stream_h;
static volatile uint16_t s_stream_fps;
static volatile uint32_t s_open_attempts;
static volatile uint32_t s_disconnects;
static volatile uint32_t s_transfer_errors;

static const char *eye_state_label(void)
{
    switch (s_state) {
        case EYE_STATE_WAITING: return "CONNECT ASTROLABE EYE";
        case EYE_STATE_STREAMING: return s_frames_decoded > 0 ? "UVC LIVE" : "STARTING STREAM";
        case EYE_STATE_ERROR: return "UVC HOST ERROR";
        case EYE_STATE_OFF:
        default: return "UVC HOST OFF";
    }
}

static void usb_library_task(void *arg)
{
    (void)arg;
    while (true) {
        uint32_t event_flags = 0;
        const esp_err_t err = usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "USB library event error: %s", esp_err_to_name(err));
            continue;
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0) {
            (void)usb_host_device_free_all();
        }
    }
}

static bool decode_mjpeg_rgb565(const uint8_t *jpeg, size_t jpeg_len, uint16_t *out, int *out_w, int *out_h)
{
    if (jpeg == NULL || jpeg_len < 64 || out == NULL || out_w == NULL || out_h == NULL) {
        return false;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB888;

    jpeg_dec_handle_t decoder = NULL;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK || decoder == NULL) {
        return false;
    }

    jpeg_dec_io_t *io = heap_caps_calloc(1, sizeof(*io), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    jpeg_dec_header_info_t *info =
        heap_caps_aligned_alloc(16, sizeof(*info), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (io == NULL || info == NULL) {
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return false;
    }

    const uint8_t *jpeg_base = jpeg;
    io->inbuf = (uint8_t *)jpeg;
    io->inbuf_len = (int)jpeg_len;
    jpeg_error_t result = jpeg_dec_parse_header(decoder, io, info);
    if (result != JPEG_ERR_OK || info->width <= 0 || info->height <= 0 ||
        info->width > EYE_FRAME_MAX_W || info->height > EYE_FRAME_MAX_H) {
        ESP_LOGW(TAG, "unsupported MJPEG frame: err=%d size=%dx%d", (int)result, info->width, info->height);
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return false;
    }

    const int width = info->width;
    const int height = info->height;
    const int consumed = io->inbuf_len - io->inbuf_remain;
    io->inbuf = (uint8_t *)(jpeg_base + consumed);
    io->inbuf_len = io->inbuf_remain;

    int native_len = 0;
    if (jpeg_dec_get_outbuf_len(decoder, &native_len) != JPEG_ERR_OK || native_len <= 0) {
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return false;
    }
    uint8_t *native = heap_caps_aligned_alloc(16, (size_t)native_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (native == NULL) {
        native = heap_caps_aligned_alloc(16, (size_t)native_len, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (native == NULL) {
        free(io);
        free(info);
        jpeg_dec_close(decoder);
        return false;
    }

    io->outbuf = native;
    io->out_size = native_len;
    result = jpeg_dec_process(decoder, io);
    jpeg_dec_close(decoder);
    free(io);
    free(info);
    if (result != JPEG_ERR_OK) {
        heap_caps_free(native);
        ESP_LOGW(TAG, "MJPEG decode failed: %d", (int)result);
        return false;
    }

    const size_t pixel_count = (size_t)width * (size_t)height;
    for (size_t i = 0; i < pixel_count; ++i) {
        const uint8_t *pixel = native + i * 3u;
        out[i] = faculty175_display_rgb888(pixel[0], pixel[1], pixel[2]);
    }
    heap_caps_free(native);
    *out_w = width;
    *out_h = height;
    return true;
}

static void decode_task(void *arg)
{
    (void)arg;
    while (true) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        size_t jpeg_len = 0;
        if (xSemaphoreTake(s_jpeg_lock, portMAX_DELAY) == pdTRUE) {
            uint8_t *swap = s_jpeg_work;
            s_jpeg_work = s_jpeg_pending;
            s_jpeg_pending = swap;
            jpeg_len = s_jpeg_pending_len;
            s_jpeg_pending_len = 0;
            xSemaphoreGive(s_jpeg_lock);
        }
        if (jpeg_len == 0) {
            continue;
        }

        uint16_t *decode_target = NULL;
        if (xSemaphoreTake(s_frame_lock, portMAX_DELAY) == pdTRUE) {
            decode_target = s_frame_back;
            xSemaphoreGive(s_frame_lock);
        }
        int width = 0;
        int height = 0;
        if (decode_target == NULL || !decode_mjpeg_rgb565(s_jpeg_work, jpeg_len, decode_target, &width, &height)) {
            s_last_error = ESP_FAIL;
            continue;
        }

        if (xSemaphoreTake(s_frame_lock, portMAX_DELAY) == pdTRUE) {
            uint16_t *swap = s_frame_front;
            s_frame_front = s_frame_back;
            s_frame_back = swap;
            s_frame_w = width;
            s_frame_h = height;
            ++s_frames_decoded;
            xSemaphoreGive(s_frame_lock);
        }
    }
}

static bool frame_callback(const uvc_host_frame_t *frame, void *user_ctx)
{
    (void)user_ctx;
    if (frame == NULL || frame->vs_format.format != UVC_VS_FORMAT_MJPEG || frame->data == NULL) {
        return true;
    }
    const uint32_t frame_number = ++s_frames_received;
    if ((frame_number % EYE_CAPTURE_DIVISOR) != 0 || frame->data_len > EYE_JPEG_CAPACITY) {
        return true;
    }
    if (xSemaphoreTake(s_jpeg_lock, 0) != pdTRUE) {
        return true;
    }
    memcpy(s_jpeg_pending, frame->data, frame->data_len);
    s_jpeg_pending_len = frame->data_len;
    xSemaphoreGive(s_jpeg_lock);
    if (s_decode_task != NULL) {
        xTaskNotifyGive(s_decode_task);
    }
    return true;
}

static void stream_event_callback(const uvc_host_stream_event_data_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (event == NULL) {
        return;
    }
    switch (event->type) {
        case UVC_HOST_TRANSFER_ERROR:
            s_last_error = event->transfer_error.error;
            ++s_transfer_errors;
            ESP_LOGW(TAG, "UVC transfer error: %s", esp_err_to_name(s_last_error));
            break;
        case UVC_HOST_DEVICE_DISCONNECTED:
            s_state = EYE_STATE_WAITING;
            s_stream_open = false;
            ++s_disconnects;
            ESP_LOGI(TAG, "Astrolabe Eye disconnected");
            (void)uvc_host_stream_close(event->device_disconnected.stream_hdl);
            xSemaphoreGive(s_disconnected);
            break;
        case UVC_HOST_FRAME_BUFFER_OVERFLOW:
            ESP_LOGW(TAG, "UVC frame overflow");
            break;
        case UVC_HOST_FRAME_BUFFER_UNDERFLOW:
            ESP_LOGW(TAG, "UVC frame underflow");
            break;
#ifdef UVC_HOST_SUSPEND_RESUME_API_SUPPORTED
        case UVC_HOST_DEVICE_SUSPENDED:
            s_state = EYE_STATE_WAITING;
            break;
        case UVC_HOST_DEVICE_RESUMED:
            break;
#endif
        default:
            break;
    }
}

static esp_err_t open_and_run_mode(unsigned width, unsigned height, float fps)
{
    ++s_open_attempts;
    const uvc_host_stream_config_t config = {
        .event_cb = stream_event_callback,
        .frame_cb = frame_callback,
        .user_ctx = NULL,
        .usb = {
            .dev_addr = UVC_HOST_ANY_DEV_ADDR,
            .vid = EYE_USB_VID,
            .pid = EYE_USB_PID,
            .uvc_stream_index = 0,
        },
        .vs_format = {
            .h_res = width,
            .v_res = height,
            .fps = fps,
            .format = UVC_VS_FORMAT_MJPEG,
        },
        .advanced = {
            .number_of_frame_buffers = 3,
            .frame_size = EYE_JPEG_CAPACITY,
            .frame_heap_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
            .number_of_urbs = 4,
            .urb_size = 10u * 1024u,
            .user_frame_buffers = NULL,
        },
    };

    uvc_host_stream_hdl_t stream = NULL;
    esp_err_t err = uvc_host_stream_open(&config, pdMS_TO_TICKS(4000), &stream);
    if (err != ESP_OK) {
        return err;
    }
    uvc_host_desc_print(stream);
    err = uvc_host_stream_start(stream);
    if (err != ESP_OK) {
        (void)uvc_host_stream_close(stream);
        return err;
    }

    s_frames_received = 0;
    s_stream_w = (uint16_t)width;
    s_stream_h = (uint16_t)height;
    s_stream_fps = (uint16_t)fps;
    s_stream_open = true;
    s_state = EYE_STATE_STREAMING;
    s_last_error = ESP_OK;
    ESP_LOGI(TAG, "Astrolabe Eye streaming %ux%u@%.1f MJPEG", width, height, (double)fps);
    (void)xSemaphoreTake(s_disconnected, portMAX_DELAY);
    return ESP_OK;
}

static void stream_task(void *arg)
{
    (void)arg;
    static const struct {
        unsigned width;
        unsigned height;
        float fps;
    } modes[] = {
        {480, 320, 30.0f},
        {320, 240, 30.0f},
    };

    while (true) {
        s_state = EYE_STATE_WAITING;
        for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
            const esp_err_t err = open_and_run_mode(modes[i].width, modes[i].height, modes[i].fps);
            if (err == ESP_OK) {
                break;
            }
            s_last_error = err;
            ESP_LOGI(TAG,
                     "waiting for %04x:%04x %ux%u MJPEG: %s",
                     EYE_USB_VID,
                     EYE_USB_PID,
                     modes[i].width,
                     modes[i].height,
                     esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

esp_err_t faculty175_face_eye_init(void)
{
    if (s_started) {
        return ESP_OK;
    }

    s_jpeg_lock = xSemaphoreCreateMutex();
    s_frame_lock = xSemaphoreCreateMutex();
    s_disconnected = xSemaphoreCreateBinary();
    s_jpeg_pending = heap_caps_malloc(EYE_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpeg_work = heap_caps_malloc(EYE_JPEG_CAPACITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t display_bytes = EYE_FRAME_MAX_W * EYE_FRAME_MAX_H * sizeof(uint16_t);
    s_frame_front = heap_caps_malloc(display_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frame_back = heap_caps_malloc(display_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_jpeg_lock == NULL || s_frame_lock == NULL || s_disconnected == NULL ||
        s_jpeg_pending == NULL || s_jpeg_work == NULL || s_frame_front == NULL || s_frame_back == NULL) {
        s_state = EYE_STATE_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        s_state = EYE_STATE_ERROR;
        s_last_error = err;
        return err;
    }
    s_host_installed = true;
    if (xTaskCreatePinnedToCore(usb_library_task,
                                "eye_usb_lib",
                                4096,
                                NULL,
                                EYE_USB_TASK_PRIORITY,
                                NULL,
                                tskNO_AFFINITY) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    const uvc_host_driver_config_t uvc_config = {
        .driver_task_stack_size = 4u * 1024u,
        .driver_task_priority = EYE_USB_TASK_PRIORITY + 1,
        .xCoreID = tskNO_AFFINITY,
        .create_background_task = true,
        .event_cb = NULL,
        .user_ctx = NULL,
    };
    err = uvc_host_install(&uvc_config);
    if (err != ESP_OK) {
        s_state = EYE_STATE_ERROR;
        s_last_error = err;
        return err;
    }
    s_uvc_installed = true;
    if (xTaskCreatePinnedToCoreWithCaps(decode_task,
                                        "eye_decode",
                                        6144,
                                        NULL,
                                        EYE_DECODE_TASK_PRIORITY,
                                        &s_decode_task,
                                        tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS ||
        xTaskCreatePinnedToCoreWithCaps(stream_task,
                                        "eye_stream",
                                        6144,
                                        NULL,
                                        EYE_STREAM_TASK_PRIORITY,
                                        NULL,
                                        tskNO_AFFINITY,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_state = EYE_STATE_ERROR;
        s_last_error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    s_state = EYE_STATE_WAITING;
    ESP_LOGI(TAG, "UVC host ready for Astrolabe Eye %04x:%04x", EYE_USB_VID, EYE_USB_PID);
    return ESP_OK;
}

bool faculty175_face_eye_has_frame(void)
{
    return s_frames_decoded > 0;
}

void faculty175_face_eye_usb_status(faculty175_eye_usb_status_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (faculty175_eye_usb_status_t){
        .initialized = s_started,
        .host_installed = s_host_installed,
        .uvc_installed = s_uvc_installed,
        .stream_open = s_stream_open,
        .state = eye_state_label(),
        .last_error = s_last_error,
        .target_vid = EYE_USB_VID,
        .target_pid = EYE_USB_PID,
        .width = s_stream_w,
        .height = s_stream_h,
        .fps = s_stream_fps,
        .open_attempts = s_open_attempts,
        .disconnects = s_disconnects,
        .transfer_errors = s_transfer_errors,
        .frames_received = s_frames_received,
        .frames_decoded = s_frames_decoded,
    };
    if (s_host_installed) {
        usb_host_lib_info_t info = {};
        if (usb_host_lib_info(&info) == ESP_OK) {
            out->connected_devices = info.num_devices;
            out->registered_clients = info.num_clients;
        }
    }
}

void faculty175_face_eye_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    const uint16_t black = faculty175_display_rgb888(0, 0, 0);
    const uint16_t cyan = faculty175_display_rgb888(80, 232, 255);
    const uint16_t dim = faculty175_display_rgb888(38, 82, 94);
    const uint16_t amber = faculty175_display_rgb888(255, 190, 78);
    faculty175_display_fill_rgb565(black);

    bool drew_frame = false;
    if (s_frames_decoded > 0 && xSemaphoreTake(s_frame_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_frame_front != NULL && s_frame_w > 0 && s_frame_h > 0) {
            const int draw_w = s_frame_w > FACULTY175_LCD_W ? FACULTY175_LCD_W : s_frame_w;
            const int src_x = (s_frame_w - draw_w) / 2;
            const int dst_x = (FACULTY175_LCD_W - draw_w) / 2;
            const int dst_y = (FACULTY175_LCD_H - s_frame_h) / 2;
            faculty175_display_draw_rgb565_stride(s_frame_front + src_x,
                                                   s_frame_w,
                                                   dst_x,
                                                   dst_y,
                                                   draw_w,
                                                   s_frame_h);
            drew_frame = true;
        }
        xSemaphoreGive(s_frame_lock);
    }

    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 231, drew_frame ? cyan : dim);
    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 226, dim);
    faculty175_display_draw_centered_text("ASTROLABE EYE", 18, drew_frame ? cyan : dim);

    char status[48];
    if (s_state == EYE_STATE_ERROR) {
        snprintf(status, sizeof(status), "%s %s", eye_state_label(), esp_err_to_name(s_last_error));
    } else if (drew_frame) {
        snprintf(status, sizeof(status), "%s  %lux%lu", eye_state_label(),
                 (unsigned long)s_frames_decoded, (unsigned long)s_frames_received);
    } else {
        snprintf(status, sizeof(status), "%s", eye_state_label());
    }
    faculty175_display_draw_centered_text(status, FACULTY175_LCD_H - 31,
                                          s_state == EYE_STATE_ERROR ? amber : (drew_frame ? cyan : dim));
    faculty175_display_flush();
}
