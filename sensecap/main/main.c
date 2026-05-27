#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include "nvs_flash.h"
#include "astrolabe_baseline.h"
#include "sensecap-watcher.h"
#include "sscma_client.h"

#if __has_include("secrets.local.h")
#include "secrets.local.h"
#else
#include "secrets.example.h"
#endif

#ifndef MYNAH_FACE_METRICS_URL
#define MYNAH_FACE_METRICS_URL ASTROLABE_FACE_METRICS_URL_DEFAULT
#endif

static const char *TAG = "astrolabe_sensecap";

#define CAMERA_SENSOR_ID 1
#define CAMERA_SENSOR_RESOLUTION_416_416 1
#define CAMERA_IMAGE_WIDTH 416
#define CAMERA_IMAGE_HEIGHT 416
#define CAMERA_JPEG_BUF_SIZE (96 * 1024)
#define CAMERA_RGB565_BUF_SIZE (CAMERA_IMAGE_WIDTH * CAMERA_IMAGE_HEIGHT * sizeof(uint16_t))
#define CAMERA_PANEL_BUF_SIZE (DRV_LCD_H_RES * DRV_LCD_V_RES * sizeof(uint16_t))
#define CAMERA_PANEL_CROP_X ((CAMERA_IMAGE_WIDTH - DRV_LCD_H_RES) / 2)
#define CAMERA_PANEL_CROP_Y ((CAMERA_IMAGE_HEIGHT - DRV_LCD_V_RES) / 2)
#define CAMERA_PREVIEW_MIRROR_X 1
#define FACE_BOX_MAX 6
#define CASTALIA_AUDIO_SECONDS 3
#define CASTALIA_AUDIO_SAMPLE_RATE 16000
#define CASTALIA_AUDIO_BYTES (CASTALIA_AUDIO_SECONDS * CASTALIA_AUDIO_SAMPLE_RATE * sizeof(int16_t))
#define CASTALIA_HTTP_TIMEOUT_MS 20000
#define HTTP_RESPONSE_MAX_BYTES 4096
#define TTS_MP3_MAX_BYTES (384 * 1024)
#define LATEST_BOXES_JSON_BYTES 768
#define WATCHER_MENU_ITEMS 6
#define WATCHER_MENU_ITEM_W 122
#define WATCHER_MENU_ITEM_H 28
#define BEZEL_SELECT_RADIUS 130
#define PRESENCE_GREETING_COOLDOWN_MS 120000
#define PRESENCE_GREETING_MIN_SCORE 40
#define PRESENCE_SETTLE_MS 1500
#define PRESENCE_CENTER_RADIUS_PX 70
#define PRESENCE_STABLE_RADIUS_PX 24
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

typedef enum {
    RGB_STATE_BOOT,
    RGB_STATE_CAMERA_PENDING,
    RGB_STATE_STREAMING,
    RGB_STATE_PRESENCE_SETTLING,
    RGB_STATE_TOUCH,
    RGB_STATE_CASTALIA,
    RGB_STATE_ERROR,
} rgb_state_t;

static const char *rgb_state_name(rgb_state_t state)
{
    switch (state) {
        case RGB_STATE_BOOT:
            return "boot";
        case RGB_STATE_CAMERA_PENDING:
            return "camera_pending";
        case RGB_STATE_STREAMING:
            return "streaming";
        case RGB_STATE_PRESENCE_SETTLING:
            return "presence_settling";
        case RGB_STATE_TOUCH:
            return "touch";
        case RGB_STATE_CASTALIA:
            return "castalia";
        case RGB_STATE_ERROR:
            return "error";
    }
    return "unknown";
}

typedef enum {
    WATCHER_ACTION_ASK_CASTALIA,
    WATCHER_ACTION_FACE_METRICS,
    WATCHER_ACTION_TOGGLE_PRESENCE,
    WATCHER_ACTION_TOGGLE_BOXES,
    WATCHER_ACTION_TOGGLE_RGB,
    WATCHER_ACTION_CLOSE_MENU,
} watcher_action_t;

typedef struct {
    lv_obj_t *camera_image;
    lv_obj_t *scan_arc;
    lv_obj_t *iris_arc;
    lv_obj_t *camera_status;
    lv_obj_t *battery_status;
    lv_obj_t *mac_status;
    lv_obj_t *tick_status;
    lv_obj_t *rgb_status;
    lv_obj_t *frame_status;
    lv_obj_t *touch_status;
    lv_obj_t *touch_dot;
    lv_obj_t *castalia_status;
    lv_obj_t *face_boxes[FACE_BOX_MAX];
    lv_obj_t *menu_layer;
    lv_obj_t *menu_items[WATCHER_MENU_ITEMS];
    bool camera_ready;
    bool streaming;
    bool touching;
    bool castalia_busy;
    bool presence_busy;
    bool menu_visible;
    bool boxes_visible;
    bool rgb_enabled;
    bool presence_enabled;
    int menu_index;
    uint32_t ticks;
    uint32_t frames;
    uint32_t touches;
    uint32_t captures;
    uint32_t detections;
    uint32_t last_frame_ticks;
} camera_face_t;

static camera_face_t s_face;
static volatile rgb_state_t s_rgb_state = RGB_STATE_BOOT;
static EventGroupHandle_t s_wifi_events;
static SemaphoreHandle_t s_latest_image_lock;
static TaskHandle_t s_castalia_task_handle;
static TaskHandle_t s_face_metrics_task_handle;
static TaskHandle_t s_presence_greeting_task_handle;
static bool s_wifi_started;
static int s_wifi_retry_count;
static TickType_t s_last_menu_activate_ticks;
static TickType_t s_last_presence_greeting_ticks;
static TickType_t s_presence_candidate_first_seen_ticks;
static TickType_t s_presence_candidate_last_seen_ticks;
static int s_presence_candidate_x;
static int s_presence_candidate_y;
static char *s_latest_image_b64;
static int s_latest_image_b64_len;
static char s_latest_boxes_json[LATEST_BOXES_JSON_BYTES] = "[]";
static int s_latest_box_count;
static uint8_t *s_jpeg_buf;
static uint8_t *s_frame_buf;
static uint8_t *s_panel_buf;
static lv_img_dsc_t s_frame_dsc = {
    .header.always_zero = 0,
    .header.w = CAMERA_IMAGE_WIDTH,
    .header.h = CAMERA_IMAGE_HEIGHT,
    .data_size = CAMERA_RGB565_BUF_SIZE,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data = NULL,
};
static jpeg_dec_io_t *s_jpeg_io;
static jpeg_dec_header_info_t *s_jpeg_info;

static lv_color_t color_bg(void)
{
    return lv_color_hex(0x06080a);
}

static lv_color_t color_trace(void)
{
    return lv_color_hex(0x27f5a8);
}

static lv_color_t color_warning(void)
{
    return lv_color_hex(0xffb84d);
}

static lv_color_t color_panel(void)
{
    return lv_color_hex(0x10161c);
}

static void set_rgb_state(rgb_state_t state)
{
    if (s_rgb_state != state) {
        ESP_LOGI(TAG, "DEBUG rgb_state %s -> %s", rgb_state_name(s_rgb_state), rgb_state_name(state));
    }
    s_rgb_state = state;
}

static void rgb_task(void *arg)
{
    (void)arg;
    bool logged_once = false;
    rgb_state_t last_logged_state = RGB_STATE_BOOT;
    uint8_t last_logged_r = 255;
    uint8_t last_logged_g = 255;
    uint8_t last_logged_b = 255;
    TickType_t last_logged_ticks = 0;

    while (true) {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;

        switch (s_rgb_state) {
            case RGB_STATE_BOOT:
                r = 1;
                g = 2;
                b = 4;
                break;
            case RGB_STATE_CAMERA_PENDING:
                r = 4;
                g = 2;
                b = 2;
                break;
            case RGB_STATE_STREAMING:
                r = 0;
                g = 3;
                b = 1;
                break;
            case RGB_STATE_PRESENCE_SETTLING:
                r = 0;
                g = 1;
                b = 6;
                break;
            case RGB_STATE_TOUCH:
                r = 1;
                g = 1;
                b = 6;
                break;
            case RGB_STATE_CASTALIA:
                r = 6;
                g = 0;
                b = 4;
                break;
            case RGB_STATE_ERROR:
                r = 8;
                g = 0;
                b = 0;
                break;
        }

        if (!s_face.rgb_enabled &&
            s_rgb_state != RGB_STATE_PRESENCE_SETTLING &&
            s_rgb_state != RGB_STATE_CASTALIA &&
            s_rgb_state != RGB_STATE_ERROR) {
            r = 0;
            g = 0;
            b = 0;
        }

        const TickType_t now = xTaskGetTickCount();
        const bool visible_status = (r != 0 || g != 0 || b != 0);
        if (!logged_once ||
            s_rgb_state != last_logged_state ||
            r != last_logged_r ||
            g != last_logged_g ||
            b != last_logged_b ||
            (visible_status && (now - last_logged_ticks) >= pdMS_TO_TICKS(2000))) {
            ESP_LOGI(TAG, "DEBUG rgb_output state=%s rgb=%u,%u,%u enabled=%d",
                     rgb_state_name(s_rgb_state), r, g, b, s_face.rgb_enabled);
            last_logged_state = s_rgb_state;
            last_logged_r = r;
            last_logged_g = g;
            last_logged_b = b;
            last_logged_ticks = now;
            logged_once = true;
        }
        bsp_rgb_set(r, g, b);
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

static void set_castalia_status(const char *text, bool ok)
{
    if (lvgl_port_lock(0)) {
        if (s_face.castalia_status != NULL) {
            lv_label_set_text(s_face.castalia_status, text);
            lv_obj_set_style_text_color(s_face.castalia_status, ok ? color_trace() : color_warning(), 0);
        }
        lvgl_port_unlock();
    }
}

static const char *watcher_action_label(int index)
{
    static const char *labels[WATCHER_MENU_ITEMS] = {
        "Ask Castalia",
        "Face Metrics",
        "Auto Greet",
        "Boxes",
        "RGB",
        "Close",
    };
    if (index < 0 || index >= WATCHER_MENU_ITEMS) {
        return "";
    }
    return labels[index];
}

typedef struct {
    int16_t cx;
    int16_t cy;
    int16_t angle;
    int16_t w;
} watcher_menu_layout_t;

static const watcher_menu_layout_t s_menu_layout[WATCHER_MENU_ITEMS] = {
    [WATCHER_ACTION_ASK_CASTALIA] = {206, 30, 0, 132},
    [WATCHER_ACTION_FACE_METRICS] = {324, 104, 600, 122},
    [WATCHER_ACTION_TOGGLE_PRESENCE] = {324, 308, -600, 132},
    [WATCHER_ACTION_TOGGLE_BOXES] = {206, 382, 0, 132},
    [WATCHER_ACTION_TOGGLE_RGB] = {88, 308, 600, 112},
    [WATCHER_ACTION_CLOSE_MENU] = {88, 104, -600, 112},
};

static void apply_watcher_menu_layout(void)
{
    for (int i = 0; i < WATCHER_MENU_ITEMS; ++i) {
        if (s_face.menu_items[i] == NULL) {
            continue;
        }

        const watcher_menu_layout_t *layout = &s_menu_layout[i];
        lv_obj_set_size(s_face.menu_items[i], layout->w, WATCHER_MENU_ITEM_H);
        lv_obj_set_pos(s_face.menu_items[i],
                       layout->cx - (layout->w / 2),
                       layout->cy - (WATCHER_MENU_ITEM_H / 2));
        lv_obj_set_style_transform_pivot_x(s_face.menu_items[i], layout->w / 2, 0);
        lv_obj_set_style_transform_pivot_y(s_face.menu_items[i], WATCHER_MENU_ITEM_H / 2, 0);
        lv_obj_set_style_transform_angle(s_face.menu_items[i], layout->angle, 0);
        lv_obj_set_style_transform_width(s_face.menu_items[i], 18, 0);
        lv_obj_set_style_transform_height(s_face.menu_items[i], 18, 0);
    }
}

static void update_menu_item_label(int index)
{
    if (index < 0 || index >= WATCHER_MENU_ITEMS || s_face.menu_items[index] == NULL) {
        return;
    }

    char label[32];
    if (index == WATCHER_ACTION_TOGGLE_PRESENCE) {
        snprintf(label, sizeof(label), "Auto Greet %s", s_face.presence_enabled ? "On" : "Off");
    } else if (index == WATCHER_ACTION_TOGGLE_BOXES) {
        snprintf(label, sizeof(label), "Boxes %s", s_face.boxes_visible ? "On" : "Off");
    } else if (index == WATCHER_ACTION_TOGGLE_RGB) {
        snprintf(label, sizeof(label), "RGB %s", s_face.rgb_enabled ? "On" : "Off");
    } else {
        snprintf(label, sizeof(label), "%s", watcher_action_label(index));
    }
    lv_label_set_text(s_face.menu_items[index], label);
}

static void refresh_watcher_menu(void)
{
    if (s_face.menu_layer == NULL) {
        return;
    }

    for (int i = 0; i < WATCHER_MENU_ITEMS; ++i) {
        if (s_face.menu_items[i] == NULL) {
            continue;
        }
        update_menu_item_label(i);
        const bool selected = i == s_face.menu_index;
        lv_obj_set_style_bg_color(s_face.menu_items[i], selected ? color_trace() : color_panel(), 0);
        lv_obj_set_style_bg_opa(s_face.menu_items[i], selected ? LV_OPA_COVER : LV_OPA_70, 0);
        lv_obj_set_style_text_color(s_face.menu_items[i], selected ? color_bg() : color_trace(), 0);
        lv_obj_set_style_border_color(s_face.menu_items[i], selected ? color_trace() : lv_color_hex(0x27434b), 0);
    }
}

static void set_watcher_menu_visible(bool visible)
{
    if (s_face.menu_layer == NULL) {
        return;
    }
    s_face.menu_visible = visible;
    if (visible) {
        lv_obj_clear_flag(s_face.menu_layer, LV_OBJ_FLAG_HIDDEN);
        refresh_watcher_menu();
    } else {
        lv_obj_add_flag(s_face.menu_layer, LV_OBJ_FLAG_HIDDEN);
    }
}

static void select_watcher_menu_index(int index)
{
    while (index < 0) {
        index += WATCHER_MENU_ITEMS;
    }
    s_face.menu_index = index % WATCHER_MENU_ITEMS;
    refresh_watcher_menu();
}

static void render_face_boxes(const sscma_client_box_t *boxes, int box_count)
{
    int visible = box_count;
    if (visible > FACE_BOX_MAX) {
        visible = FACE_BOX_MAX;
    }

    for (int i = 0; i < FACE_BOX_MAX; ++i) {
        if (s_face.face_boxes[i] == NULL) {
            continue;
        }
        if (!s_face.boxes_visible || i >= visible) {
            lv_obj_add_flag(s_face.face_boxes[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        int x = boxes[i].x - (boxes[i].w / 2) - CAMERA_PANEL_CROP_X;
        int y = boxes[i].y - (boxes[i].h / 2) - CAMERA_PANEL_CROP_Y;
        int w = boxes[i].w;
        int h = boxes[i].h;
        if (x < 0) {
            w += x;
            x = 0;
        }
        if (y < 0) {
            h += y;
            y = 0;
        }
        if (x + w > DRV_LCD_H_RES) {
            w = DRV_LCD_H_RES - x;
        }
        if (y + h > DRV_LCD_V_RES) {
            h = DRV_LCD_V_RES - y;
        }
#if CAMERA_PREVIEW_MIRROR_X
        x = DRV_LCD_H_RES - (x + w);
#endif
        if (w < 6) {
            w = 6;
        }
        if (h < 6) {
            h = 6;
        }

        lv_obj_clear_flag(s_face.face_boxes[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_face.face_boxes[i], x, y);
        lv_obj_set_size(s_face.face_boxes[i], w, h);
    }
}

static bool best_presence_box_center(const sscma_client_box_t *boxes, int box_count, int *x, int *y)
{
    if (boxes == NULL || box_count <= 0 || x == NULL || y == NULL) {
        return false;
    }

    int best = -1;
    int best_score = PRESENCE_GREETING_MIN_SCORE - 1;
    const int visible = box_count > FACE_BOX_MAX ? FACE_BOX_MAX : box_count;
    for (int i = 0; i < visible; ++i) {
        if (boxes[i].score > best_score) {
            best = i;
            best_score = boxes[i].score;
        }
    }
    if (best < 0) {
        return false;
    }

    *x = boxes[best].x - CAMERA_PANEL_CROP_X;
    *y = boxes[best].y - CAMERA_PANEL_CROP_Y;
    return true;
}

static void log_detection_boxes(const sscma_client_box_t *boxes, int box_count)
{
    static TickType_t last_log_ticks;

    if (boxes == NULL || box_count <= 0) {
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if ((now - last_log_ticks) < pdMS_TO_TICKS(500)) {
        return;
    }
    last_log_ticks = now;

    const int limit = box_count < 3 ? box_count : 3;
    for (int i = 0; i < limit; ++i) {
        ESP_LOGI(TAG, "DEBUG detection box[%d/%d] target=%u score=%u center=%u,%u size=%ux%u screen=%d,%d",
                 i + 1, box_count, boxes[i].target, boxes[i].score,
                 boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h,
                 boxes[i].x - CAMERA_PANEL_CROP_X, boxes[i].y - CAMERA_PANEL_CROP_Y);
    }
}

static void reset_presence_settle(void)
{
    s_presence_candidate_first_seen_ticks = 0;
    s_presence_candidate_last_seen_ticks = 0;
    s_presence_candidate_x = 0;
    s_presence_candidate_y = 0;
    if (s_rgb_state == RGB_STATE_PRESENCE_SETTLING) {
        ESP_LOGI(TAG, "DEBUG presence settle reset");
        set_rgb_state(RGB_STATE_STREAMING);
    }
}

static void format_boxes_json(const sscma_client_box_t *boxes, int box_count, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return;
    }

    size_t used = 0;
    int written = snprintf(out, out_size, "[");
    if (written < 0) {
        out[0] = '\0';
        return;
    }
    used = (size_t)written;

    const int visible = box_count > FACE_BOX_MAX ? FACE_BOX_MAX : box_count;
    for (int i = 0; i < visible && used < out_size; ++i) {
        written = snprintf(out + used, out_size - used,
                           "%s{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"score\":%.3f,\"target\":%d}",
                           i == 0 ? "" : ",",
                           boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h,
                           (double)boxes[i].score / 100.0, boxes[i].target);
        if (written < 0 || (size_t)written >= out_size - used) {
            break;
        }
        used += (size_t)written;
    }

    if (used + 2 > out_size) {
        used = out_size - 2;
    }
    out[used++] = ']';
    out[used] = '\0';
}

static void update_latest_image_copy(const char *image, int image_size, const sscma_client_box_t *boxes, int box_count)
{
    if (image == NULL || image_size <= 0 || s_latest_image_lock == NULL) {
        return;
    }

    char *copy = heap_caps_malloc(image_size + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (copy == NULL) {
        ESP_LOGW(TAG, "Latest image copy allocation failed");
        return;
    }
    memcpy(copy, image, image_size);
    copy[image_size] = '\0';

    xSemaphoreTake(s_latest_image_lock, portMAX_DELAY);
    free(s_latest_image_b64);
    s_latest_image_b64 = copy;
    s_latest_image_b64_len = image_size;
    format_boxes_json(boxes, box_count, s_latest_boxes_json, sizeof(s_latest_boxes_json));
    s_latest_box_count = box_count;
    xSemaphoreGive(s_latest_image_lock);
}

static char *take_latest_image_copy(int *out_len, char *boxes_json, size_t boxes_json_size, int *box_count)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (box_count != NULL) {
        *box_count = 0;
    }
    if (boxes_json != NULL && boxes_json_size > 0) {
        snprintf(boxes_json, boxes_json_size, "[]");
    }
    if (s_latest_image_lock == NULL) {
        return NULL;
    }

    xSemaphoreTake(s_latest_image_lock, portMAX_DELAY);
    const int len = s_latest_image_b64_len;
    const char *src = s_latest_image_b64;
    char *copy = NULL;
    if (src != NULL && len > 0) {
        copy = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy != NULL) {
            memcpy(copy, src, len + 1);
            if (out_len != NULL) {
                *out_len = len;
            }
        }
    }
    if (boxes_json != NULL && boxes_json_size > 0) {
        snprintf(boxes_json, boxes_json_size, "%s", s_latest_boxes_json);
    }
    if (box_count != NULL) {
        *box_count = s_latest_box_count;
    }
    xSemaphoreGive(s_latest_image_lock);
    return copy;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_retry_count < 5) {
            s_wifi_retry_count++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "WiFi retry %d", s_wifi_retry_count);
        } else if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_retry_count = 0;
        if (s_wifi_events != NULL) {
            xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        }
        ESP_LOGI(TAG, "WiFi connected");
    }
}

static esp_err_t wifi_connect(void)
{
    if (strlen(MYNAH_WIFI_SSID) == 0) {
        ESP_LOGW(TAG, "No WiFi SSID configured");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_wifi_started) {
        s_wifi_events = xEventGroupCreate();
        if (s_wifi_events == NULL) {
            return ESP_ERR_NO_MEM;
        }

        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL, NULL));

        wifi_config_t wifi_config = {0};
        snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", MYNAH_WIFI_SSID);
        snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", MYNAH_WIFI_PASSWORD);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        s_wifi_started = true;
    } else if (s_wifi_events != NULL) {
        xEventGroupClearBits(s_wifi_events, WIFI_FAIL_BIT);
        esp_wifi_connect();
    }

    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                 pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

static esp_err_t record_pcm(uint8_t **out_pcm, size_t *out_len)
{
    if (out_pcm == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_pcm = NULL;
    *out_len = 0;

    uint8_t *pcm = heap_caps_malloc(CASTALIA_AUDIO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (total < CASTALIA_AUDIO_BYTES) {
        size_t got = 0;
        const size_t want = (CASTALIA_AUDIO_BYTES - total) > 4096 ? 4096 : (CASTALIA_AUDIO_BYTES - total);
        esp_err_t ret = bsp_i2s_read(pcm + total, want, &got, 1000);
        if (ret != ESP_OK) {
            free(pcm);
            return ret;
        }
        total += got;
    }

    *out_pcm = pcm;
    *out_len = total;
    return ESP_OK;
}

static char *json_escape_alloc(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    size_t extra = 1;
    for (const char *p = src; *p; ++p) {
        extra += (*p == '"' || *p == '\\') ? 2 : 1;
    }
    char *out = heap_caps_malloc(extra, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out == NULL) {
        return NULL;
    }
    char *w = out;
    for (const char *p = src; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            *w++ = '\\';
        }
        *w++ = *p;
    }
    *w = '\0';
    return out;
}

static bool json_value_looks_structured(const char *json)
{
    if (json == NULL) {
        return false;
    }
    while (*json == ' ' || *json == '\n' || *json == '\r' || *json == '\t') {
        json++;
    }
    return *json == '{' || *json == '[';
}

static esp_err_t post_body_collect_response(const char *url,
                                            const uint8_t *body,
                                            int body_len,
                                            const char *content_type,
                                            const char *api_key,
                                            char **out_response,
                                            int *out_status)
{
    if (out_response != NULL) {
        *out_response = NULL;
    }
    if (out_status != NULL) {
        *out_status = 0;
    }
    if (url == NULL || body == NULL || body_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CASTALIA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", content_type != NULL ? content_type : "application/octet-stream");
    if (api_key != NULL && api_key[0] != '\0') {
        esp_http_client_set_header(client, "apikey", api_key);
        char auth[512];
        snprintf(auth, sizeof(auth), "Bearer %s", api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret == ESP_OK) {
        int written_total = 0;
        while (written_total < body_len) {
            int written = esp_http_client_write(client, (const char *)body + written_total, body_len - written_total);
            if (written <= 0) {
                ret = ESP_FAIL;
                break;
            }
            written_total += written;
        }
    }

    if (ret == ESP_OK) {
        ret = esp_http_client_fetch_headers(client) >= 0 ? ESP_OK : ESP_FAIL;
    }

    const int status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }

    if (ret == ESP_OK && out_response != NULL) {
        char *response = heap_caps_malloc(HTTP_RESPONSE_MAX_BYTES + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (response != NULL) {
            int total = 0;
            while (total < HTTP_RESPONSE_MAX_BYTES) {
                int read = esp_http_client_read(client, response + total, HTTP_RESPONSE_MAX_BYTES - total);
                if (read <= 0) {
                    break;
                }
                total += read;
            }
            response[total] = '\0';
            if (total > 0) {
                *out_response = response;
            } else {
                free(response);
            }
        }
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return (ret == ESP_OK && status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static esp_err_t post_json_collect_response(const char *url,
                                            const char *body,
                                            int body_len,
                                            const char *api_key,
                                            char **out_response,
                                            int *out_status)
{
    return post_body_collect_response(url, (const uint8_t *)body, body_len,
                                      "application/json", api_key, out_response, out_status);
}

static esp_err_t post_json_collect_mp3_response(const char *url,
                                                const char *body,
                                                int body_len,
                                                const char *api_key,
                                                uint8_t **out_mp3,
                                                size_t *out_mp3_len,
                                                int *out_status)
{
    if (out_mp3 == NULL || out_mp3_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_mp3 = NULL;
    *out_mp3_len = 0;
    if (out_status != NULL) {
        *out_status = 0;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = CASTALIA_HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "audio/mpeg");
    if (api_key != NULL && strlen(api_key) > 0) {
        esp_http_client_set_header(client, "apikey", api_key);
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", api_key);
        esp_http_client_set_header(client, "Authorization", auth);
    }

    esp_err_t ret = esp_http_client_open(client, body_len);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }
    const int written = esp_http_client_write(client, body, body_len);
    if (written != body_len) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    const int content_len = esp_http_client_fetch_headers(client);
    const int status = esp_http_client_get_status_code(client);
    if (out_status != NULL) {
        *out_status = status;
    }
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    size_t cap = content_len > 0 ? (size_t)content_len : 64 * 1024;
    if (cap > TTS_MP3_MAX_BYTES) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    uint8_t *mp3 = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (mp3 == NULL) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total = 0;
    while (true) {
        if (total == cap) {
            size_t next_cap = cap * 2;
            if (next_cap > TTS_MP3_MAX_BYTES) {
                free(mp3);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            uint8_t *next = heap_caps_realloc(mp3, next_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (next == NULL) {
                free(mp3);
                esp_http_client_close(client);
                esp_http_client_cleanup(client);
                return ESP_ERR_NO_MEM;
            }
            mp3 = next;
            cap = next_cap;
        }
        int read = esp_http_client_read(client, (char *)mp3 + total, cap - total);
        if (read < 0) {
            free(mp3);
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        total += (size_t)read;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (total < 64) {
        free(mp3);
        return ESP_FAIL;
    }
    *out_mp3 = mp3;
    *out_mp3_len = total;
    return ESP_OK;
}

static esp_err_t play_mp3_tts(const uint8_t *mp3, size_t mp3_len)
{
    if (mp3 == NULL || mp3_len < 64) {
        return ESP_ERR_INVALID_ARG;
    }

    mp3dec_t dec;
    mp3dec_frame_info_t info;
    int16_t *pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        return ESP_ERR_NO_MEM;
    }

    mp3dec_init(&dec);
    size_t offset = 0;
    bool configured = false;
    int frames = 0;
    esp_err_t ret = ESP_OK;

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_codec_volume_set(80, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_codec_mute_set(false));

    while (offset < mp3_len) {
        memset(&info, 0, sizeof(info));
        const int samples_per_channel = mp3dec_decode_frame(&dec, mp3 + offset,
                                                            (int)(mp3_len - offset),
                                                            pcm, &info);
        if (info.frame_bytes <= 0) {
            break;
        }
        offset += (size_t)info.frame_bytes;
        if (samples_per_channel <= 0 || info.hz <= 0 || info.channels <= 0) {
            continue;
        }

        if (!configured) {
            const i2s_slot_mode_t channel_mode = info.channels == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
            ret = bsp_codec_set_fs((uint32_t)info.hz, 16, channel_mode);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "TTS codec setup failed: %s", esp_err_to_name(ret));
                break;
            }
            configured = true;
        }

        size_t bytes_written = 0;
        const size_t bytes_to_write = (size_t)samples_per_channel * (size_t)info.channels * sizeof(int16_t);
        ret = bsp_i2s_write(pcm, bytes_to_write, &bytes_written, 1000);
        if (ret != ESP_OK || bytes_written != bytes_to_write) {
            ESP_LOGW(TAG, "TTS I2S write failed: %s wrote=%u/%u",
                     esp_err_to_name(ret), (unsigned)bytes_written, (unsigned)bytes_to_write);
            break;
        }
        frames++;
        vTaskDelay(1);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_codec_dev_stop());
    free(pcm);
    ESP_LOGI(TAG, "TTS playback frames=%d bytes=%u", frames, (unsigned)mp3_len);
    return configured && frames > 0 ? ret : ESP_FAIL;
}

static esp_err_t post_face_metrics_payload(const char *image_b64,
                                           int image_len,
                                           const char *boxes_json,
                                           int box_count,
                                           char **out_metrics_json)
{
    if (out_metrics_json != NULL) {
        *out_metrics_json = NULL;
    }
    if (image_b64 == NULL || image_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    (void)boxes_json;

    const size_t jpeg_cap = ((size_t)image_len * 3) / 4 + 4;
    uint8_t *jpeg = heap_caps_malloc(jpeg_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (jpeg == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t jpeg_len = 0;
    if (mbedtls_base64_decode(jpeg, jpeg_cap, &jpeg_len,
                              (const unsigned char *)image_b64, (size_t)image_len) != 0 ||
        jpeg_len == 0) {
        free(jpeg);
        return ESP_FAIL;
    }

    int status = 0;
    char *response = NULL;
    ESP_LOGI(TAG, "Face metrics POST jpeg=%u boxes=%d", (unsigned)jpeg_len, box_count);
    esp_err_t ret = post_body_collect_response(MYNAH_FACE_METRICS_URL, jpeg, (int)jpeg_len,
                                               "image/jpeg", NULL, &response, &status);
    free(jpeg);
    ESP_LOGI(TAG, "Face metrics HTTP status=%d err=%s%s", status, esp_err_to_name(ret),
             response != NULL ? " response captured" : "");

    if (ret == ESP_OK && out_metrics_json != NULL && json_value_looks_structured(response)) {
        *out_metrics_json = response;
    } else {
        free(response);
    }
    return ret;
}

static esp_err_t post_castalia_payload(const uint8_t *pcm,
                                       size_t pcm_len,
                                       const char *image_b64,
                                       int image_len,
                                       const char *boxes_json,
                                       int box_count,
                                       const char *face_metrics_json)
{
    if (pcm == NULL || pcm_len == 0 || image_b64 == NULL || image_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        ESP_LOGW(TAG, "No Supabase config");
        return ESP_ERR_INVALID_STATE;
    }

    const size_t audio_b64_cap = ((pcm_len + 2) / 3) * 4 + 1;
    char *audio_b64 = heap_caps_malloc(audio_b64_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (audio_b64 == NULL) {
        return ESP_ERR_NO_MEM;
    }

    size_t audio_b64_len = 0;
    if (mbedtls_base64_encode((unsigned char *)audio_b64, audio_b64_cap, &audio_b64_len, pcm, pcm_len) != 0) {
        free(audio_b64);
        return ESP_FAIL;
    }
    audio_b64[audio_b64_len] = '\0';

    const char *instruction =
        "You are Castalia on Astrolabe Watcher. Transcribe the user's speech, inspect the camera image, "
        "and follow this baseline policy. " ASTROLABE_MINDFULNESS_POLICY_TEXT " "
        ASTROLABE_MINDFULNESS_RESPONSE_STYLE_TEXT " Answer with concise, situated guidance.";
    char *esc_instruction = json_escape_alloc(instruction);
    if (esc_instruction == NULL) {
        free(audio_b64);
        return ESP_ERR_NO_MEM;
    }

    const char *boxes = boxes_json != NULL ? boxes_json : "[]";
    const bool has_face_metrics = json_value_looks_structured(face_metrics_json);
    const size_t metrics_len = has_face_metrics ? strlen(face_metrics_json) : 0;
    const size_t body_cap = audio_b64_len + image_len + strlen(esc_instruction) + strlen(boxes) + metrics_len + 768;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        free(audio_b64);
        free(esc_instruction);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"sampleRateHertz\":16000,"
                                  "\"face\":\"astrolabe_watcher\",\"systemInstruction\":\"%s\","
                                  "\"audioBase64\":\"%s\",\"imageMime\":\"image/jpeg\",\"imageBase64\":\"%s\","
                                  "\"facialMetricsSource\":\"%s\","
                                  "\"onboardDetections\":{\"model\":\"sscma\",\"boxCount\":%d,\"boxes\":%s}%s%s%s}",
                                  esc_instruction, audio_b64, image_b64,
                                  has_face_metrics ? ASTROLABE_FACE_METRICS_SOURCE_LABEL : "watcher-sscma",
                                  box_count, boxes,
                                  has_face_metrics ? ",\"facialMetrics\":" : "",
                                  has_face_metrics ? face_metrics_json : "",
                                  "");
    free(audio_b64);
    free(esc_instruction);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    ESP_LOGI(TAG, "Castalia POST audio=%u image=%d body=%d", (unsigned)pcm_len, image_len, body_len);
    int status = 0;
    esp_err_t ret = post_json_collect_response(url, body, body_len, MYNAH_SUPABASE_ANON_KEY, NULL, &status);
    ESP_LOGI(TAG, "Castalia HTTP status=%d err=%s", status, esp_err_to_name(ret));
    free(body);
    return ret;
}

static esp_err_t post_presence_greeting_payload(const char *image_b64,
                                                int image_len,
                                                const char *boxes_json,
                                                int box_count,
                                                const char *face_metrics_json,
                                                uint8_t **out_mp3,
                                                size_t *out_mp3_len)
{
    if (out_mp3 != NULL) {
        *out_mp3 = NULL;
    }
    if (out_mp3_len != NULL) {
        *out_mp3_len = 0;
    }
    if (image_b64 == NULL || image_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(MYNAH_SUPABASE_URL) == 0 || strlen(MYNAH_SUPABASE_ANON_KEY) == 0) {
        ESP_LOGW(TAG, "No Supabase config");
        return ESP_ERR_INVALID_STATE;
    }

    const char *instruction =
        "You are Castalia on Astrolabe Watcher. SSCMA has detected a nearby person or face. "
        "Follow this baseline policy. " ASTROLABE_MINDFULNESS_POLICY_TEXT " "
        "Greet them warmly in one short sentence, do not claim identity, and offer a gentle check-in or breath invitation.";
    char message[1024];
    snprintf(message, sizeof(message),
             "Presence event from SenseCAP Watcher. SSCMA detected %d person/face candidate(s). "
             "Boxes: %s. If facial affective cues are available, treat them as uncertain movement observations, "
             "not psychological facts. Please create a brief mindfulness-oriented spoken greeting.",
             box_count, boxes_json != NULL ? boxes_json : "[]");

    char *esc_instruction = json_escape_alloc(instruction);
    char *esc_message = json_escape_alloc(message);
    if (esc_instruction == NULL || esc_message == NULL) {
        free(esc_instruction);
        free(esc_message);
        return ESP_ERR_NO_MEM;
    }

    const char *boxes = boxes_json != NULL ? boxes_json : "[]";
    const bool has_face_metrics = json_value_looks_structured(face_metrics_json);
    const size_t metrics_len = has_face_metrics ? strlen(face_metrics_json) : 0;
    const size_t body_cap = image_len + strlen(esc_instruction) + strlen(esc_message) +
                            strlen(boxes) + metrics_len + 896;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        free(esc_instruction);
        free(esc_message);
        return ESP_ERR_NO_MEM;
    }

    const int body_len = snprintf(body, body_cap,
                                  "{\"languageCode\":\"en-US\",\"face\":\"astrolabe_watcher_presence\","
                                  "\"responseFormat\":\"mp3\","
                                  "\"message\":\"%s\",\"systemInstruction\":\"%s\","
                                  "\"imageMime\":\"image/jpeg\",\"imageBase64\":\"%s\","
                                  "\"facialMetricsSource\":\"%s\","
                                  "\"onboardDetections\":{\"model\":\"sscma\",\"intent\":\"presence-greeting\","
                                  "\"boxCount\":%d,\"boxes\":%s}%s%s%s}",
                                  esc_message, esc_instruction, image_b64,
                                  has_face_metrics ? ASTROLABE_FACE_METRICS_SOURCE_LABEL : "watcher-sscma",
                                  box_count, boxes,
                                  has_face_metrics ? ",\"facialMetrics\":" : "",
                                  has_face_metrics ? face_metrics_json : "",
                                  "");
    free(esc_instruction);
    free(esc_message);
    if (body_len <= 0 || (size_t)body_len >= body_cap) {
        free(body);
        return ESP_ERR_NO_MEM;
    }

    char url[224];
    snprintf(url, sizeof(url), "%s/functions/v1/voice-pipeline", MYNAH_SUPABASE_URL);
    ESP_LOGI(TAG, "Presence greeting POST image=%d boxes=%d body=%d", image_len, box_count, body_len);
    int status = 0;
    esp_err_t ret = post_json_collect_mp3_response(url, body, body_len, MYNAH_SUPABASE_ANON_KEY,
                                                   out_mp3, out_mp3_len, &status);
    ESP_LOGI(TAG, "Presence greeting HTTP status=%d err=%s mp3=%u",
             status, esp_err_to_name(ret),
             out_mp3_len != NULL ? (unsigned)*out_mp3_len : 0);
    free(body);
    return ret;
}

static void notify_castalia_from_knob(const char *source);
static void notify_face_metrics_from_menu(const char *source);
static void maybe_notify_presence_greeting(const sscma_client_box_t *boxes, int box_count);

static void watcher_menu_move(int delta, const char *source)
{
    if (lvgl_port_lock(0)) {
        if (!s_face.menu_visible) {
            set_watcher_menu_visible(true);
        }
        select_watcher_menu_index(s_face.menu_index + delta);
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Menu %s: %s", source, watcher_action_label(s_face.menu_index));
}

static void watcher_menu_activate(const char *source)
{
    const TickType_t now = xTaskGetTickCount();
    if ((now - s_last_menu_activate_ticks) < pdMS_TO_TICKS(280)) {
        return;
    }
    s_last_menu_activate_ticks = now;

    watcher_action_t action = WATCHER_ACTION_CLOSE_MENU;

    if (lvgl_port_lock(0)) {
        if (!s_face.menu_visible) {
            set_watcher_menu_visible(true);
            lvgl_port_unlock();
            ESP_LOGI(TAG, "Menu %s: open", source);
            return;
        }

        action = (watcher_action_t)s_face.menu_index;
        switch (action) {
            case WATCHER_ACTION_TOGGLE_PRESENCE:
                s_face.presence_enabled = !s_face.presence_enabled;
                refresh_watcher_menu();
                break;
            case WATCHER_ACTION_TOGGLE_BOXES:
                s_face.boxes_visible = !s_face.boxes_visible;
                refresh_watcher_menu();
                if (!s_face.boxes_visible) {
                    for (int i = 0; i < FACE_BOX_MAX; ++i) {
                        if (s_face.face_boxes[i] != NULL) {
                            lv_obj_add_flag(s_face.face_boxes[i], LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                }
                break;
            case WATCHER_ACTION_TOGGLE_RGB:
                s_face.rgb_enabled = !s_face.rgb_enabled;
                refresh_watcher_menu();
                break;
            case WATCHER_ACTION_CLOSE_MENU:
                set_watcher_menu_visible(false);
                break;
            case WATCHER_ACTION_ASK_CASTALIA:
            case WATCHER_ACTION_FACE_METRICS:
                set_watcher_menu_visible(false);
                break;
        }
        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "Menu %s activate: %s", source, watcher_action_label(action));
    if (action == WATCHER_ACTION_ASK_CASTALIA) {
        notify_castalia_from_knob("menu");
    } else if (action == WATCHER_ACTION_FACE_METRICS) {
        notify_face_metrics_from_menu("menu");
    }
}

static lv_indev_t *find_encoder_indev(void)
{
    lv_indev_t *indev = NULL;
    while (true) {
        indev = lv_indev_get_next(indev);
        if (indev == NULL || indev->driver->type == LV_INDEV_TYPE_ENCODER) {
            return indev;
        }
    }
}

static void castalia_capture_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_face.castalia_busy) {
            continue;
        }
        s_face.castalia_busy = true;
        s_face.captures++;
        set_rgb_state(RGB_STATE_CASTALIA);
        set_castalia_status("CASTALIA REC", true);

        int image_len = 0;
        char boxes_json[LATEST_BOXES_JSON_BYTES];
        int box_count = 0;
        char *image_b64 = take_latest_image_copy(&image_len, boxes_json, sizeof(boxes_json), &box_count);
        if (image_b64 == NULL) {
            ESP_LOGW(TAG, "Castalia capture has no camera frame yet");
            set_castalia_status("NO CAMERA FRAME", false);
            set_rgb_state(RGB_STATE_ERROR);
            s_face.castalia_busy = false;
            continue;
        }

        uint8_t *pcm = NULL;
        size_t pcm_len = 0;
        esp_err_t ret = record_pcm(&pcm, &pcm_len);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Castalia audio capture failed: %s", esp_err_to_name(ret));
            free(image_b64);
            set_castalia_status("AUDIO FAILED", false);
            set_rgb_state(RGB_STATE_ERROR);
            s_face.castalia_busy = false;
            continue;
        }

        set_castalia_status("CASTALIA SEND", true);
        ret = wifi_connect();
        char *face_metrics_json = NULL;
        if (ret == ESP_OK) {
            esp_err_t metrics_ret = post_face_metrics_payload(image_b64, image_len, boxes_json, box_count, &face_metrics_json);
            if (metrics_ret != ESP_OK) {
                ESP_LOGW(TAG, "Face metrics unavailable, sending onboard detections: %s", esp_err_to_name(metrics_ret));
            }
            ret = post_castalia_payload(pcm, pcm_len, image_b64, image_len, boxes_json, box_count, face_metrics_json);
        }
        free(face_metrics_json);
        free(pcm);
        free(image_b64);

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Castalia capture %" PRIu32 " sent", s_face.captures);
            set_castalia_status("CASTALIA SENT", true);
            set_rgb_state(RGB_STATE_STREAMING);
        } else {
            ESP_LOGW(TAG, "Castalia capture failed: %s", esp_err_to_name(ret));
            set_castalia_status("CASTALIA FAIL", false);
            set_rgb_state(RGB_STATE_ERROR);
        }
        s_face.castalia_busy = false;
    }
}

static void face_metrics_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int image_len = 0;
        char boxes_json[LATEST_BOXES_JSON_BYTES];
        int box_count = 0;
        char *image_b64 = take_latest_image_copy(&image_len, boxes_json, sizeof(boxes_json), &box_count);
        if (image_b64 == NULL) {
            ESP_LOGW(TAG, "Face metrics has no camera frame yet");
            set_rgb_state(RGB_STATE_ERROR);
            continue;
        }

        set_rgb_state(RGB_STATE_CASTALIA);
        esp_err_t ret = wifi_connect();
        char *metrics_json = NULL;
        if (ret == ESP_OK) {
            ret = post_face_metrics_payload(image_b64, image_len, boxes_json, box_count, &metrics_json);
        }
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Face metrics sent (%s)", metrics_json != NULL ? "json" : "empty");
            set_rgb_state(RGB_STATE_STREAMING);
        } else {
            ESP_LOGW(TAG, "Face metrics failed: %s", esp_err_to_name(ret));
            set_rgb_state(RGB_STATE_ERROR);
        }
        free(metrics_json);
        free(image_b64);
    }
}

static void presence_greeting_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_face.presence_busy) {
            continue;
        }
        s_face.presence_busy = true;

        int image_len = 0;
        char boxes_json[LATEST_BOXES_JSON_BYTES];
        int box_count = 0;
        char *image_b64 = take_latest_image_copy(&image_len, boxes_json, sizeof(boxes_json), &box_count);
        if (image_b64 == NULL) {
            ESP_LOGW(TAG, "Presence greeting has no camera frame yet");
            s_face.presence_busy = false;
            continue;
        }

        ESP_LOGI(TAG, "DEBUG greeting task start image=%d boxes=%d", image_len, box_count);
        set_rgb_state(RGB_STATE_CASTALIA);
        set_castalia_status("GREETING SEND", true);
        esp_err_t ret = wifi_connect();
        ESP_LOGI(TAG, "DEBUG greeting wifi=%s", esp_err_to_name(ret));
        char *face_metrics_json = NULL;
        uint8_t *mp3 = NULL;
        size_t mp3_len = 0;
        if (ret == ESP_OK) {
            esp_err_t metrics_ret = post_face_metrics_payload(image_b64, image_len, boxes_json, box_count, &face_metrics_json);
            ESP_LOGI(TAG, "DEBUG greeting face_metrics=%s present=%d",
                     esp_err_to_name(metrics_ret), face_metrics_json != NULL);
            if (metrics_ret != ESP_OK) {
                ESP_LOGW(TAG, "Presence face metrics unavailable: %s", esp_err_to_name(metrics_ret));
            }
            ret = post_presence_greeting_payload(image_b64, image_len, boxes_json, box_count, face_metrics_json, &mp3, &mp3_len);
            ESP_LOGI(TAG, "DEBUG greeting voice_pipeline=%s mp3=%u",
                     esp_err_to_name(ret), (unsigned)mp3_len);
        }

        if (ret == ESP_OK) {
            set_castalia_status("GREETING PLAY", true);
            ESP_LOGI(TAG, "DEBUG greeting playback begin mp3=%u", (unsigned)mp3_len);
            ret = play_mp3_tts(mp3, mp3_len);
            ESP_LOGI(TAG, "DEBUG greeting playback end=%s", esp_err_to_name(ret));
            if (ret == ESP_OK) {
                set_castalia_status("GREETING PLAYED", true);
                set_rgb_state(RGB_STATE_STREAMING);
            } else {
                ESP_LOGW(TAG, "Presence greeting playback failed: %s", esp_err_to_name(ret));
                set_castalia_status("PLAYBACK FAIL", false);
                set_rgb_state(RGB_STATE_ERROR);
            }
        } else {
            ESP_LOGW(TAG, "Presence greeting failed: %s", esp_err_to_name(ret));
            set_castalia_status("GREETING FAIL", false);
            set_rgb_state(RGB_STATE_ERROR);
        }

        free(mp3);
        free(face_metrics_json);
        free(image_b64);
        s_face.presence_busy = false;
    }
}

static void knob_single_click_cb(void *button, void *user_data)
{
    (void)button;
    (void)user_data;
    watcher_menu_activate("click");
}

static void knob_left_cb(void *knob, void *user_data)
{
    (void)knob;
    (void)user_data;
    watcher_menu_move(-1, "left");
}

static void knob_right_cb(void *knob, void *user_data)
{
    (void)knob;
    (void)user_data;
    watcher_menu_move(1, "right");
}

static void notify_castalia_from_knob(const char *source)
{
    ESP_LOGI(TAG, "Knob %s: Castalia capture requested", source);
    if (s_castalia_task_handle != NULL) {
        xTaskNotifyGive(s_castalia_task_handle);
    }
}

static void notify_face_metrics_from_menu(const char *source)
{
    ESP_LOGI(TAG, "Face metrics requested from %s", source);
    if (s_face_metrics_task_handle != NULL) {
        xTaskNotifyGive(s_face_metrics_task_handle);
    }
}

static void maybe_notify_presence_greeting(const sscma_client_box_t *boxes, int box_count)
{
    if (!s_face.presence_enabled || s_presence_greeting_task_handle == NULL || s_face.presence_busy) {
        reset_presence_settle();
        return;
    }

    int x = 0;
    int y = 0;
    if (!best_presence_box_center(boxes, box_count, &x, &y)) {
        reset_presence_settle();
        return;
    }

    const TickType_t now = xTaskGetTickCount();
    if (s_last_presence_greeting_ticks != 0 &&
        (now - s_last_presence_greeting_ticks) < pdMS_TO_TICKS(PRESENCE_GREETING_COOLDOWN_MS)) {
        ESP_LOGI(TAG, "DEBUG presence cooldown active");
        reset_presence_settle();
        return;
    }

    const int dx_center = x - (DRV_LCD_H_RES / 2);
    const int dy_center = y - (DRV_LCD_V_RES / 2);
    const int center_distance_sq = (dx_center * dx_center) + (dy_center * dy_center);
    const int center_radius_sq = PRESENCE_CENTER_RADIUS_PX * PRESENCE_CENTER_RADIUS_PX;
    if (center_distance_sq > center_radius_sq) {
        ESP_LOGI(TAG, "DEBUG presence outside center x=%d y=%d dx=%d dy=%d", x, y, dx_center, dy_center);
        reset_presence_settle();
        return;
    }

    if (s_presence_candidate_first_seen_ticks == 0) {
        s_presence_candidate_first_seen_ticks = now;
        s_presence_candidate_last_seen_ticks = now;
        s_presence_candidate_x = x;
        s_presence_candidate_y = y;
        set_rgb_state(RGB_STATE_PRESENCE_SETTLING);
        ESP_LOGI(TAG, "DEBUG presence settling start x=%d y=%d boxes=%d", x, y, box_count);
        return;
    }

    const int dx_stable = x - s_presence_candidate_x;
    const int dy_stable = y - s_presence_candidate_y;
    const int stable_distance_sq = (dx_stable * dx_stable) + (dy_stable * dy_stable);
    const int stable_radius_sq = PRESENCE_STABLE_RADIUS_PX * PRESENCE_STABLE_RADIUS_PX;
    if (stable_distance_sq > stable_radius_sq) {
        s_presence_candidate_first_seen_ticks = now;
        s_presence_candidate_last_seen_ticks = now;
        s_presence_candidate_x = x;
        s_presence_candidate_y = y;
        set_rgb_state(RGB_STATE_PRESENCE_SETTLING);
        ESP_LOGI(TAG, "DEBUG presence settling restart x=%d y=%d move=%d", x, y, stable_distance_sq);
        return;
    }

    s_presence_candidate_last_seen_ticks = now;
    s_presence_candidate_x = ((s_presence_candidate_x * 3) + x) / 4;
    s_presence_candidate_y = ((s_presence_candidate_y * 3) + y) / 4;
    if ((now - s_presence_candidate_first_seen_ticks) < pdMS_TO_TICKS(PRESENCE_SETTLE_MS)) {
        ESP_LOGI(TAG, "DEBUG presence settling progress elapsed_ms=%u x=%d y=%d",
                 (unsigned)pdTICKS_TO_MS(now - s_presence_candidate_first_seen_ticks), x, y);
        return;
    }

    s_last_presence_greeting_ticks = now;
    reset_presence_settle();
    set_rgb_state(RGB_STATE_CASTALIA);
    ESP_LOGI(TAG, "DEBUG presence trigger settled boxes=%d x=%d y=%d", box_count, x, y);
    xTaskNotifyGive(s_presence_greeting_task_handle);
}

static void knob_poll_task(void *arg)
{
    (void)arg;
    uint8_t last = 1;
    TickType_t last_trigger = 0;

    while (true) {
        const uint8_t level = bsp_exp_io_get_level(BSP_KNOB_BTN);
        const TickType_t now = xTaskGetTickCount();
        if (last == 1 && level == 0 && (now - last_trigger) > pdMS_TO_TICKS(600)) {
            last_trigger = now;
            watcher_menu_activate("press");
        }
        last = level;
        vTaskDelay(pdMS_TO_TICKS(25));
    }
}

static esp_err_t register_knob_click(void)
{
    lv_indev_t *encoder = find_encoder_indev();
    if (encoder == NULL) {
        ESP_LOGW(TAG, "No encoder input found for Castalia click");
        return ESP_ERR_NOT_FOUND;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_encoder_register_event_cb(encoder, KNOB_LEFT, knob_left_cb, NULL));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lvgl_port_encoder_register_event_cb(encoder, KNOB_RIGHT, knob_right_cb, NULL));
    return lvgl_port_encoder_btn_register_event_cb(encoder, BUTTON_SINGLE_CLICK, knob_single_click_cb, NULL);
}

static esp_err_t camera_preview_init(void)
{
    s_jpeg_buf = heap_caps_malloc(CAMERA_JPEG_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_frame_buf = heap_caps_aligned_alloc(16, CAMERA_RGB565_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_panel_buf = heap_caps_aligned_alloc(16, CAMERA_PANEL_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpeg_io = heap_caps_malloc(sizeof(jpeg_dec_io_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_jpeg_info = heap_caps_aligned_alloc(16, sizeof(jpeg_dec_header_info_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (s_jpeg_buf == NULL || s_frame_buf == NULL || s_panel_buf == NULL || s_jpeg_io == NULL || s_jpeg_info == NULL) {
        ESP_LOGE(TAG, "Camera preview buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }

    memset(s_jpeg_io, 0, sizeof(*s_jpeg_io));
    memset(s_jpeg_info, 0, sizeof(*s_jpeg_info));

    s_frame_dsc.data = s_frame_buf;
    return ESP_OK;
}

static esp_err_t decode_camera_frame(const char *base64_image, int base64_len)
{
    size_t jpeg_len = 0;
    jpeg_dec_handle_t jpeg_dec = NULL;
    esp_err_t ret = ESP_OK;

    if (base64_image == NULL || base64_len <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = mbedtls_base64_decode(s_jpeg_buf, CAMERA_JPEG_BUF_SIZE, &jpeg_len,
                                (const unsigned char *)base64_image, base64_len);
    if (ret != 0 || jpeg_len == 0) {
        ESP_LOGW(TAG, "Base64 JPEG decode failed: %d", ret);
        return ESP_FAIL;
    }

    jpeg_dec_config_t config = {
        .output_type = JPEG_RAW_TYPE_RGB565_BE,
        .rotate = JPEG_ROTATE_0D,
    };
    jpeg_dec = jpeg_dec_open(&config);
    if (jpeg_dec == NULL) {
        ESP_LOGW(TAG, "JPEG decoder open failed");
        return ESP_FAIL;
    }

    memset(s_jpeg_io, 0, sizeof(*s_jpeg_io));
    memset(s_jpeg_info, 0, sizeof(*s_jpeg_info));
    s_jpeg_io->inbuf = s_jpeg_buf;
    s_jpeg_io->inbuf_len = jpeg_len;

    ret = jpeg_dec_parse_header(jpeg_dec, s_jpeg_io, s_jpeg_info);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG header parse failed: %s", esp_err_to_name(ret));
        jpeg_dec_close(jpeg_dec);
        return ret;
    }

    const int consumed = s_jpeg_io->inbuf_len - s_jpeg_io->inbuf_remain;
    s_jpeg_io->inbuf = s_jpeg_buf + consumed;
    s_jpeg_io->inbuf_len = s_jpeg_io->inbuf_remain;
    s_jpeg_io->outbuf = s_frame_buf;

    ret = jpeg_dec_process(jpeg_dec, s_jpeg_io);
    jpeg_dec_close(jpeg_dec);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static void draw_camera_frame_direct(void)
{
    esp_lcd_panel_handle_t panel = bsp_lcd_get_panel_handle();
    if (panel == NULL || s_frame_buf == NULL || s_panel_buf == NULL) {
        return;
    }

    const uint8_t *src = s_frame_buf + ((CAMERA_PANEL_CROP_Y * CAMERA_IMAGE_WIDTH + CAMERA_PANEL_CROP_X) * sizeof(uint16_t));
    uint8_t *dst = s_panel_buf;
    const size_t dst_row_bytes = DRV_LCD_H_RES * sizeof(uint16_t);
    const size_t src_row_bytes = CAMERA_IMAGE_WIDTH * sizeof(uint16_t);

    for (int y = 0; y < DRV_LCD_V_RES; ++y) {
        const uint16_t *src_px = (const uint16_t *)(src + (y * src_row_bytes));
        uint16_t *dst_px = (uint16_t *)(dst + (y * dst_row_bytes));
#if CAMERA_PREVIEW_MIRROR_X
        for (int x = 0; x < DRV_LCD_H_RES; ++x) {
            dst_px[x] = src_px[DRV_LCD_H_RES - 1 - x];
        }
#else
        memcpy(dst_px, src_px, dst_row_bytes);
#endif
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_panel_draw_bitmap(panel, 0, 0, DRV_LCD_H_RES, DRV_LCD_V_RES, s_panel_buf));
}

static void camera_on_event(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    (void)client;
    (void)user_ctx;

    char *image = NULL;
    int image_size = 0;
    sscma_client_box_t *boxes = NULL;
    int box_count = 0;
    if (sscma_utils_fetch_image_from_reply(reply, &image, &image_size) != ESP_OK) {
        return;
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(sscma_utils_fetch_boxes_from_reply(reply, &boxes, &box_count));

    log_detection_boxes(boxes, box_count);
    update_latest_image_copy(image, image_size, boxes, box_count);
    maybe_notify_presence_greeting(boxes, box_count);

    if (decode_camera_frame(image, image_size) != ESP_OK) {
        free(image);
        free(boxes);
        set_rgb_state(RGB_STATE_ERROR);
        return;
    }

    draw_camera_frame_direct();

    if (lvgl_port_lock(0)) {
        render_face_boxes(boxes, box_count);
        s_face.detections += box_count;
        s_face.frames++;
        if (s_face.frames == 1 || (s_face.frames % 30) == 0) {
            ESP_LOGI(TAG, "Displayed camera frame %" PRIu32 " (%d bytes base64, boxes=%d)",
                     s_face.frames, image_size, box_count);
        }
        s_face.last_frame_ticks = s_face.ticks;
        s_face.streaming = true;
        lvgl_port_unlock();
    }

    free(image);
    free(boxes);
    if (s_rgb_state != RGB_STATE_PRESENCE_SETTLING && s_rgb_state != RGB_STATE_CASTALIA) {
        set_rgb_state(RGB_STATE_STREAMING);
    }
}

static void camera_on_connect(sscma_client_handle_t client, const sscma_client_reply_t *reply, void *user_ctx)
{
    (void)client;
    (void)reply;
    (void)user_ctx;
    ESP_LOGI(TAG, "SSCMA camera connected");
}

static void update_timer_cb(lv_timer_t *timer)
{
    camera_face_t *face = (camera_face_t *)timer->user_data;
    face->ticks++;

    if (face->streaming && (face->ticks - face->last_frame_ticks) > 36) {
        face->streaming = false;
        if (!face->touching) {
            set_rgb_state(RGB_STATE_CAMERA_PENDING);
        }
    }
}

static int bezel_menu_index_from_point(const lv_point_t *point)
{
    if (point == NULL) {
        return -1;
    }

    const int cx = DRV_LCD_H_RES / 2;
    const int cy = DRV_LCD_V_RES / 2;
    const int dx = point->x - cx;
    const int dy = point->y - cy;
    if ((dx * dx + dy * dy) < (BEZEL_SELECT_RADIUS * BEZEL_SELECT_RADIUS)) {
        return -1;
    }

    if (point->y < 110) {
        return WATCHER_ACTION_ASK_CASTALIA;
    }
    if (point->x > 282 && point->y < (DRV_LCD_V_RES / 2)) {
        return WATCHER_ACTION_FACE_METRICS;
    }
    if (point->x > 282) {
        return WATCHER_ACTION_TOGGLE_PRESENCE;
    }
    if (point->y > 302) {
        return WATCHER_ACTION_TOGGLE_BOXES;
    }
    if (point->x < 130 && point->y >= (DRV_LCD_V_RES / 2)) {
        return WATCHER_ACTION_TOGGLE_RGB;
    }
    if (point->x < 130) {
        return WATCHER_ACTION_CLOSE_MENU;
    }
    return WATCHER_ACTION_CLOSE_MENU;
}

static void touch_event_cb(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING && code != LV_EVENT_RELEASED && code != LV_EVENT_CLICKED) {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (indev == NULL) {
        return;
    }

    lv_point_t point = {0};
    lv_indev_get_point(indev, &point);
    const int bezel_index = bezel_menu_index_from_point(&point);

    if (code == LV_EVENT_PRESSED) {
        s_face.touches++;
        s_face.touching = true;
        ESP_LOGI(TAG, "Touch press %" PRIu32 " at %d,%d", s_face.touches, point.x, point.y);
        set_rgb_state(RGB_STATE_TOUCH);
        if (bezel_index >= 0) {
            set_watcher_menu_visible(true);
            select_watcher_menu_index(bezel_index);
        }
    } else if (code == LV_EVENT_PRESSING && bezel_index >= 0) {
        if (!s_face.menu_visible) {
            set_watcher_menu_visible(true);
        }
        select_watcher_menu_index(bezel_index);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
        s_face.touching = false;
        if (code == LV_EVENT_RELEASED) {
            ESP_LOGI(TAG, "Touch release at %d,%d", point.x, point.y);
        }
        if (bezel_index >= 0 && s_face.menu_visible) {
            watcher_menu_activate("bezel");
        } else if (code == LV_EVENT_CLICKED && !s_face.menu_visible) {
            set_watcher_menu_visible(true);
        }
        set_rgb_state(s_face.streaming ? RGB_STATE_STREAMING : RGB_STATE_CAMERA_PENDING);
    }

}

static void build_camera_face(bool camera_ready)
{
    s_face.camera_ready = camera_ready;
    s_face.boxes_visible = true;
    s_face.rgb_enabled = false;
    s_face.presence_enabled = true;
    s_face.menu_index = WATCHER_ACTION_ASK_CASTALIA;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(screen, DRV_LCD_H_RES, DRV_LCD_V_RES);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x10251f), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    s_face.camera_image = lv_img_create(screen);
    lv_obj_center(s_face.camera_image);
    lv_img_set_zoom(s_face.camera_image, 253);
    lv_obj_set_style_img_opa(s_face.camera_image, LV_OPA_COVER, 0);

    for (int i = 0; i < FACE_BOX_MAX; ++i) {
        s_face.face_boxes[i] = lv_obj_create(screen);
        lv_obj_set_style_bg_opa(s_face.face_boxes[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(s_face.face_boxes[i], 3, 0);
        lv_obj_set_style_border_color(s_face.face_boxes[i], color_trace(), 0);
        lv_obj_set_style_radius(s_face.face_boxes[i], 0, 0);
        lv_obj_set_style_pad_all(s_face.face_boxes[i], 0, 0);
        lv_obj_clear_flag(s_face.face_boxes[i], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_face.face_boxes[i], LV_OBJ_FLAG_HIDDEN);
    }

    s_face.menu_layer = lv_obj_create(screen);
    lv_obj_set_size(s_face.menu_layer, DRV_LCD_H_RES, DRV_LCD_V_RES);
    lv_obj_align(s_face.menu_layer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(s_face.menu_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_face.menu_layer, 0, 0);
    lv_obj_set_style_pad_all(s_face.menu_layer, 0, 0);
    lv_obj_clear_flag(s_face.menu_layer, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < WATCHER_MENU_ITEMS; ++i) {
        s_face.menu_items[i] = lv_label_create(s_face.menu_layer);
        lv_obj_set_size(s_face.menu_items[i], WATCHER_MENU_ITEM_W, WATCHER_MENU_ITEM_H);
        lv_obj_set_style_radius(s_face.menu_items[i], 4, 0);
        lv_obj_set_style_border_width(s_face.menu_items[i], 1, 0);
        lv_obj_set_style_pad_top(s_face.menu_items[i], 5, 0);
        lv_obj_set_style_text_align(s_face.menu_items[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(s_face.menu_items[i], LV_LABEL_LONG_CLIP);
    }
    apply_watcher_menu_layout();
    refresh_watcher_menu();
    set_watcher_menu_visible(false);

    lv_obj_t *touch_layer = lv_obj_create(screen);
    lv_obj_set_size(touch_layer, DRV_LCD_H_RES, DRV_LCD_V_RES);
    lv_obj_align(touch_layer, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(touch_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_layer, 0, 0);
    lv_obj_set_style_pad_all(touch_layer, 0, 0);
    lv_obj_clear_flag(touch_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_layer, touch_event_cb, LV_EVENT_ALL, NULL);

    lv_scr_load(screen);
    lv_timer_create(update_timer_cb, 90, &s_face);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Booting Astrolabe SenseCAP camera face");

    init_nvs();

    bsp_io_expander_init();
    s_latest_image_lock = xSemaphoreCreateMutex();
    if (s_latest_image_lock == NULL) {
        ESP_LOGE(TAG, "Latest image mutex allocation failed");
        abort();
    }

    lv_disp_t *display = bsp_lvgl_init();
    if (display == NULL) {
        ESP_LOGE(TAG, "LVGL display init failed");
        abort();
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_lcd_brightness_set(85));
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_rgb_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_codec_init());
    set_rgb_state(RGB_STATE_CAMERA_PENDING);
    xTaskCreatePinnedToCore(rgb_task, "rgb_task", 2048, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(castalia_capture_task, "castalia_capture", 8192, NULL, 5, &s_castalia_task_handle, 1);
    xTaskCreatePinnedToCore(face_metrics_task, "face_metrics", 6144, NULL, 5, &s_face_metrics_task_handle, 1);
    xTaskCreatePinnedToCore(presence_greeting_task, "presence_greet", 8192, NULL, 5, &s_presence_greeting_task_handle, 1);
    xTaskCreatePinnedToCore(knob_poll_task, "knob_poll", 2048, NULL, 4, NULL, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(camera_preview_init());

    sscma_client_handle_t camera = bsp_sscma_client_init();
    if (camera != NULL) {
        const sscma_client_callback_t callback = {
            .on_connect = camera_on_connect,
            .on_response = camera_on_event,
            .on_event = camera_on_event,
            .on_log = NULL,
        };
        ESP_ERROR_CHECK_WITHOUT_ABORT(sscma_client_register_callback(camera, &callback, NULL));
    }

    const esp_err_t camera_status = camera ? sscma_client_init(camera) : ESP_FAIL;
    ESP_LOGI(TAG, "SSCMA camera init: %s", esp_err_to_name(camera_status));

    if (lvgl_port_lock(0)) {
        build_camera_face(camera_status == ESP_OK);
        lvgl_port_unlock();
    }
    ESP_ERROR_CHECK_WITHOUT_ABORT(register_knob_click());

    if (camera_status == ESP_OK) {
        esp_err_t ret = sscma_client_break(camera);
        ESP_LOGI(TAG, "SSCMA break: %s", esp_err_to_name(ret));

        ret = sscma_client_set_sensor(camera, CAMERA_SENSOR_ID, CAMERA_SENSOR_RESOLUTION_416_416, true);
        ESP_LOGI(TAG, "SSCMA set 416x416 sensor: %s", esp_err_to_name(ret));

        if (ret == ESP_OK) {
            sscma_client_model_t *model = NULL;
            esp_err_t model_ret = sscma_client_get_model(camera, &model, false);
            if (model_ret == ESP_OK && model != NULL) {
                ESP_LOGI(TAG, "SSCMA model id=%d name=%s", model->id, model->name ? model->name : "(unknown)");
            } else {
                ESP_LOGI(TAG, "SSCMA model query: %s", esp_err_to_name(model_ret));
            }

            ret = sscma_client_invoke(camera, -1, false, true);
            ESP_LOGI(TAG, "SSCMA invoke stream: %s", esp_err_to_name(ret));
        }
    } else {
        set_rgb_state(RGB_STATE_ERROR);
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
