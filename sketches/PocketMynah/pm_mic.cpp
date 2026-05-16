#include "pm_mic.h"

#include <Wire.h>

#include "driver/i2s.h"
#include "esp_err.h"
#include "es7210.h"

#include "pin_config.h"

#define I2S_CH I2S_NUM_1
#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

static bool g_mic = false;

bool pm_mic_begin() {
  if (g_mic) {
    return true;
  }
  audio_hal_codec_config_t cfg = {
      .adc_input = AUDIO_HAL_ADC_INPUT_ALL,
      .codec_mode = AUDIO_HAL_CODEC_MODE_ENCODE,
      .i2s_iface =
          {
              .mode = AUDIO_HAL_MODE_SLAVE,
              .fmt = AUDIO_HAL_I2S_NORMAL,
              .samples = AUDIO_HAL_16K_SAMPLES,
              .bits = AUDIO_HAL_BIT_LENGTH_16BITS,
          },
  };
  esp_err_t ret = es7210_adc_init(&Wire, &cfg);
  ret = static_cast<esp_err_t>(ret | es7210_adc_config_i2s(cfg.codec_mode, &cfg.i2s_iface));
  ret = static_cast<esp_err_t>(
      ret | es7210_adc_set_gain(
                (es7210_input_mics_t)(ES7210_INPUT_MIC1 | ES7210_INPUT_MIC2),
                (es7210_gain_value_t)GAIN_0DB));
  ret = static_cast<esp_err_t>(
      ret | es7210_adc_set_gain(
                (es7210_input_mics_t)(ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4),
                (es7210_gain_value_t)GAIN_37_5DB));
  ret = static_cast<esp_err_t>(ret | es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START));
  if (ret != ESP_OK) {
    return false;
  }

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = VAD_SAMPLE_RATE_HZ,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_ALL_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
      .chan_mask = (i2s_channel_t)(I2S_TDM_ACTIVE_CH0 | I2S_TDM_ACTIVE_CH1),
  };

  i2s_pin_config_t pin_config = {};
  pin_config.bck_io_num = PIN_ES7210_BCLK;
  pin_config.ws_io_num = PIN_ES7210_LRCK;
  pin_config.data_in_num = PIN_ES7210_DIN;
  pin_config.mck_io_num = PIN_ES7210_MCLK;

  if (i2s_driver_install(I2S_CH, &i2s_config, 0, NULL) != ESP_OK) {
    es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_STOP);
    return false;
  }
  i2s_set_pin(I2S_CH, &pin_config);
  i2s_zero_dma_buffer(I2S_CH);
  g_mic = true;
  return true;
}

void pm_mic_stop() {
  if (!g_mic) {
    return;
  }
  es7210_adc_ctrl_state(AUDIO_HAL_CODEC_MODE_ENCODE, AUDIO_HAL_CTRL_STOP);
  i2s_driver_uninstall(I2S_CH);
  g_mic = false;
}

bool pm_mic_read_frame(int16_t *out, size_t frame_samples, size_t *bytes_read) {
  if (!g_mic || !out || !bytes_read) {
    return false;
  }
  const size_t want = frame_samples * sizeof(int16_t);
  if (i2s_read(I2S_CH, reinterpret_cast<char *>(out), want, bytes_read, portMAX_DELAY) != ESP_OK) {
    return false;
  }
  return *bytes_read == want;
}

size_t pm_mic_frame_samples() { return VAD_BUFFER_LENGTH; }
