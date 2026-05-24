#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs_flash.h"
#include "p4_audio.h"
#include "p4_network.h"
#include "p4_real_ui.h"
#include "p4_screen_http.h"
#include "p4_settings.h"
#include "sdkconfig.h"

static const char *TAG = "astrolabe_p4";
enum {
  GESTURE_MIN_PX = 80,
  TOUR_TTS_FRAME_STEPS = 1,
};

static lv_obj_t *s_scale_root;
static lv_obj_t *s_touch_layer;
static lv_indev_t *s_touch_indev;
static int32_t s_panel_zoom = 256;
static lv_point_t s_press_point;
static lv_point_t s_last_touch_point;
static bool s_press_valid;
static bool s_touch_seen;
static uint32_t s_touch_events;
static volatile int s_requested_face = -1;
static volatile bool s_tour_requested;
static volatile bool s_tour_tts_requested;
static volatile bool s_screen_http_requested;

static void set_face(int face);

static void put_le16(uint8_t *out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xff);
  out[1] = (uint8_t)((value >> 8) & 0xff);
}

static void put_le32(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xff);
  out[1] = (uint8_t)((value >> 8) & 0xff);
  out[2] = (uint8_t)((value >> 16) & 0xff);
  out[3] = (uint8_t)((value >> 24) & 0xff);
}

static void uart_write_cstr(const char *text) { uart_write_bytes(UART_NUM_0, text, strlen(text)); }

static void serial_hex_emit(const uint8_t *bytes, size_t len) {
  static const char hex[] = "0123456789ABCDEF";
  char out[131];
  size_t out_len = 0;
  for (size_t i = 0; i < len; ++i) {
    out[out_len++] = hex[(bytes[i] >> 4) & 0x0f];
    out[out_len++] = hex[bytes[i] & 0x0f];
    if (out_len >= 128) {
      out[out_len++] = '\n';
      uart_write_bytes(UART_NUM_0, out, out_len);
      out_len = 0;
    }
  }
  if (out_len > 0) {
    out[out_len++] = '\n';
    uart_write_bytes(UART_NUM_0, out, out_len);
  }
}

static void serial_dump_screen_bmp_hex(void) {
  const uint16_t *fb = astrolabe_real_ui_framebuffer();
  const int src_w = astrolabe_real_ui_width();
  const int src_h = astrolabe_real_ui_height();
  if (fb == NULL || src_w <= 0 || src_h <= 0) {
    uart_write_cstr("SCREEN_BMP_HEX_ERROR framebuffer unavailable\n");
    return;
  }

  const int scale = 4;
  const int w = src_w / scale;
  const int h = src_h / scale;
  const uint32_t row_stride = (((uint32_t)w * 24u + 31u) / 32u) * 4u;
  const uint32_t pixel_bytes = row_stride * (uint32_t)h;
  const uint32_t file_size = 54u + pixel_bytes;
  uint8_t header[54] = {0};
  uint8_t *row = calloc(1, row_stride);
  if (row == NULL) {
    uart_write_cstr("SCREEN_BMP_HEX_ERROR out of memory\n");
    return;
  }

  header[0] = 'B';
  header[1] = 'M';
  put_le32(&header[2], file_size);
  put_le32(&header[10], 54);
  put_le32(&header[14], 40);
  put_le32(&header[18], (uint32_t)w);
  put_le32(&header[22], (uint32_t)h);
  put_le16(&header[26], 1);
  put_le16(&header[28], 24);
  put_le32(&header[34], pixel_bytes);

  char begin[96];
  snprintf(begin, sizeof(begin), "SCREEN_BMP_HEX_BEGIN %lu %d %d\n", (unsigned long)file_size, w, h);
  uart_write_cstr(begin);
  serial_hex_emit(header, sizeof(header));

  for (int y = h - 1; y >= 0; --y) {
    memset(row, 0, row_stride);
    for (int x = 0; x < w; ++x) {
      const int src_x = x * scale;
      const int src_y = y * scale;
      const uint16_t rgb565 = fb[(src_y * src_w) + src_x];
      const uint8_t r = (uint8_t)(((rgb565 >> 11) & 0x1f) * 255 / 31);
      const uint8_t g = (uint8_t)(((rgb565 >> 5) & 0x3f) * 255 / 63);
      const uint8_t b = (uint8_t)((rgb565 & 0x1f) * 255 / 31);
      row[x * 3 + 0] = b;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = r;
    }
    serial_hex_emit(row, row_stride);
  }
  uart_write_cstr("SCREEN_BMP_HEX_END\n");
  free(row);
}

static void maybe_start_screen_http(void) {
  astrolabe_p4_network_status_t net = astrolabe_p4_network_status();
  if (s_screen_http_requested && net.connected) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(astrolabe_p4_screen_http_start());
    s_screen_http_requested = false;
  }
}

static bool parse_face_token(const char *token, int *face) {
  if (token == NULL || face == NULL) {
    return false;
  }
  while (*token == ' ') {
    ++token;
  }
  if (strncasecmp(token, "face ", 5) == 0) {
    token += 5;
    while (*token == ' ') {
      ++token;
    }
  }
  if (*token >= '0' && *token <= '9') {
    int parsed = atoi(token);
    if (parsed >= 0 && parsed < astrolabe_real_ui_face_count()) {
      *face = parsed;
      return true;
    }
    return false;
  }
  int parsed = astrolabe_real_ui_parse_face_name(token);
  if (parsed >= 0) {
    *face = parsed;
    return true;
  }
  return false;
}

static void log_service_status(void) {
  int face = astrolabe_real_ui_current_face();
  int home = astrolabe_p4_settings_home_face();
  astrolabe_p4_network_status_t net = astrolabe_p4_network_status();
  astrolabe_p4_audio_status_t audio = astrolabe_p4_audio_status();
  const lv_coord_t panel_w = lv_display_get_horizontal_resolution(NULL);
  const lv_coord_t panel_h = lv_display_get_vertical_resolution(NULL);
  ESP_LOGI(TAG,
           "qa: profile=%s face=%d name=%s home=%d home_name=%s faces=%d panel=%dx%d touch=%d touch_max=%d "
           "touch_events=%lu touch_last=%d,%d wifi=%d ip=%s rssi=%d audio_spk=%d audio_mic=%d",
           astrolabe_p4_settings_profile(), face, astrolabe_real_ui_face_name(face), home,
           astrolabe_real_ui_face_name(home), astrolabe_real_ui_face_count(), (int)panel_w, (int)panel_h,
           s_touch_indev != NULL, CONFIG_ESP_LCD_TOUCH_MAX_POINTS, (unsigned long)s_touch_events,
           s_touch_seen ? (int)s_last_touch_point.x : -1, s_touch_seen ? (int)s_last_touch_point.y : -1,
           net.connected, net.ip, net.rssi, audio.speaker_ready, audio.mic_ready);
}

static void force_waveshare_4c_backlight_on(void) {
  const gpio_num_t backlight = GPIO_NUM_26;
  ESP_ERROR_CHECK(gpio_reset_pin(backlight));
  ESP_ERROR_CHECK(gpio_set_direction(backlight, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(backlight, 0));
  ESP_LOGI(TAG, "forced Waveshare 4C backlight GPIO%d active-low on", (int)backlight);
}

static void apply_display_fit(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  s_scale_root = lv_obj_create(screen);
  lv_obj_remove_style_all(s_scale_root);
  lv_coord_t display_w = lv_display_get_horizontal_resolution(NULL);
  lv_coord_t display_h = lv_display_get_vertical_resolution(NULL);
  if (display_w <= 0) {
    display_w = ASTROLABE_REAL_UI_WIDTH;
  }
  if (display_h <= 0) {
    display_h = ASTROLABE_REAL_UI_HEIGHT;
  }
  lv_obj_set_size(s_scale_root, ASTROLABE_REAL_UI_WIDTH, ASTROLABE_REAL_UI_HEIGHT);
  s_panel_zoom = ((int32_t)display_w * 256) / ASTROLABE_REAL_UI_WIDTH;
  const int32_t zoom_y = ((int32_t)display_h * 256) / ASTROLABE_REAL_UI_HEIGHT;
  if (zoom_y < s_panel_zoom) {
    s_panel_zoom = zoom_y;
  }
  if (s_panel_zoom <= 0) {
    s_panel_zoom = 256;
  }
  const int32_t scaled_w = (ASTROLABE_REAL_UI_WIDTH * s_panel_zoom) / 256;
  const int32_t scaled_h = (ASTROLABE_REAL_UI_HEIGHT * s_panel_zoom) / 256;
  const int32_t extra_w = scaled_w > ASTROLABE_REAL_UI_WIDTH ? scaled_w - ASTROLABE_REAL_UI_WIDTH : 0;
  const int32_t extra_h = scaled_h > ASTROLABE_REAL_UI_HEIGHT ? scaled_h - ASTROLABE_REAL_UI_HEIGHT : 0;
  lv_obj_set_pos(s_scale_root, ((int32_t)display_w - scaled_w) / 2, ((int32_t)display_h - scaled_h) / 2);
  lv_obj_set_style_bg_opa(s_scale_root, LV_OPA_TRANSP, 0);
  lv_obj_set_style_transform_pivot_x(s_scale_root, 0, 0);
  lv_obj_set_style_transform_pivot_y(s_scale_root, 0, 0);
  lv_obj_set_style_transform_scale(s_scale_root, s_panel_zoom, 0);
  lv_obj_set_style_transform_width(s_scale_root, extra_w, 0);
  lv_obj_set_style_transform_height(s_scale_root, extra_h, 0);
  lv_obj_set_scrollbar_mode(s_scale_root, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_scale_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_scale_root, LV_OBJ_FLAG_SCROLLABLE);

  ESP_LOGI(TAG, "Astrolabe logical %dx%d fit to %dx%d panel at zoom %ld/256", ASTROLABE_REAL_UI_WIDTH,
           ASTROLABE_REAL_UI_HEIGHT, (int)display_w, (int)display_h, (long)s_panel_zoom);
}

static void next_face(void) {
  astrolabe_real_ui_cycle(1);
  ESP_LOGI(TAG, "face=%d %s", astrolabe_real_ui_current_face(),
           astrolabe_real_ui_face_name(astrolabe_real_ui_current_face()));
}

static void previous_face(void) {
  astrolabe_real_ui_cycle(-1);
  ESP_LOGI(TAG, "face=%d %s", astrolabe_real_ui_current_face(),
           astrolabe_real_ui_face_name(astrolabe_real_ui_current_face()));
}

static void set_face(int face) {
  astrolabe_real_ui_set_face(face);
  face = astrolabe_real_ui_current_face();
  ESP_LOGI(TAG, "face=%d %s", face, astrolabe_real_ui_face_name(face));
}

static void serial_console_task(void *arg) {
  (void)arg;
  char line[192];
  size_t line_len = 0;
  esp_err_t uart_ret = uart_driver_install(UART_NUM_0, 2048, 0, 0, NULL, 0);
  if (uart_ret != ESP_OK && uart_ret != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(TAG, "UART command driver install failed: %s", esp_err_to_name(uart_ret));
  }

  while (true) {
    uint8_t ch;
    int got = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(100));
    if (got <= 0) {
      continue;
    }
    if (ch != '\n' && ch != '\r') {
      if (line_len + 1 < sizeof(line)) {
        line[line_len++] = (char)ch;
      }
      continue;
    }
    if (line_len == 0) {
      continue;
    }
    line[line_len] = '\0';
    line_len = 0;

    char *face_cmd = strstr(line, "face ");
    char *wifi_set_cmd = strstr(line, "wifi set ");
    char *audio_tone_cmd = strstr(line, "audio tone");
    char *settings_home_cmd = strstr(line, "settings home ");
    if (settings_home_cmd != NULL) {
      char *value = settings_home_cmd + strlen("settings home ");
      int home_face = ASTROLABE_REAL_UI_FACE_MOON;
      if (strcasecmp(value, "current") == 0) {
        home_face = astrolabe_real_ui_current_face();
      }
      if (strcasecmp(value, "current") == 0 || parse_face_token(value, &home_face)) {
        if (astrolabe_p4_settings_set_home_face(home_face)) {
          if (bsp_display_lock(100) == ESP_OK) {
            set_face(home_face);
            bsp_display_unlock();
          }
          log_service_status();
        }
      } else {
        ESP_LOGW(TAG, "settings home expects moon | classic | face N | current");
      }
    } else if (face_cmd != NULL) {
      s_requested_face = atoi(face_cmd + 5);
    } else if (wifi_set_cmd != NULL) {
      char *ssid = wifi_set_cmd + strlen("wifi set ");
      while (*ssid == ' ') {
        ++ssid;
      }
      char *pass = strchr(ssid, ' ');
      if (pass != NULL) {
        *pass++ = '\0';
        while (*pass == ' ') {
          ++pass;
        }
      }
      if (astrolabe_p4_network_save_credentials(ssid, pass ? pass : "")) {
        ESP_LOGI(TAG, "wifi credentials accepted");
      }
    } else if (strstr(line, "wifi start") != NULL) {
      (void)astrolabe_p4_network_start();
    } else if (strstr(line, "wifi stop") != NULL) {
      (void)astrolabe_p4_network_stop();
    } else if (strstr(line, "wifi forget") != NULL) {
      (void)astrolabe_p4_network_forget_credentials();
    } else if (strstr(line, "wifi scan") != NULL) {
      astrolabe_p4_network_scan();
    } else if (strstr(line, "wifi status") != NULL) {
      astrolabe_p4_network_log_status();
    } else if (audio_tone_cmd != NULL) {
      int hz = 660;
      int ms = 350;
      (void)sscanf(audio_tone_cmd, "audio tone %d %d", &hz, &ms);
      (void)astrolabe_p4_audio_play_tone(hz, ms);
    } else if (strstr(line, "audio mic") != NULL) {
      (void)astrolabe_p4_audio_probe_mic();
    } else if (strstr(line, "audio status") != NULL) {
      astrolabe_p4_audio_log_status();
    } else if (strstr(line, "settings status") != NULL) {
      astrolabe_p4_settings_log_status();
    } else if (strstr(line, "home") != NULL) {
      if (bsp_display_lock(100) == ESP_OK) {
        set_face(astrolabe_p4_settings_home_face());
        bsp_display_unlock();
      }
    } else if (strstr(line, "next") != NULL) {
      s_requested_face = astrolabe_real_ui_current_face() + 1;
    } else if (strstr(line, "prev") != NULL) {
      s_requested_face = astrolabe_real_ui_current_face() - 1;
    } else if (strstr(line, "tour tts") != NULL || strstr(line, "tour voice") != NULL) {
      s_tour_tts_requested = true;
    } else if (strstr(line, "tour") != NULL) {
      s_tour_requested = true;
    } else if (strstr(line, "screen http") != NULL) {
      s_screen_http_requested = true;
    } else if (strstr(line, "screenshot") != NULL || strstr(line, "screen hex") != NULL) {
      serial_dump_screen_bmp_hex();
    } else if (strstr(line, "status") != NULL) {
      log_service_status();
    } else if (line[0] != '\0') {
      ESP_LOGI(TAG,
               "commands: face N | home | settings status/home moon|classic|face N|current | next | prev | "
               "tour/tour tts | screenshot/screen http | qa status | wifi status/start/stop/scan/forget/set SSID "
               "PASS | audio status/tone HZ MS/mic");
    }
  }
}

static void gesture_event_cb(lv_event_t *event) {
  lv_indev_t *indev = lv_indev_active();
  if (indev == NULL) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED) {
    lv_indev_get_point(indev, &s_press_point);
    s_last_touch_point = s_press_point;
    s_press_valid = true;
    s_touch_seen = true;
    s_touch_events++;
    ESP_LOGI(TAG, "touch down x=%d y=%d", (int)s_press_point.x, (int)s_press_point.y);
    return;
  }
  if (code != LV_EVENT_RELEASED || !s_press_valid) {
    return;
  }

  lv_point_t release_point;
  lv_indev_get_point(indev, &release_point);
  s_last_touch_point = release_point;
  s_press_valid = false;
  s_touch_events++;

  const int32_t dx = release_point.x - s_press_point.x;
  const int32_t dy = release_point.y - s_press_point.y;
  const int32_t abs_dx = dx < 0 ? -dx : dx;
  const int32_t abs_dy = dy < 0 ? -dy : dy;
  ESP_LOGI(TAG, "touch up x=%d y=%d dx=%ld dy=%ld", (int)release_point.x, (int)release_point.y, (long)dx,
           (long)dy);
  if (abs_dx >= GESTURE_MIN_PX && abs_dx > abs_dy) {
    if (dx < 0) {
      next_face();
    } else {
      previous_face();
    }
    return;
  }

  if (abs_dx < GESTURE_MIN_PX / 2 && abs_dy < GESTURE_MIN_PX / 2) {
    next_face();
  }
}

static void register_touch_layer(lv_display_t *display) {
  s_touch_indev = bsp_display_get_input_dev();
  if (s_touch_indev == NULL) {
    ESP_LOGW(TAG, "Waveshare touch input was not registered by BSP");
    return;
  }
  lv_indev_set_display(s_touch_indev, display);

  const lv_coord_t display_w = lv_display_get_horizontal_resolution(display);
  const lv_coord_t display_h = lv_display_get_vertical_resolution(display);
  s_touch_layer = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(s_touch_layer);
  lv_obj_set_pos(s_touch_layer, 0, 0);
  lv_obj_set_size(s_touch_layer, display_w, display_h);
  lv_obj_set_style_bg_opa(s_touch_layer, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(s_touch_layer, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_touch_layer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_touch_layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_touch_layer, gesture_event_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(s_touch_layer, gesture_event_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_move_foreground(s_touch_layer);

  ESP_LOGI(TAG, "touch input ready: indev=%p layer=%dx%d max_points=%d", (void *)s_touch_indev, (int)display_w,
           (int)display_h, CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
}

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK_WITHOUT_ABORT(astrolabe_p4_network_init());
  (void)astrolabe_p4_network_start();
  esp_err_t sd_ret = bsp_sdcard_mount();
  if (sd_ret == ESP_OK) {
    ESP_LOGI(TAG, "mounted SD card at %s", BSP_SD_MOUNT_POINT);
  } else {
    ESP_LOGW(TAG, "SD card mount skipped/failed: %s", esp_err_to_name(sd_ret));
  }

  bsp_display_cfg_t cfg = {
      .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
      .rotation = ESP_LV_ADAPTER_ROTATE_180,
      .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
      .touch_flags = {
          .swap_xy = 0,
          .mirror_x = 1,
          .mirror_y = 1,
      },
  };
  lv_display_t *display = bsp_display_start_with_config(&cfg);
  if (display == NULL) {
    ESP_LOGE(TAG, "failed to start Waveshare display");
    abort();
  }
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(bsp_display_get_panel_handle(), true));
  bsp_display_backlight_on();
  force_waveshare_4c_backlight_on();

  if (bsp_display_lock(-1) != ESP_OK) {
    ESP_LOGE(TAG, "failed to acquire LVGL lock for startup");
    abort();
  }
  apply_display_fit();
  ESP_LOGI(TAG, "initializing Astrolabe UI");
  astrolabe_real_ui_init_in(s_scale_root);
  astrolabe_p4_settings_log_status();
  ESP_LOGI(TAG, "applying configured home face");
  set_face(astrolabe_p4_settings_home_face());
  ESP_LOGI(TAG, "starting Astrolabe render loop");
  ESP_LOGI(TAG, "registering Astrolabe touch layer");
  register_touch_layer(display);
  bsp_display_unlock();
  ESP_ERROR_CHECK_WITHOUT_ABORT(astrolabe_p4_audio_init());
  xTaskCreate(serial_console_task, "astrolabe_console", 4096, NULL, 5, NULL);
  ESP_LOGI(TAG, "Astrolabe P4 is running");
  log_service_status();
  maybe_start_screen_http();

  int64_t last_tick_us = esp_timer_get_time();
  while (true) {
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_tick_us >= 1000000) {
      if (bsp_display_lock(100) == ESP_OK) {
        astrolabe_real_ui_tick((uint32_t)((now_us - last_tick_us) / 1000));
        bsp_display_unlock();
      }
      last_tick_us = now_us;
      maybe_start_screen_http();
    }
    if (s_requested_face != -1) {
      int requested = s_requested_face;
      s_requested_face = -1;
      while (requested < 0) {
        requested += astrolabe_real_ui_face_count();
      }
      if (bsp_display_lock(100) == ESP_OK) {
        set_face(requested % astrolabe_real_ui_face_count());
        bsp_display_unlock();
      } else {
        ESP_LOGW(TAG, "skipped face request because LVGL lock was busy");
      }
    }
    if (s_tour_requested) {
      s_tour_requested = false;
      ESP_LOGI(TAG, "tour: start faces=%d", astrolabe_real_ui_face_count());
      for (int face = 0; face < astrolabe_real_ui_face_count(); ++face) {
        if (bsp_display_lock(100) == ESP_OK) {
          set_face(face);
          bsp_display_unlock();
        }
        for (int step = 0; step < 75; ++step) {
          if (bsp_display_lock(100) == ESP_OK) {
            astrolabe_real_ui_tick(16);
            bsp_display_unlock();
          }
          vTaskDelay(pdMS_TO_TICKS(16));
        }
      }
      ESP_LOGI(TAG, "tour: done");
    }
    if (s_tour_tts_requested) {
      s_tour_tts_requested = false;
      ESP_LOGI(TAG, "tour tts: start faces=%d", astrolabe_real_ui_face_count());
      for (int face = 0; face < astrolabe_real_ui_face_count(); ++face) {
        if (bsp_display_lock(100) == ESP_OK) {
          set_face(face);
          bsp_display_unlock();
        }
        ESP_LOGI(TAG, "tour tts: face=%d name=%s", face, astrolabe_real_ui_face_name(face));
        for (int step = 0; step < TOUR_TTS_FRAME_STEPS; ++step) {
          if (bsp_display_lock(100) == ESP_OK) {
            astrolabe_real_ui_tick(16);
            bsp_display_unlock();
          }
          vTaskDelay(pdMS_TO_TICKS(16));
        }
      }
      ESP_LOGI(TAG, "tour tts: done");
    }
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}
