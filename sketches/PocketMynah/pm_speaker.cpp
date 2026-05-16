#include "pm_speaker.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "minimp3_ex.h"

extern "C" {
#include "es8311.h"
}

#include "pin_config.h"

static const char *TAG = "pm_speaker";

#define I2S_TX I2S_NUM_0

static es8311_handle_t s_es = nullptr;
static bool s_es_inited = false;

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
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es, 85, nullptr), TAG, "vol");
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
  const uint8_t *p = reinterpret_cast<const uint8_t *>(pcm);
  size_t remain = total_s16 * sizeof(int16_t);
  while (remain > 0) {
    size_t wrote = 0;
    if (i2s_write(I2S_TX, p, remain, &wrote, portMAX_DELAY) != ESP_OK) {
      return ESP_FAIL;
    }
    p += wrote;
    remain -= wrote;
  }
  return ESP_OK;
}

bool pm_speaker_play_mp3(const uint8_t *mp3, size_t mp3_len) {
  if (!mp3 || mp3_len == 0) {
    return false;
  }
  mp3dec_t dec = {};
  mp3dec_init(&dec);
  mp3dec_file_info_t fi = {};
  const int ld = mp3dec_load_buf(&dec, mp3, mp3_len, &fi, nullptr, nullptr);
  if (ld != 0 || !fi.buffer || fi.samples == 0) {
    free(fi.buffer);
    ESP_LOGW(TAG, "mp3dec_load_buf ret=%d samples=%zu", ld, (size_t)fi.samples);
    return false;
  }

  if (es8311_board_init(fi.hz) != ESP_OK) {
    free(fi.buffer);
    return false;
  }
  const int out_ch = (fi.channels == 1) ? 2 : fi.channels;
  if (i2s_tx_begin(fi.hz, out_ch) != ESP_OK) {
    free(fi.buffer);
    return false;
  }

  if (fi.channels == 1) {
    const size_t n = fi.samples;
    int16_t *st = static_cast<int16_t *>(
        heap_caps_malloc(n * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!st) {
      st = static_cast<int16_t *>(malloc(n * 2 * sizeof(int16_t)));
    }
    if (!st) {
      free(fi.buffer);
      return false;
    }
    for (size_t i = 0; i < n; i++) {
      const int16_t s = fi.buffer[i];
      st[2 * i] = s;
      st[2 * i + 1] = s;
    }
    const esp_err_t w = i2s_write_all(st, n * 2);
    free(st);
    free(fi.buffer);
    i2s_tx_stop();
    return w == ESP_OK;
  }

  const esp_err_t w = i2s_write_all(fi.buffer, fi.samples);
  free(fi.buffer);
  i2s_tx_stop();
  return w == ESP_OK;
}
