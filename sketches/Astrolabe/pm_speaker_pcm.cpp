#include "pm_speaker_pcm.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern "C" {
#include "es8311.h"
}

#include "pin_config.h"
#include "faces/pm_faces.h"
#include "pm_audio_analyzer.h"
#include "pm_mic.h"

static const char *TAG = "pm_speaker_pcm";

#define I2S_TX I2S_NUM_0

static es8311_handle_t s_es = nullptr;
static bool s_es_inited = false;
static bool s_pcm_active = false;
static int s_sample_hz = 0;
static int s_channels = 0;
static SemaphoreHandle_t s_pcm_mux = nullptr;
static int s_volume = 70;
static bool s_muted = false;

static esp_err_t es8311_board_init(int sample_hz) {
  if (!s_es) {
    s_es = es8311_create(I2C_NUM_0, ES8311_ADDRESS_0);
    ESP_RETURN_ON_FALSE(s_es, ESP_FAIL, TAG, "es8311_create");
  }
  es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = sample_hz * 256,
      .sample_frequency = sample_hz,
  };
  if (!s_es_inited) {
    ESP_RETURN_ON_ERROR(es8311_init(s_es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "init");
    ESP_RETURN_ON_ERROR(
        es8311_sample_frequency_config(s_es, clk.mclk_frequency, clk.sample_frequency), TAG, "sf");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_es, false), TAG, "mic off");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es, s_volume, nullptr), TAG, "vol");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(s_es, ES8311_MIC_GAIN_6DB), TAG, "mg");
    ESP_RETURN_ON_ERROR(gpio_set_direction((gpio_num_t)PA, GPIO_MODE_OUTPUT), TAG, "pa dir");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)PA, 1), TAG, "pa on");
    s_es_inited = true;
  } else {
    ESP_RETURN_ON_ERROR(
        es8311_sample_frequency_config(s_es, clk.mclk_frequency, clk.sample_frequency), TAG, "sf2");
  }
  return ESP_OK;
}

static void i2s_tx_stop() {
  i2s_driver_uninstall(I2S_TX);
}

static esp_err_t i2s_tx_begin(int sample_hz, int channels) {
  i2s_tx_stop();
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate = (uint32_t)sample_hz;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = (channels == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT;
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 8;
  c.dma_buf_len = 256;
  c.use_apll = false;
  c.tx_desc_auto_clear = true;
  c.fixed_mclk = 0;
  c.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  c.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  ESP_RETURN_ON_ERROR(i2s_driver_install(I2S_TX, &c, 0, NULL), TAG, "i2s install");

  i2s_pin_config_t pin = {};
  pin.bck_io_num = PIN_ES7210_BCLK;
  pin.ws_io_num = PIN_ES7210_LRCK;
  pin.data_out_num = PIN_ES8311_DOUT;
  pin.mck_io_num = PIN_ES7210_MCLK;
  ESP_RETURN_ON_ERROR(i2s_set_pin(I2S_TX, &pin), TAG, "i2s pins");
  i2s_zero_dma_buffer(I2S_TX);
  return ESP_OK;
}

static esp_err_t i2s_write_all(const int16_t *pcm, size_t total_s16) {
  if (pm_faces_current() == ClockFace::Spectrum) {
    const int channels = s_channels > 0 ? s_channels : 1;
    pm_audio_analyzer_feed_out(pcm, total_s16, channels);
  }
  const uint8_t *p = reinterpret_cast<const uint8_t *>(pcm);
  size_t remain = total_s16 * sizeof(int16_t);
  while (remain > 0) {
    size_t wrote = 0;
    if (i2s_write(I2S_TX, p, remain, &wrote, portMAX_DELAY) != ESP_OK) {
      return ESP_FAIL;
    }
    p += wrote;
    remain -= wrote;
    vTaskDelay(1);
  }
  return ESP_OK;
}

static void pcm_mux_ensure() {
  if (!s_pcm_mux) {
    s_pcm_mux = xSemaphoreCreateMutex();
  }
}

esp_err_t pm_speaker_pcm_begin(int sample_hz, int channels) {
  pcm_mux_ensure();
  if (xSemaphoreTake(s_pcm_mux, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }
  pm_mic_stop();
  esp_err_t err = es8311_board_init(sample_hz);
  if (err == ESP_OK) {
    const int i2s_ch = (channels == 1) ? 2 : channels;
    err = i2s_tx_begin(sample_hz, i2s_ch);
  }
  if (err == ESP_OK) {
    s_sample_hz = sample_hz;
    s_channels = channels;
    s_pcm_active = true;
    ESP_LOGI(TAG, "PCM path %d Hz x %d ch", sample_hz, channels);
  }
  xSemaphoreGive(s_pcm_mux);
  return err;
}

esp_err_t pm_speaker_pcm_write(const int16_t *pcm, size_t num_s16) {
  if (!s_pcm_active || !pcm || num_s16 == 0 || s_muted) {
    return ESP_OK;
  }
  return i2s_write_all(pcm, num_s16);
}

void pm_speaker_pcm_end(void) {
  if (!s_pcm_active) {
    return;
  }
  pcm_mux_ensure();
  if (xSemaphoreTake(s_pcm_mux, pdMS_TO_TICKS(2000)) != pdTRUE) {
    return;
  }
  static int16_t silence[512 * 2];
  memset(silence, 0, sizeof(silence));
  const int i2s_ch = (s_channels == 1) ? 2 : s_channels;
  for (int i = 0; i < 4; ++i) {
    (void)i2s_write_all(silence, 512u * static_cast<size_t>(i2s_ch));
  }
  i2s_stop(I2S_TX);
  vTaskDelay(pdMS_TO_TICKS(20));
  i2s_tx_stop();
  s_pcm_active = false;
  xSemaphoreGive(s_pcm_mux);
}

void pm_speaker_pcm_set_volume(int volume) {
  if (volume < 0) {
    volume = 0;
  }
  if (volume > 100) {
    volume = 100;
  }
  s_volume = volume;
  if (s_es) {
    (void)es8311_voice_volume_set(s_es, s_volume, nullptr);
  }
}

void pm_speaker_pcm_set_mute(bool mute) {
  s_muted = mute;
}

bool pm_speaker_pcm_active(void) {
  return s_pcm_active;
}
