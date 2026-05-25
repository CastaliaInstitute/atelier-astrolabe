#include "AstrolabeSpeakerAudioSink.h"

#include <algorithm>
#include <cstring>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "astrolabe_cspot_audio";

#define ASTROLABE_CSPOT_I2S I2S_NUM_0
#define ASTROLABE_CSPOT_I2C I2C_NUM_0

#if __has_include("pin_config.h")
#include "pin_config.h"
#endif

#ifndef ASTROLABE_CSPOT_I2C_SDA
#ifdef IIC_SDA
#define ASTROLABE_CSPOT_I2C_SDA IIC_SDA
#else
#define ASTROLABE_CSPOT_I2C_SDA 11
#endif
#endif
#ifndef ASTROLABE_CSPOT_I2C_SCL
#ifdef IIC_SCL
#define ASTROLABE_CSPOT_I2C_SCL IIC_SCL
#else
#define ASTROLABE_CSPOT_I2C_SCL 10
#endif
#endif
#ifndef ASTROLABE_CSPOT_I2S_BCLK
#ifdef PIN_I2S_BCLK
#define ASTROLABE_CSPOT_I2S_BCLK PIN_I2S_BCLK
#else
#define ASTROLABE_CSPOT_I2S_BCLK 48
#endif
#endif
#ifndef ASTROLABE_CSPOT_I2S_LRCK
#ifdef PIN_I2S_LRCK
#define ASTROLABE_CSPOT_I2S_LRCK PIN_I2S_LRCK
#else
#define ASTROLABE_CSPOT_I2S_LRCK 38
#endif
#endif
#ifndef ASTROLABE_CSPOT_I2S_DOUT
#ifdef PIN_I2S_DOUT
#define ASTROLABE_CSPOT_I2S_DOUT PIN_I2S_DOUT
#else
#define ASTROLABE_CSPOT_I2S_DOUT 47
#endif
#endif
#ifndef ASTROLABE_CSPOT_I2S_MCLK
#ifdef PIN_I2S_MCLK
#define ASTROLABE_CSPOT_I2S_MCLK PIN_I2S_MCLK
#else
#define ASTROLABE_CSPOT_I2S_MCLK -1
#endif
#endif
#ifndef ASTROLABE_CSPOT_PA
#ifdef PA
#define ASTROLABE_CSPOT_PA PA
#else
#define ASTROLABE_CSPOT_PA -1
#endif
#endif

#if defined(ASTROLABE_AUDIO_CODEC_ES8311) && !defined(ASTROLABE_CSPOT_CODEC_ES8311)
#define ASTROLABE_CSPOT_CODEC_ES8311 1
#endif

#if defined(ASTROLABE_CSPOT_CODEC_ES8311)
extern "C" {
#include "es8311.h"
}
#endif

#if defined(ASTROLABE_CSPOT_CODEC_ES8311)
static es8311_handle_t s_es8311 = nullptr;
static bool s_i2c_installed = false;
#endif

AstrolabeSpeakerAudioSink::AstrolabeSpeakerAudioSink() {
  softwareVolumeControl = false;
  setParams(44100, 2, 16);
}

AstrolabeSpeakerAudioSink::~AstrolabeSpeakerAudioSink() {
  stopI2s();
}

bool AstrolabeSpeakerAudioSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  if (sampleRate == sampleRate_ && channelCount == channelCount_ && bitDepth == bitDepth_ && i2sInstalled_) {
    return true;
  }
  return beginAudio(sampleRate, channelCount, bitDepth) == ESP_OK;
}

void AstrolabeSpeakerAudioSink::feedPCMFrames(const uint8_t *buffer, size_t bytes) {
  if (!buffer || bytes == 0 || !i2sInstalled_) {
    return;
  }
  const uint8_t *p = buffer;
  size_t remain = bytes;
  while (remain > 0) {
    size_t wrote = 0;
    const esp_err_t err = i2s_write(ASTROLABE_CSPOT_I2S, p, remain, &wrote, pdMS_TO_TICKS(500));
    if (err != ESP_OK || wrote == 0) {
      ESP_LOGW(TAG, "i2s_write failed err=%d remain=%u wrote=%u", (int)err, (unsigned)remain, (unsigned)wrote);
      return;
    }
    p += wrote;
    remain -= wrote;
  }
}

void AstrolabeSpeakerAudioSink::volumeChanged(uint16_t volume) {
  int mapped = 0;
  if (volume <= 100) {
    mapped = volume;
  } else {
    mapped = (static_cast<uint32_t>(volume) * 100u) / 65535u;
  }
  mapped = std::max(0, std::min(100, mapped));
  volume_ = mapped;
#if defined(ASTROLABE_CSPOT_CODEC_ES8311)
  if (s_es8311) {
    (void)es8311_voice_volume_set(s_es8311, volume_, nullptr);
  }
#endif
}

esp_err_t AstrolabeSpeakerAudioSink::beginAudio(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  if (bitDepth != 16 || (channelCount != 1 && channelCount != 2)) {
    ESP_LOGE(TAG, "unsupported params sample=%u channels=%u bits=%u", (unsigned)sampleRate, channelCount, bitDepth);
    return ESP_ERR_NOT_SUPPORTED;
  }
  ESP_RETURN_ON_ERROR(beginCodec(sampleRate), TAG, "codec");
  ESP_RETURN_ON_ERROR(beginI2s(sampleRate, channelCount, bitDepth), TAG, "i2s");
  sampleRate_ = sampleRate;
  channelCount_ = channelCount;
  bitDepth_ = bitDepth;
  ESP_LOGI(TAG, "ready sample=%u channels=%u bits=%u bclk=%d lrck=%d dout=%d mclk=%d",
           (unsigned)sampleRate, channelCount, bitDepth, ASTROLABE_CSPOT_I2S_BCLK, ASTROLABE_CSPOT_I2S_LRCK,
           ASTROLABE_CSPOT_I2S_DOUT, ASTROLABE_CSPOT_I2S_MCLK);
  return ESP_OK;
}

esp_err_t AstrolabeSpeakerAudioSink::beginCodec(uint32_t sampleRate) {
  (void)sampleRate;
#if ASTROLABE_CSPOT_PA >= 0
  ESP_RETURN_ON_ERROR(gpio_set_direction((gpio_num_t)ASTROLABE_CSPOT_PA, GPIO_MODE_OUTPUT), TAG, "pa dir");
  ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)ASTROLABE_CSPOT_PA, 1), TAG, "pa on");
#endif

#if defined(ASTROLABE_CSPOT_CODEC_ES8311)
  if (!s_i2c_installed) {
    i2c_config_t i2c = {};
    i2c.mode = I2C_MODE_MASTER;
    i2c.sda_io_num = (gpio_num_t)ASTROLABE_CSPOT_I2C_SDA;
    i2c.scl_io_num = (gpio_num_t)ASTROLABE_CSPOT_I2C_SCL;
    i2c.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c.master.clk_speed = 400000;
    ESP_RETURN_ON_ERROR(i2c_param_config(ASTROLABE_CSPOT_I2C, &i2c), TAG, "i2c param");
    esp_err_t err = i2c_driver_install(ASTROLABE_CSPOT_I2C, i2c.mode, 0, 0, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      return err;
    }
    s_i2c_installed = true;
  }
  if (!s_es8311) {
    s_es8311 = es8311_create(ASTROLABE_CSPOT_I2C, ES8311_ADDRESS_0);
    ESP_RETURN_ON_FALSE(s_es8311, ESP_FAIL, TAG, "es8311_create");
  }
  es8311_clock_config_t clk = {
      .mclk_inverted = false,
      .sclk_inverted = false,
      .mclk_from_mclk_pin = true,
      .mclk_frequency = (int)sampleRate * 256,
      .sample_frequency = (int)sampleRate,
  };
  if (!codecReady_) {
    ESP_RETURN_ON_ERROR(es8311_init(s_es8311, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "es8311 init");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_es8311, false), TAG, "mic off");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es8311, volume_, nullptr), TAG, "volume");
    codecReady_ = true;
  } else {
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(s_es8311, clk.mclk_frequency, clk.sample_frequency), TAG, "sf");
  }
#endif
  return ESP_OK;
}

esp_err_t AstrolabeSpeakerAudioSink::beginI2s(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  stopI2s();
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = sampleRate;
  cfg.bits_per_sample = (i2s_bits_per_sample_t)bitDepth;
  cfg.channel_format = channelCount == 2 ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 512;
  cfg.use_apll = false;
  cfg.tx_desc_auto_clear = true;
  cfg.fixed_mclk = 0;
  cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  cfg.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

  ESP_RETURN_ON_ERROR(i2s_driver_install(ASTROLABE_CSPOT_I2S, &cfg, 0, nullptr), TAG, "install");
  i2sInstalled_ = true;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = ASTROLABE_CSPOT_I2S_MCLK;
  pins.bck_io_num = ASTROLABE_CSPOT_I2S_BCLK;
  pins.ws_io_num = ASTROLABE_CSPOT_I2S_LRCK;
  pins.data_out_num = ASTROLABE_CSPOT_I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  ESP_RETURN_ON_ERROR(i2s_set_pin(ASTROLABE_CSPOT_I2S, &pins), TAG, "pins");
  i2s_zero_dma_buffer(ASTROLABE_CSPOT_I2S);
  return ESP_OK;
}

void AstrolabeSpeakerAudioSink::stopI2s() {
  if (!i2sInstalled_) {
    return;
  }
  i2s_zero_dma_buffer(ASTROLABE_CSPOT_I2S);
  i2s_stop(ASTROLABE_CSPOT_I2S);
  i2s_driver_uninstall(ASTROLABE_CSPOT_I2S);
  i2sInstalled_ = false;
}
