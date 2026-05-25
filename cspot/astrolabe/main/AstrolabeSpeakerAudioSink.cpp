#include "AstrolabeSpeakerAudioSink.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <Wire.h>

#include "AstrolabeAudioVisualizer.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "astrolabe_cspot_audio";
static constexpr float kInstrumentPi = 3.14159265358979323846f;
static constexpr float kInstrumentSampleRate = 44100.f;
static portMUX_TYPE s_instrumentMux = portMUX_INITIALIZER_UNLOCKED;

struct InstrumentVoice {
  float phase = 0.f;
  float phaseStep = 0.f;
  float env = 0.f;
  float decay = 0.9994f;
};

static InstrumentVoice s_instrumentVoices[8];
static uint8_t s_nextInstrumentVoice = 0;

#define ASTROLABE_CSPOT_I2S I2S_NUM_0
#define ASTROLABE_CSPOT_I2C 0

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
static uint16_t s_es8311_addr = 0;
#endif

AstrolabeSpeakerAudioSink::AstrolabeSpeakerAudioSink() {
  softwareVolumeControl = false;
  if (!setParams(44100, 2, 16)) {
    ESP_LOGE(TAG, "initial audio setup failed");
  }
}

AstrolabeSpeakerAudioSink::~AstrolabeSpeakerAudioSink() {
  stopI2s();
}

void astrolabe_instrument_note_on(int note, uint8_t velocity) {
  static constexpr float kBaseFreq = 261.6256f;  // Middle C.
  note = std::max(-24, std::min(36, note));
  const float freq = kBaseFreq * powf(2.f, static_cast<float>(note) / 12.f);
  const float amp = std::max(0.08f, std::min(0.42f, static_cast<float>(velocity) / 255.f * 0.42f));
  portENTER_CRITICAL(&s_instrumentMux);
  InstrumentVoice &v = s_instrumentVoices[s_nextInstrumentVoice++ % (sizeof(s_instrumentVoices) / sizeof(s_instrumentVoices[0]))];
  v.phase = 0.f;
  v.phaseStep = (2.f * kInstrumentPi * freq) / kInstrumentSampleRate;
  v.env = amp;
  v.decay = 0.99935f - std::min(0.00025f, std::max(0, note) * 0.000006f);
  portEXIT_CRITICAL(&s_instrumentMux);
}

static int16_t instrumentNextSample() {
  float mixed = 0.f;
  portENTER_CRITICAL(&s_instrumentMux);
  for (InstrumentVoice &v : s_instrumentVoices) {
    if (v.env <= 0.0008f) {
      v.env = 0.f;
      continue;
    }
    const float fundamental = sinf(v.phase);
    const float shimmer = 0.28f * sinf(v.phase * 2.01f);
    mixed += (fundamental + shimmer) * v.env;
    v.phase += v.phaseStep;
    if (v.phase >= 2.f * kInstrumentPi) {
      v.phase -= 2.f * kInstrumentPi;
    }
    v.env *= v.decay;
  }
  portEXIT_CRITICAL(&s_instrumentMux);
  mixed = std::max(-0.85f, std::min(0.85f, mixed));
  return static_cast<int16_t>(mixed * 32767.f);
}

static void mixInstrumentIntoPcm(int16_t *samples, size_t sampleCount) {
  if (!samples || sampleCount == 0) {
    return;
  }
  for (size_t i = 0; i < sampleCount; i += 2) {
    const int16_t note = instrumentNextSample();
    if (note == 0) {
      continue;
    }
    const int32_t left = static_cast<int32_t>(samples[i]) + note;
    samples[i] = static_cast<int16_t>(std::max<int32_t>(INT16_MIN, std::min<int32_t>(INT16_MAX, left)));
    if (i + 1 < sampleCount) {
      const int32_t right = static_cast<int32_t>(samples[i + 1]) + note;
      samples[i + 1] = static_cast<int16_t>(std::max<int32_t>(INT16_MIN, std::min<int32_t>(INT16_MAX, right)));
    }
  }
}

bool AstrolabeSpeakerAudioSink::setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) {
  if (sampleRate == sampleRate_ && channelCount == channelCount_ && bitDepth == bitDepth_ && i2sInstalled_) {
    return true;
  }
  return beginAudio(sampleRate, channelCount, bitDepth) == ESP_OK;
}

void AstrolabeSpeakerAudioSink::feedPCMFrames(const uint8_t *buffer, size_t bytes) {
  if (!buffer || bytes == 0 || !i2sInstalled_) {
    if (!i2sInstalled_) {
      ESP_LOGW(TAG, "dropping %u PCM bytes; i2s not installed", (unsigned)bytes);
    }
    return;
  }
  static size_t s_pcmBytes = 0;

  auto writeChunk = [&](const uint8_t *p, size_t len) -> bool {
    size_t remain = len;
    while (remain > 0) {
      size_t wrote = 0;
      const esp_err_t err = i2s_write(ASTROLABE_CSPOT_I2S, p, remain, &wrote, pdMS_TO_TICKS(500));
      if (err != ESP_OK || wrote == 0) {
        ESP_LOGW(TAG, "i2s_write failed err=%d remain=%u wrote=%u", (int)err, (unsigned)remain, (unsigned)wrote);
        return false;
      }
      p += wrote;
      remain -= wrote;
      s_pcmBytes += wrote;
      if (s_pcmBytes >= 64 * 1024) {
        s_pcmBytes = 0;
      }
    }
    return true;
  };

#if defined(ASTROLABE_WAVESHARE_S3_185_V1)
  const bool applySoftwareVolume = bitDepth_ == 16 && volume_ < 100;
#else
  const bool applySoftwareVolume = false;
#endif

  if (applySoftwareVolume) {
    int16_t scaled[512];
    const uint8_t *p = buffer;
    size_t remain = bytes;
    while (remain > 0) {
      size_t chunkBytes = std::min(remain, sizeof(scaled));
      chunkBytes &= ~static_cast<size_t>(1);
      if (chunkBytes == 0) {
        break;
      }
      const int16_t *src = reinterpret_cast<const int16_t *>(p);
      const size_t samples = chunkBytes / sizeof(int16_t);
      for (size_t i = 0; i < samples; ++i) {
        scaled[i] = static_cast<int16_t>((static_cast<int32_t>(src[i]) * volume_) / 100);
      }
      mixInstrumentIntoPcm(scaled, samples);
      astrolabe_audio_visualizer_feed_output_pcm(scaled, samples, channelCount_);
      if (!writeChunk(reinterpret_cast<const uint8_t *>(scaled), chunkBytes)) {
        return;
      }
      p += chunkBytes;
      remain -= chunkBytes;
    }
    return;
  }

  if (bitDepth_ == 16) {
    int16_t mixed[512];
    const uint8_t *p = buffer;
    size_t remain = bytes;
    while (remain > 0) {
      size_t chunkBytes = std::min(remain, sizeof(mixed));
      chunkBytes &= ~static_cast<size_t>(1);
      if (chunkBytes == 0) {
        break;
      }
      memcpy(mixed, p, chunkBytes);
      const size_t samples = chunkBytes / sizeof(int16_t);
      mixInstrumentIntoPcm(mixed, samples);
      astrolabe_audio_visualizer_feed_output_pcm(mixed, samples, channelCount_);
      if (!writeChunk(reinterpret_cast<const uint8_t *>(mixed), chunkBytes)) {
        return;
      }
      p += chunkBytes;
      remain -= chunkBytes;
    }
    return;
  }
  (void)writeChunk(buffer, bytes);
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
#if defined(ASTROLABE_CSPOT_CODEC_ES8311)
  Wire.begin(ASTROLABE_CSPOT_I2C_SDA, ASTROLABE_CSPOT_I2C_SCL, 400000);
  auto probeAddr = [](uint16_t addr) -> bool {
    Wire.beginTransmission(static_cast<uint8_t>(addr));
    return Wire.endTransmission() == 0;
  };
  const uint16_t addr = probeAddr(ES8311_ADDRESS_0) ? ES8311_ADDRESS_0
                       : probeAddr(ES8311_ADDRESS_1) ? ES8311_ADDRESS_1
                                                     : 0;
  ESP_RETURN_ON_FALSE(addr != 0, ESP_FAIL, TAG, "es8311 probe");
  if (s_es8311 && s_es8311_addr != addr) {
    es8311_delete(s_es8311);
    s_es8311 = nullptr;
    codecReady_ = false;
  }
  if (!s_es8311) {
    s_es8311_addr = addr;
    s_es8311 = es8311_create(ASTROLABE_CSPOT_I2C, s_es8311_addr);
    ESP_RETURN_ON_FALSE(s_es8311, ESP_FAIL, TAG, "es8311_create");
    ESP_LOGI(TAG, "ES8311 codec found at 0x%02x", (unsigned)s_es8311_addr);
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
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(s_es8311, clk.mclk_frequency, clk.sample_frequency), TAG, "sf");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_es8311, false), TAG, "mic off");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es8311, volume_, nullptr), TAG, "volume");
#if ASTROLABE_CSPOT_PA >= 0
    ESP_RETURN_ON_ERROR(gpio_set_direction((gpio_num_t)ASTROLABE_CSPOT_PA, GPIO_MODE_OUTPUT), TAG, "pa dir");
    ESP_RETURN_ON_ERROR(gpio_set_level((gpio_num_t)ASTROLABE_CSPOT_PA, 1), TAG, "pa on");
#endif
    codecReady_ = true;
    ESP_LOGI(TAG, "ES8311 ready sample=%u volume=%d pa=%d", (unsigned)sampleRate, volume_, ASTROLABE_CSPOT_PA);
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
  cfg.dma_buf_count = 12;
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
  ESP_RETURN_ON_ERROR(i2s_start(ASTROLABE_CSPOT_I2S), TAG, "start");
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
