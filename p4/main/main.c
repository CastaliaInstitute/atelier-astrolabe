#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "astrolabe_ui.h"
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

static const char *TAG = "astrolabe_p4";
enum {
  WAVESHARE_ROUND_VISIBLE_SIZE = 720,
  GESTURE_MIN_PX = 80,
};

static lv_obj_t *s_scale_root;
static lv_point_t s_press_point;
static bool s_press_valid;
static volatile int s_requested_face = -1;
static volatile bool s_tour_requested;

static void log_service_status(void) {
  astrolabe_ui_face_t face = astrolabe_ui_current_face();
  astrolabe_p4_network_status_t net = astrolabe_p4_network_status();
  astrolabe_p4_audio_status_t audio = astrolabe_p4_audio_status();
  ESP_LOGI(TAG,
           "qa: face=%d name=%s faces=%d panel=720x720 wifi=%d ip=%s rssi=%d audio_spk=%d audio_mic=%d",
           (int)face, astrolabe_ui_face_name(face), ASTROLABE_UI_FACE_COUNT, net.connected, net.ip, net.rssi,
           audio.speaker_ready, audio.mic_ready);
}

static void force_waveshare_4c_backlight_on(void) {
  const gpio_num_t backlight = GPIO_NUM_26;
  ESP_ERROR_CHECK(gpio_reset_pin(backlight));
  ESP_ERROR_CHECK(gpio_set_direction(backlight, GPIO_MODE_OUTPUT));
  ESP_ERROR_CHECK(gpio_set_level(backlight, 0));
  ESP_LOGI(TAG, "forced Waveshare 4C backlight GPIO%d active-low on", (int)backlight);
}

static void apply_round_panel_scale(void) {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  s_scale_root = lv_obj_create(screen);
  lv_obj_remove_style_all(s_scale_root);
  const lv_coord_t display_w = lv_display_get_horizontal_resolution(NULL);
  const lv_coord_t display_h = lv_display_get_vertical_resolution(NULL);
  const lv_coord_t visible_w = display_w < WAVESHARE_ROUND_VISIBLE_SIZE ? display_w : WAVESHARE_ROUND_VISIBLE_SIZE;
  const lv_coord_t visible_h = display_h < WAVESHARE_ROUND_VISIBLE_SIZE ? display_h : WAVESHARE_ROUND_VISIBLE_SIZE;
  lv_obj_set_size(s_scale_root, visible_w, visible_h);
  lv_obj_center(s_scale_root);
  lv_obj_set_style_bg_opa(s_scale_root, LV_OPA_TRANSP, 0);
  lv_obj_set_scrollbar_mode(s_scale_root, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(s_scale_root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(s_scale_root, LV_OBJ_FLAG_SCROLLABLE);

  const int32_t zoom_x = ((int32_t)visible_w * 256) / ASTROLABE_UI_WIDTH;
  const int32_t zoom_y = ((int32_t)visible_h * 256) / ASTROLABE_UI_HEIGHT;
  const int32_t zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
  ESP_LOGI(TAG, "Astrolabe logical %dx%d scaled to visible %dx%d on %dx%d panel at zoom %ld/256",
           ASTROLABE_UI_WIDTH, ASTROLABE_UI_HEIGHT, (int)visible_w, (int)visible_h, (int)display_w, (int)display_h,
           (long)zoom);
}

static void next_face(void) {
  astrolabe_ui_next_face();
  astrolabe_ui_face_t face = astrolabe_ui_current_face();
  ESP_LOGI(TAG, "face=%d %s", (int)face, astrolabe_ui_face_name(face));
}

static void previous_face(void) {
  astrolabe_ui_previous_face();
  astrolabe_ui_face_t face = astrolabe_ui_current_face();
  ESP_LOGI(TAG, "face=%d %s", (int)face, astrolabe_ui_face_name(face));
}

static void set_face(astrolabe_ui_face_t face) {
  astrolabe_ui_set_face(face);
  face = astrolabe_ui_current_face();
  ESP_LOGI(TAG, "face=%d %s", (int)face, astrolabe_ui_face_name(face));
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
    if (face_cmd != NULL) {
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
    } else if (strstr(line, "next") != NULL) {
      s_requested_face = (int)astrolabe_ui_current_face() + 1;
    } else if (strstr(line, "prev") != NULL) {
      s_requested_face = (int)astrolabe_ui_current_face() - 1;
    } else if (strstr(line, "tour") != NULL) {
      s_tour_requested = true;
    } else if (strstr(line, "status") != NULL) {
      log_service_status();
    } else if (line[0] != '\0') {
      ESP_LOGI(TAG,
               "commands: face N | next | prev | tour | qa status | wifi status/start/scan/forget/set SSID PASS | "
               "audio status/tone HZ MS/mic");
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
    s_press_valid = true;
    return;
  }
  if (code != LV_EVENT_RELEASED || !s_press_valid) {
    return;
  }

  lv_point_t release_point;
  lv_indev_get_point(indev, &release_point);
  s_press_valid = false;

  const int32_t dx = release_point.x - s_press_point.x;
  const int32_t dy = release_point.y - s_press_point.y;
  const int32_t abs_dx = dx < 0 ? -dx : dx;
  const int32_t abs_dy = dy < 0 ? -dy : dy;
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

void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  ESP_ERROR_CHECK_WITHOUT_ABORT(astrolabe_p4_network_init());
  (void)astrolabe_p4_network_start();

  bsp_display_cfg_t cfg = {
      .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
      .rotation = ESP_LV_ADAPTER_ROTATE_0,
      .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
      .touch_flags = {
          .swap_xy = 0,
          .mirror_x = 0,
          .mirror_y = 0,
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
  apply_round_panel_scale();
  ESP_LOGI(TAG, "initializing Astrolabe UI");
  astrolabe_ui_init_in(s_scale_root);
  ESP_LOGI(TAG, "starting Astrolabe render loop");
  ESP_LOGI(TAG, "registering Astrolabe touch handler");
  lv_obj_add_event_cb(s_scale_root, gesture_event_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(s_scale_root, gesture_event_cb, LV_EVENT_RELEASED, NULL);
  ESP_ERROR_CHECK_WITHOUT_ABORT(astrolabe_p4_audio_init());
  xTaskCreate(serial_console_task, "astrolabe_console", 4096, NULL, 5, NULL);
  ESP_LOGI(TAG, "Astrolabe P4 is running");
  log_service_status();

  int64_t last_tick_us = esp_timer_get_time();
  while (true) {
    lv_timer_handler();
    int64_t now_us = esp_timer_get_time();
    if (now_us - last_tick_us >= 1000000) {
      astrolabe_ui_tick((uint32_t)((now_us - last_tick_us) / 1000));
      last_tick_us = now_us;
    }
    if (s_requested_face != -1) {
      int requested = s_requested_face;
      s_requested_face = -1;
      while (requested < 0) {
        requested += ASTROLABE_UI_FACE_COUNT;
      }
      set_face((astrolabe_ui_face_t)(requested % ASTROLABE_UI_FACE_COUNT));
    }
    if (s_tour_requested) {
      s_tour_requested = false;
      ESP_LOGI(TAG, "tour: start faces=%d", ASTROLABE_UI_FACE_COUNT);
      for (int face = 0; face < ASTROLABE_UI_FACE_COUNT; ++face) {
        set_face((astrolabe_ui_face_t)face);
        for (int step = 0; step < 75; ++step) {
          lv_timer_handler();
          astrolabe_ui_tick(16);
          vTaskDelay(pdMS_TO_TICKS(16));
        }
      }
      ESP_LOGI(TAG, "tour: done");
    }
    vTaskDelay(pdMS_TO_TICKS(16));
  }
}
