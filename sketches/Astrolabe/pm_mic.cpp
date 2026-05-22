#include "pm_mic.h"

#include <Wire.h>
#include <string.h>

#include "driver/i2s.h"
#include "esp_err.h"
#include "es7210.h"

#include "pin_config.h"

#define I2S_CH I2S_NUM_1
#define PM_MIC_I2S_CHANNELS 4
#define PM_MIC_CAPTURE_CHANNELS 2
#define VAD_SAMPLE_RATE_HZ 16000
#define VAD_FRAME_LENGTH_MS 30
#define VAD_BUFFER_LENGTH (VAD_FRAME_LENGTH_MS * VAD_SAMPLE_RATE_HZ / 1000)

static bool g_mic = false;

int pm_mic_i2s_channels() { return PM_MIC_I2S_CHANNELS; }

int pm_mic_capture_channels() { return PM_MIC_CAPTURE_CHANNELS; }

int pm_mic_capture_slot(int capture_channel) {
  switch (capture_channel) {
    case 0:
      return 0;
    case 1:
      return 2;
    default:
      return -1;
  }
}

static esp_err_t es7210_write_reg_direct(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(ES7210_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0 ? ESP_OK : ESP_FAIL;
}

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
                (es7210_gain_value_t)GAIN_24DB));
  ret = static_cast<esp_err_t>(
      ret | es7210_adc_set_gain(
                (es7210_input_mics_t)(ES7210_INPUT_MIC3 | ES7210_INPUT_MIC4),
                (es7210_gain_value_t)GAIN_37_5DB));
  ret = static_cast<esp_err_t>(ret | es7210_write_reg_direct(ES7210_MODE_CONFIG_REG08, 0x20));
  ret = static_cast<esp_err_t>(ret | es7210_write_reg_direct(ES7210_SDP_INTERFACE2_REG12, 0x02));
  ret = static_cast<esp_err_t>(ret | es7210_adc_ctrl_state(cfg.codec_mode, AUDIO_HAL_CTRL_START));
  if (ret != ESP_OK) {
    return false;
  }

  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = VAD_SAMPLE_RATE_HZ,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_MULTIPLE,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 8,
      .dma_buf_len = 64,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
      .mclk_multiple = I2S_MCLK_MULTIPLE_256,
      .bits_per_chan = I2S_BITS_PER_CHAN_16BIT,
      .chan_mask = (i2s_channel_t)(I2S_TDM_ACTIVE_CH0 | I2S_TDM_ACTIVE_CH1 |
                                   I2S_TDM_ACTIVE_CH2 | I2S_TDM_ACTIVE_CH3),
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
  const size_t want =
      frame_samples * static_cast<size_t>(PM_MIC_I2S_CHANNELS) * sizeof(int16_t);
  if (i2s_read(I2S_CH, reinterpret_cast<char *>(out), want, bytes_read, portMAX_DELAY) != ESP_OK) {
    return false;
  }
  return *bytes_read == want;
}

void pm_mic_pick_channel(const int16_t *interleaved, size_t frame_samples, int channel,
                         int16_t *mono) {
  if (!interleaved || !mono || channel < 0 || channel >= PM_MIC_I2S_CHANNELS) {
    return;
  }
  for (size_t i = 0; i < frame_samples; ++i) {
    mono[i] = interleaved[i * static_cast<size_t>(PM_MIC_I2S_CHANNELS) + static_cast<size_t>(channel)];
  }
}

void pm_mic_pick_capture_channel(const int16_t *interleaved, size_t frame_samples, int capture_channel,
                                 int16_t *mono) {
  const int slot = pm_mic_capture_slot(capture_channel);
  if (slot < 0) {
    if (mono) {
      memset(mono, 0, frame_samples * sizeof(int16_t));
    }
    return;
  }
  pm_mic_pick_channel(interleaved, frame_samples, slot, mono);
}

bool pm_mic_mix_capture_channels(const int16_t *interleaved, size_t frame_samples, int16_t *mono) {
  if (!interleaved || !mono || frame_samples == 0) {
    return false;
  }
  const int capture_ch = pm_mic_capture_channels();
  const int nch = pm_mic_i2s_channels();
  int slots[PM_MIC_CAPTURE_CHANNELS] = {};
  int slot_count = 0;
  for (int ch = 0; ch < capture_ch && slot_count < PM_MIC_CAPTURE_CHANNELS; ++ch) {
    const int slot = pm_mic_capture_slot(ch);
    if (slot >= 0 && slot < nch) {
      slots[slot_count++] = slot;
    }
  }
  if (slot_count <= 0) {
    memset(mono, 0, frame_samples * sizeof(int16_t));
    return false;
  }
  if (slot_count == 1) {
    pm_mic_pick_channel(interleaved, frame_samples, slots[0], mono);
    return true;
  }

  for (size_t i = 0; i < frame_samples; ++i) {
    int32_t sum = 0;
    for (int s = 0; s < slot_count; ++s) {
      sum += interleaved[i * static_cast<size_t>(nch) + static_cast<size_t>(slots[s])];
    }
    mono[i] = static_cast<int16_t>(sum / slot_count);
  }
  return true;
}

bool pm_mic_repair_sparse_mono(int16_t *mono, size_t frame_samples) {
  if (!mono || frame_samples < 4) {
    return false;
  }

  uint32_t nonzero[2] = {};
  uint64_t sum_abs[2] = {};
  for (size_t i = 0; i < frame_samples; ++i) {
    const int32_t v = mono[i];
    const uint32_t a = static_cast<uint32_t>(v < 0 ? -v : v);
    if (a > 0) {
      nonzero[i & 1u]++;
      sum_abs[i & 1u] += a;
    }
  }

  const size_t half = frame_samples / 2;
  int active = -1;
  int sparse = -1;
  if (nonzero[0] > half / 4 && nonzero[1] <= half / 16 && sum_abs[0] > sum_abs[1] * 8u) {
    active = 0;
    sparse = 1;
  } else if (nonzero[1] > half / 4 && nonzero[0] <= half / 16 && sum_abs[1] > sum_abs[0] * 8u) {
    active = 1;
    sparse = 0;
  } else {
    return false;
  }

  for (size_t i = static_cast<size_t>(sparse); i < frame_samples; i += 2) {
    int32_t prev = 0;
    int32_t next = 0;
    bool have_prev = false;
    bool have_next = false;
    if (i > 0 && ((i - 1u) & 1u) == static_cast<size_t>(active)) {
      prev = mono[i - 1u];
      have_prev = true;
    }
    if (i + 1u < frame_samples && ((i + 1u) & 1u) == static_cast<size_t>(active)) {
      next = mono[i + 1u];
      have_next = true;
    }
    if (have_prev && have_next) {
      mono[i] = static_cast<int16_t>((prev + next) / 2);
    } else if (have_prev) {
      mono[i] = static_cast<int16_t>(prev);
    } else if (have_next) {
      mono[i] = static_cast<int16_t>(next);
    }
  }
  return true;
}

size_t pm_mic_frame_samples() { return VAD_BUFFER_LENGTH; }
