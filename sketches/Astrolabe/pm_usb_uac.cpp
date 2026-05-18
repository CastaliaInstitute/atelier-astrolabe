#include "pm_usb_uac.h"

#include "esp_log.h"
#include "pm_speaker_pcm.h"
#include "sdkconfig.h"

#if CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && !defined(ASTROLABE_QEMU) && __has_include("usb_device_uac.h")

#include "usb_device_uac.h"

static const char *TAG = "pm_usb_uac";

#ifndef ASTROLABE_UAC_SAMPLE_HZ
#define ASTROLABE_UAC_SAMPLE_HZ 48000
#endif

static bool s_uac_ready = false;
static bool s_pcm_open = false;

static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *arg) {
  (void)arg;
  if (!buf || len == 0) {
    return ESP_OK;
  }
  if (!s_pcm_open) {
    if (pm_speaker_pcm_begin(ASTROLABE_UAC_SAMPLE_HZ, 2) != ESP_OK) {
      return ESP_FAIL;
    }
    s_pcm_open = true;
  }
  const size_t num_s16 = len / sizeof(int16_t);
  return pm_speaker_pcm_write(reinterpret_cast<const int16_t *>(buf), num_s16);
}

static void uac_set_mute_cb(uint32_t mute, void *arg) {
  (void)arg;
  pm_speaker_pcm_set_mute(mute != 0);
  ESP_LOGI(TAG, "mute=%lu", static_cast<unsigned long>(mute));
}

static void uac_set_volume_cb(uint32_t volume, void *arg) {
  (void)arg;
  pm_speaker_pcm_set_volume(static_cast<int>(volume));
  ESP_LOGI(TAG, "volume=%lu", static_cast<unsigned long>(volume));
}

bool pm_usb_uac_begin(void) {
  if (s_uac_ready) {
    return true;
  }

  uac_device_config_t cfg = {};
  cfg.skip_tinyusb_init = false;
  cfg.output_cb = uac_output_cb;
  cfg.input_cb = nullptr;
  cfg.set_mute_cb = uac_set_mute_cb;
  cfg.set_volume_cb = uac_set_volume_cb;
  cfg.cb_ctx = nullptr;

  if (uac_device_init(&cfg) != ESP_OK) {
    ESP_LOGE(TAG, "uac_device_init failed");
    return false;
  }
  s_uac_ready = true;
  ESP_LOGI(TAG, "USB UAC speaker @ %d Hz", ASTROLABE_UAC_SAMPLE_HZ);
  return true;
}

bool pm_usb_uac_ready(void) {
  return s_uac_ready;
}

#elif CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && !defined(ASTROLABE_QEMU)

static const char *TAG = "pm_usb_uac";

bool pm_usb_uac_begin(void) {
  ESP_LOGW(TAG, "UAC enabled but usb_device_uac not available (build waveshare_s3_175_uac, run hybrid once)");
  return false;
}

bool pm_usb_uac_ready(void) {
  return false;
}

#else

bool pm_usb_uac_begin(void) {
  return false;
}

bool pm_usb_uac_ready(void) {
  return false;
}

#endif
