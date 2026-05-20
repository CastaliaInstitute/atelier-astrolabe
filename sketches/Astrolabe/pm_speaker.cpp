#include "pm_speaker.h"

#include <WiFiClient.h>
#include <math.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "driver/i2c.h"
#include "driver/i2s.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "minimp3.h"

extern "C" {
#include "es8311.h"
}

#include "pin_config.h"
#include "faces/pm_faces.h"
#include "pm_audio_analyzer.h"
#include "pm_mic.h"
#include "pm_audio_route.h"
#include "pm_speaker_pcm.h"
#include "pm_usb_uac.h"

static const char *TAG = "pm_speaker";

#define I2S_TX I2S_NUM_0
static constexpr uint32_t kSpeakerTaskStack = 32768;
static constexpr UBaseType_t kSpeakerTaskPriority = 3;
static constexpr int kSpeakerVolume = 70;
static uint32_t s_max_play_seconds = 180u;

static es8311_handle_t s_es = nullptr;
static bool s_es_inited = false;

static TaskHandle_t s_speaker_task = nullptr;
static volatile bool s_spk_task_busy = false;
static volatile bool s_speaker_ok = false;
static volatile PmSpeakerStatus s_speaker_status = PmSpeakerStatus::Idle;
static const uint8_t *s_play_mp3 = nullptr;
static size_t s_play_mp3_len = 0;
static volatile uint8_t s_play_mode = 0; /** 0 = MP3, 1 = tone, 2 = bowl voice */
static volatile bool s_bowl_stop = false;

struct BowlVoiceState {
  volatile float target_hz = 320.f;
  volatile float base_hz = 320.f;
  volatile float excitation = 0.f;
  volatile float energy = 0.f;
  volatile float brightness = 0.5f;
  volatile float pan = 0.f;
  volatile float rim_quality = 0.f;
  volatile bool finger_down = false;
  volatile bool center_strike = false;
  volatile bool want_run = false;
};

static BowlVoiceState s_bowl;
static float s_play_tone_hz = 528.f;
static uint32_t s_play_tone_ms = 5000;
static uint32_t s_play_start_ms = 0;
static uint32_t s_play_est_ms = 1;
static volatile uint32_t s_play_pcm_frames = 0;
static volatile uint32_t s_play_pcm_hz = 0;
static volatile bool s_tone_stop = false;
static volatile bool s_http_mp3_stream_active = false;

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
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(s_es, kSpeakerVolume, nullptr), TAG, "vol");
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
  c.dma_buf_len = 512;
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
  /** Spectrum face only — avoid FFT load / races during chakra tones. */
  if (pm_faces_current() == ClockFace::Spectrum) {
    pm_audio_analyzer_feed_out(pcm, total_s16, 2);
  }
  const uint8_t *p = reinterpret_cast<const uint8_t *>(pcm);
  size_t remain = total_s16 * sizeof(int16_t);
  while (remain > 0) {
    esp_task_wdt_reset();
    size_t wrote = 0;
    if (i2s_write(I2S_TX, p, remain, &wrote, portMAX_DELAY) != ESP_OK) {
      return ESP_FAIL;
    }
    if (wrote == 0) {
      taskYIELD();
      continue;
    }
    p += wrote;
    remain -= wrote;
  }
  return ESP_OK;
}

static int mp3_scan_sync(const uint8_t *buf, int bytes_left) {
  for (int i = 0; i + 1 < bytes_left; ++i) {
    if (buf[i] == 0xFF && (buf[i + 1] & 0xE0) == 0xE0) {
      return i;
    }
  }
  return -1;
}

static void i2s_drain_and_stop(int out_hz, int out_channels) {
  if (out_hz <= 0) {
    return;
  }
  const int i2s_ch = (out_channels == 1) ? 2 : out_channels;
  static int16_t silence[512 * 2];
  memset(silence, 0, sizeof(silence));
  for (int i = 0; i < 6; ++i) {
    (void)i2s_write_all(silence, 512u * static_cast<size_t>(i2s_ch));
  }
  const uint32_t dma_ms =
      (8u * 512u * static_cast<uint32_t>(i2s_ch) * 1000u) / static_cast<uint32_t>(out_hz) + 250u;
  vTaskDelay(pdMS_TO_TICKS(dma_ms > 1200u ? 1200u : dma_ms));
  i2s_stop(I2S_TX);
  vTaskDelay(pdMS_TO_TICKS(20));
  i2s_tx_stop();
}

static bool speaker_wait_idle(uint32_t timeout_ms);

static float bowl_voice_read_energy(void) { return s_bowl.energy; }

static bool play_bowl_voice_streaming(void) {
  pm_mic_stop();

  static constexpr int kToneHz = 22050;
  static constexpr int kPartials = 4;
  static constexpr float kRatios[kPartials] = {1.f, 2.01f, 2.72f, 3.91f};
  static constexpr float kGains[kPartials] = {0.65f, 0.25f, 0.12f, 0.08f};
  static constexpr float kLfoHz[kPartials] = {0.7f, 1.1f, 1.9f, 2.3f};

  if (es8311_board_init(kToneHz) != ESP_OK) {
    ESP_LOGW(TAG, "es8311 init failed (bowl voice)");
    return false;
  }
  if (i2s_tx_begin(kToneHz, 2) != ESP_OK) {
    ESP_LOGW(TAG, "i2s begin failed (bowl voice)");
    return false;
  }

  static int16_t buf[512 * 2];
  double phases[kPartials] = {};
  double lfos[kPartials] = {};
  uint32_t strike_noise_left = 0;
  uint32_t silent_frames = 0;
  uint32_t written = 0;

  s_play_pcm_hz = kToneHz;
  s_play_pcm_frames = 0;
  s_play_est_ms = 60000u;
  s_bowl_stop = false;

  while (!s_bowl_stop) {
    esp_task_wdt_reset();

    if (s_bowl.center_strike) {
      s_bowl.center_strike = false;
      s_bowl.energy = 1.f;
      strike_noise_left = (kToneHz * 35u) / 1000u;
    }

    const float target = s_bowl.target_hz < 80.f ? 80.f : (s_bowl.target_hz > 900.f ? 900.f : s_bowl.target_hz);
    s_bowl.base_hz += (target - s_bowl.base_hz) * 0.06f;

    const float excite = s_bowl.excitation;
    s_bowl.excitation = 0.f;
    const float rim_q = s_bowl.rim_quality < 0.f ? 0.f : (s_bowl.rim_quality > 1.f ? 1.f : s_bowl.rim_quality);
    float e = s_bowl.energy;
    e += excite * rim_q * 0.045f;
    if (s_bowl.finger_down && rim_q > 0.15f) {
      e += 0.0018f * rim_q;
    }
    if (e > 1.f) {
      e = 1.f;
    }
    const float bright =
        s_bowl.brightness < 0.f ? 0.f : (s_bowl.brightness > 1.f ? 1.f : s_bowl.brightness);
    const float pan = s_bowl.pan < -1.f ? -1.f : (s_bowl.pan > 1.f ? 1.f : s_bowl.pan);
    const float pan_l = cosf((pan + 1.f) * 0.78539816f);
    const float pan_r = sinf((pan + 1.f) * 0.78539816f);

    const size_t frame = 512u;
    for (size_t i = 0; i < frame; ++i) {
      e *= 0.99972f;
      float s = 0.f;
      for (int p = 0; p < kPartials; ++p) {
        const float ratio = kRatios[p];
        const float gain = kGains[p] * (p == 0 ? 1.f : (0.35f + 0.65f * bright));
        const double inc =
            (2.0 * 3.14159265358979323846 * static_cast<double>(s_bowl.base_hz) * static_cast<double>(ratio)) /
            static_cast<double>(kToneHz);
        const double lfo_inc =
            (2.0 * 3.14159265358979323846 * static_cast<double>(kLfoHz[p])) / static_cast<double>(kToneHz);
        phases[p] += inc;
        lfos[p] += lfo_inc;
        if (phases[p] > 2.0 * 3.14159265358979323846) {
          phases[p] -= 2.0 * 3.14159265358979323846;
        }
        if (lfos[p] > 2.0 * 3.14159265358979323846) {
          lfos[p] -= 2.0 * 3.14159265358979323846;
        }
        const float shimmer = 1.f + 0.05f * static_cast<float>(sin(lfos[p]));
        s += gain * shimmer * static_cast<float>(sin(phases[p]));
      }
      if (strike_noise_left > 0) {
        const float n = sinf(static_cast<float>(strike_noise_left) * 0.73f) * 0.35f;
        s += n * static_cast<float>(strike_noise_left) / 400.f;
        --strike_noise_left;
      }
      float v = s * e * 0.42f;
      if (v > 1.f) {
        v = 1.f;
      } else if (v < -1.f) {
        v = -1.f;
      }
      int16_t l = static_cast<int16_t>(v * pan_l * 30000.f);
      int16_t r = static_cast<int16_t>(v * pan_r * 30000.f);
      buf[2 * i] = l;
      buf[2 * i + 1] = r;
    }
    s_bowl.energy = e;

    if (i2s_write_all(buf, frame * 2) != ESP_OK) {
      i2s_tx_stop();
      s_bowl.energy = 0.f;
      s_bowl.want_run = false;
      return false;
    }
    written += static_cast<uint32_t>(frame);
    s_play_pcm_frames = written;

    if (!s_bowl.finger_down && e < 0.0025f) {
      ++silent_frames;
      if (silent_frames > (kToneHz / 4u)) {
        break;
      }
    } else {
      silent_frames = 0;
    }

    if (written > kToneHz * s_max_play_seconds) {
      break;
    }
  }

  i2s_drain_and_stop(kToneHz, 2);
  s_bowl.energy = 0.f;
  s_bowl.want_run = false;
  ESP_LOGI(TAG, "bowl voice ended energy=%.3f", static_cast<double>(s_bowl.energy));
  return true;
}

static bool play_tone_streaming(float hz, uint32_t duration_ms) {
  if (hz < 20.f || hz > 2000.f) {
    return false;
  }
  const bool until_stop = (duration_ms == 0);
  pm_mic_stop();

  static constexpr int kToneHz = 22050;
  if (es8311_board_init(kToneHz) != ESP_OK) {
    ESP_LOGW(TAG, "es8311 init failed (tone)");
    return false;
  }
  if (i2s_tx_begin(kToneHz, 2) != ESP_OK) {
    ESP_LOGW(TAG, "i2s begin failed (tone)");
    return false;
  }

  const uint32_t total_samples =
      until_stop ? 0u : static_cast<uint32_t>((static_cast<uint64_t>(duration_ms) * kToneHz) / 1000u);
  const uint32_t fade_in = (kToneHz * 180u) / 1000u;
  const uint32_t fade_out = until_stop ? (kToneHz * 220u) / 1000u : (kToneHz * 900u) / 1000u;
  static int16_t buf[512 * 2];
  double phase = 0.0;
  const double phase_inc = (2.0 * 3.14159265358979323846 * static_cast<double>(hz)) / static_cast<double>(kToneHz);
  uint32_t written = 0;
  uint32_t stop_fade_left = 0;

  s_play_pcm_hz = kToneHz;
  s_play_pcm_frames = 0;
  s_play_est_ms = until_stop ? 60000u : duration_ms + 120u;
  s_tone_stop = false;

  for (;;) {
    esp_task_wdt_reset();
    vTaskDelay(1);
    if (until_stop && s_tone_stop && stop_fade_left == 0) {
      stop_fade_left = fade_out;
    }
    if (!until_stop && written >= total_samples) {
      break;
    }

    const size_t frame = until_stop ? 512u
                                    : ((total_samples - written > 512u) ? 512u : (total_samples - written));
    for (size_t i = 0; i < frame; ++i) {
      const uint32_t pos = written + static_cast<uint32_t>(i);
      float env = 1.f;
      if (pos < fade_in) {
        env = static_cast<float>(pos) / static_cast<float>(fade_in);
      } else if (!until_stop && pos + fade_out > total_samples) {
        const uint32_t tail = total_samples - pos;
        env = static_cast<float>(tail) / static_cast<float>(fade_out);
      } else if (until_stop && stop_fade_left > 0) {
        env = static_cast<float>(stop_fade_left) / static_cast<float>(fade_out);
        --stop_fade_left;
      }
      const float s =
          sinf(static_cast<float>(phase)) * 0.82f * env +
          sinf(static_cast<float>(phase * 2.0)) * 0.12f * env;
      phase += phase_inc;
      if (phase > 2.0 * 3.14159265358979323846) {
        phase -= 2.0 * 3.14159265358979323846;
      }
      int16_t v = static_cast<int16_t>(s * 28000.f);
      if (v > 30000) {
        v = 30000;
      } else if (v < -30000) {
        v = -30000;
      }
      buf[2 * i] = v;
      buf[2 * i + 1] = v;
    }
    if (i2s_write_all(buf, frame * 2) != ESP_OK) {
      i2s_tx_stop();
      return false;
    }
    written += static_cast<uint32_t>(frame);
    s_play_pcm_frames = written;
    if (until_stop && s_tone_stop && stop_fade_left == 0) {
      break;
    }
  }

  i2s_drain_and_stop(kToneHz, 2);
  if (until_stop) {
    ESP_LOGI(TAG, "tone %.1f Hz (loop until stop)", static_cast<double>(hz));
  } else {
    ESP_LOGI(TAG, "tone %.1f Hz ~%u ms", static_cast<double>(hz), duration_ms);
  }
  return true;
}

static bool play_mp3_streaming(const uint8_t *mp3, size_t mp3_len) {
  if (!mp3 || mp3_len == 0) {
    return false;
  }

  if (pm_faces_current() != ClockFace::Spectrum) {
    pm_mic_stop();
  }

  mp3dec_t dec;
  mp3dec_init(&dec);

  const uint8_t *buf = mp3;
  int bytes_left = static_cast<int>(mp3_len);
  bool i2s_ready = false;
  int out_hz = 0;
  int out_channels = 0;
  uint32_t pcm_frames_at_hz = 0;

  static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static int16_t stereo_up[MINIMP3_MAX_SAMPLES_PER_FRAME];

  while (bytes_left > 0) {
    esp_task_wdt_reset();

    mp3dec_frame_info_t info = {};
    const int samples_per_ch = mp3dec_decode_frame(&dec, buf, bytes_left, pcm, &info);
    if (info.frame_bytes <= 0) {
      if (bytes_left > 0) {
        int skip = 1;
        if (bytes_left >= 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
          skip = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) | ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f);
          if (skip < 10) {
            skip = 10;
          }
          if (skip > bytes_left) {
            skip = bytes_left;
          }
        }
        buf += skip;
        bytes_left -= skip;
        continue;
      }
      break;
    }
    buf += info.frame_bytes;
    bytes_left -= info.frame_bytes;

    if (samples_per_ch <= 0) {
      continue;
    }

    if (!i2s_ready) {
      if (info.hz <= 0 || info.channels <= 0) {
        continue;
      }
      out_hz = info.hz;
      out_channels = info.channels;
      if (es8311_board_init(out_hz) != ESP_OK) {
        ESP_LOGW(TAG, "es8311 init failed");
        return false;
      }
      const int i2s_ch = (out_channels == 1) ? 2 : out_channels;
      if (i2s_tx_begin(out_hz, i2s_ch) != ESP_OK) {
        ESP_LOGW(TAG, "i2s begin failed");
        return false;
      }
      i2s_ready = true;
      s_play_pcm_hz = static_cast<uint32_t>(out_hz);
    } else if (info.channels > 0 && info.channels != out_channels) {
      ESP_LOGW(TAG, "mp3 channel change %d -> %d", out_channels, info.channels);
      break;
    } else if (info.hz > 0 && info.hz != out_hz) {
      const int diff = info.hz > out_hz ? info.hz - out_hz : out_hz - info.hz;
      if (diff > 200) {
        ESP_LOGW(TAG, "mp3 rate change %d -> %d", out_hz, info.hz);
        break;
      }
    }

    const int nch = info.channels;
    pcm_frames_at_hz += static_cast<uint32_t>(samples_per_ch);
    s_play_pcm_frames = pcm_frames_at_hz;
    if (out_hz > 0 && pcm_frames_at_hz / static_cast<uint32_t>(out_hz) > s_max_play_seconds) {
      ESP_LOGW(TAG, "playback capped at %us", static_cast<unsigned>(s_max_play_seconds));
      break;
    }

    if (nch == 1) {
      const size_t n = static_cast<size_t>(samples_per_ch);
      if (n * 2 > MINIMP3_MAX_SAMPLES_PER_FRAME) {
        ESP_LOGW(TAG, "mono frame too large");
        break;
      }
      for (size_t i = 0; i < n; ++i) {
        const int16_t s = pcm[i];
        stereo_up[2 * i] = s;
        stereo_up[2 * i + 1] = s;
      }
      if (i2s_write_all(stereo_up, n * 2) != ESP_OK) {
        i2s_tx_stop();
        return false;
      }
    } else {
      const size_t pcm_s16 = static_cast<size_t>(samples_per_ch) * static_cast<size_t>(nch);
      if (pcm_s16 > MINIMP3_MAX_SAMPLES_PER_FRAME) {
        ESP_LOGW(TAG, "stereo frame too large");
        break;
      }
      if (i2s_write_all(pcm, pcm_s16) != ESP_OK) {
        i2s_tx_stop();
        return false;
      }
    }
  }

  if (bytes_left > 32) {
    ESP_LOGW(TAG, "mp3 decode stopped early: %d bytes undecoded of %u", bytes_left,
             static_cast<unsigned>(mp3_len));
  }

  if (i2s_ready && out_hz > 0) {
    if (pcm_frames_at_hz > 0) {
      s_play_est_ms = (pcm_frames_at_hz * 1000u) / static_cast<uint32_t>(out_hz) + 120u;
    }
    i2s_drain_and_stop(out_hz, out_channels);
    ESP_LOGI(TAG, "played ~%u ms (%u PCM frames @ %d Hz)", s_play_est_ms, pcm_frames_at_hz, out_hz);
  }
  return i2s_ready;
}

static constexpr size_t kHttpMp3BufCap = 32768u;
static constexpr size_t kHttpMp3StartBytes = 16384u;
static constexpr size_t kHttpMp3RefillLow = 8192u;

bool pm_speaker_play_mp3_http_stream(WiFiClient *stream, int content_length, volatile bool *cancel) {
  s_http_mp3_stream_active = false;
  if (!stream) {
    return false;
  }
  if (pm_speaker_pcm_active() || pm_usb_uac_speaker_active()) {
    ESP_LOGW(TAG, "HTTP MP3 blocked: PCM/UAC owns speaker");
    return false;
  }
  if (!speaker_wait_idle(8000)) {
    ESP_LOGW(TAG, "HTTP MP3: speaker busy");
    return false;
  }

  uint8_t *buf = static_cast<uint8_t *>(
      heap_caps_malloc(kHttpMp3BufCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t *>(malloc(kHttpMp3BufCap));
  }
  if (!buf) {
    return false;
  }

  if (pm_faces_current() != ClockFace::Spectrum) {
    pm_mic_stop();
  }

  mp3dec_t dec;
  mp3dec_init(&dec);

  size_t fill = 0;
  size_t total_rx = 0;
  bool i2s_ready = false;
  int out_hz = 0;
  int out_channels = 0;
  uint32_t pcm_frames_at_hz = 0;
  bool got_audio = false;

  const uint32_t deadline = millis() + 680000u;
  uint32_t last_rx_ms = millis();

  static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static int16_t stereo_up[MINIMP3_MAX_SAMPLES_PER_FRAME];

  auto body_complete = [&]() -> bool {
    if (content_length > 0) {
      return total_rx >= static_cast<size_t>(content_length);
    }
    return !stream->connected() && stream->available() <= 0;
  };

  auto refill = [&]() -> bool {
    while (fill < kHttpMp3BufCap) {
      if (cancel && *cancel) {
        return false;
      }
      if (content_length > 0 && total_rx >= static_cast<size_t>(content_length)) {
        break;
      }
      const int avail = stream->available();
      if (avail <= 0) {
        if (body_complete()) {
          break;
        }
        if (fill > 0 && last_rx_ms != 0 && (millis() - last_rx_ms) > 90000u) {
          ESP_LOGW(TAG, "HTTP MP3 read stalled");
          return false;
        }
        if (static_cast<int32_t>(millis() - deadline) >= 0) {
          ESP_LOGW(TAG, "HTTP MP3 read timeout");
          return false;
        }
        delay(5);
        esp_task_wdt_reset();
        continue;
      }
      const size_t room = kHttpMp3BufCap - fill;
      const size_t take = static_cast<size_t>(avail) < room ? static_cast<size_t>(avail) : room;
      const int n = stream->readBytes(buf + fill, take);
      if (n > 0) {
        fill += static_cast<size_t>(n);
        total_rx += static_cast<size_t>(n);
        last_rx_ms = millis();
        if (total_rx >= 65536u && (total_rx - static_cast<size_t>(n)) < 65536u) {
          Serial.printf("voice: MP3 stream %u B…\n", static_cast<unsigned>(total_rx));
        } else if (total_rx / 65536u > (total_rx - static_cast<size_t>(n)) / 65536u) {
          Serial.printf("voice: MP3 stream %u B…\n", static_cast<unsigned>(total_rx));
        }
      } else {
        delay(2);
      }
    }
    return true;
  };

  Serial.println("voice: streaming MP3 playback…");
  if (content_length > 0) {
    Serial.printf("voice: MP3 Content-Length %d\n", content_length);
  }
  const size_t start_bytes =
      content_length > 0 && static_cast<size_t>(content_length) < kHttpMp3StartBytes
          ? static_cast<size_t>(content_length)
          : kHttpMp3StartBytes;
  while (fill < start_bytes && !body_complete()) {
    if (!refill()) {
      free(buf);
      return false;
    }
  }
  if (fill > 0) {
    Serial.printf("voice: MP3 prebuffer %u B\n", static_cast<unsigned>(fill));
  }

  for (;;) {
    esp_task_wdt_reset();
    if (cancel && *cancel) {
      free(buf);
      s_http_mp3_stream_active = false;
      if (i2s_ready) {
        i2s_drain_and_stop(out_hz, out_channels);
      }
      return false;
    }

    if (fill < kHttpMp3RefillLow && !body_complete()) {
      if (!refill()) {
        free(buf);
        if (i2s_ready) {
          i2s_drain_and_stop(out_hz, out_channels);
        }
        return false;
      }
    }

    if (fill < 4) {
      if (body_complete()) {
        break;
      }
      if (!refill()) {
        free(buf);
        if (i2s_ready) {
          i2s_drain_and_stop(out_hz, out_channels);
        }
        return false;
      }
      if (fill < 4) {
        continue;
      }
    }

    int bytes_left = static_cast<int>(fill);
    mp3dec_frame_info_t info = {};
    const int samples_per_ch = mp3dec_decode_frame(&dec, buf, bytes_left, pcm, &info);
    if (info.frame_bytes <= 0) {
      if (bytes_left > 0) {
        int skip = 1;
        if (bytes_left >= 10 && buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') {
          skip = ((buf[6] & 0x7f) << 21) | ((buf[7] & 0x7f) << 14) | ((buf[8] & 0x7f) << 7) | (buf[9] & 0x7f);
          if (skip < 10) {
            skip = 10;
          }
          if (skip > bytes_left) {
            skip = bytes_left;
          }
        } else {
          const int sync = mp3_scan_sync(buf, bytes_left);
          if (sync > 0) {
            skip = sync;
          }
        }
        fill -= static_cast<size_t>(skip);
        memmove(buf, buf + skip, fill);
        if (!body_complete() && fill < kHttpMp3RefillLow) {
          continue;
        }
      }
      if (body_complete()) {
        break;
      }
      if (!refill()) {
        free(buf);
        if (i2s_ready) {
          i2s_drain_and_stop(out_hz, out_channels);
        }
        return false;
      }
      continue;
    }

    fill -= static_cast<size_t>(info.frame_bytes);
    memmove(buf, buf + info.frame_bytes, fill);

    if (samples_per_ch <= 0) {
      continue;
    }
    got_audio = true;
    if (!s_http_mp3_stream_active) {
      s_http_mp3_stream_active = true;
      Serial.println("briefing: audio stream active");
    }

    if (!i2s_ready) {
      if (info.hz <= 0 || info.channels <= 0) {
        continue;
      }
      out_hz = info.hz;
      out_channels = info.channels;
      if (es8311_board_init(out_hz) != ESP_OK) {
        ESP_LOGW(TAG, "es8311 init failed (HTTP stream)");
        free(buf);
        return false;
      }
      const int i2s_ch = (out_channels == 1) ? 2 : out_channels;
      if (i2s_tx_begin(out_hz, i2s_ch) != ESP_OK) {
        ESP_LOGW(TAG, "i2s begin failed (HTTP stream)");
        free(buf);
        return false;
      }
      i2s_ready = true;
    } else if (info.channels > 0 && info.channels != out_channels) {
      ESP_LOGW(TAG, "mp3 channel change %d -> %d", out_channels, info.channels);
      break;
    } else if (info.hz > 0 && info.hz != out_hz) {
      const int diff = info.hz > out_hz ? info.hz - out_hz : out_hz - info.hz;
      if (diff > 200) {
        ESP_LOGW(TAG, "mp3 rate change %d -> %d", out_hz, info.hz);
        break;
      }
    }

    const int nch = info.channels;
    pcm_frames_at_hz += static_cast<uint32_t>(samples_per_ch);
    if (out_hz > 0 && pcm_frames_at_hz / static_cast<uint32_t>(out_hz) > s_max_play_seconds) {
      ESP_LOGW(TAG, "HTTP stream playback capped at %us", static_cast<unsigned>(s_max_play_seconds));
      break;
    }

    if (nch == 1) {
      const size_t n = static_cast<size_t>(samples_per_ch);
      if (n * 2 > MINIMP3_MAX_SAMPLES_PER_FRAME) {
        break;
      }
      for (size_t i = 0; i < n; ++i) {
        const int16_t s = pcm[i];
        stereo_up[2 * i] = s;
        stereo_up[2 * i + 1] = s;
      }
      if (i2s_write_all(stereo_up, n * 2) != ESP_OK) {
        free(buf);
        i2s_tx_stop();
        return false;
      }
    } else {
      const size_t pcm_s16 = static_cast<size_t>(samples_per_ch) * static_cast<size_t>(nch);
      if (pcm_s16 > MINIMP3_MAX_SAMPLES_PER_FRAME) {
        break;
      }
      if (i2s_write_all(pcm, pcm_s16) != ESP_OK) {
        free(buf);
        i2s_tx_stop();
        return false;
      }
    }
  }

  free(buf);
  s_http_mp3_stream_active = false;

  if (i2s_ready && out_hz > 0) {
    i2s_drain_and_stop(out_hz, out_channels);
    ESP_LOGI(TAG, "HTTP stream played ~%u ms (%u frames @ %d Hz), rx %u B",
             (pcm_frames_at_hz * 1000u) / static_cast<uint32_t>(out_hz), pcm_frames_at_hz, out_hz,
             static_cast<unsigned>(total_rx));
  }

  return got_audio && i2s_ready;
}

static bool speaker_wait_idle(uint32_t timeout_ms) {
  const uint32_t deadline = millis() + timeout_ms;
  while (s_spk_task_busy) {
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ESP_LOGW(TAG, "speaker_wait_idle timeout");
      return false;
    }
    delay(5);
    esp_task_wdt_reset();
  }
  return true;
}

static void speaker_play_task(void *arg) {
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    s_spk_task_busy = true;
    s_speaker_status = PmSpeakerStatus::Playing;
    if (s_play_mode == 1) {
      s_speaker_ok = play_tone_streaming(s_play_tone_hz, s_play_tone_ms);
    } else if (s_play_mode == 2) {
      s_speaker_ok = play_bowl_voice_streaming();
    } else {
      s_speaker_ok = play_mp3_streaming(s_play_mp3, s_play_mp3_len);
    }
    s_speaker_status = s_speaker_ok ? PmSpeakerStatus::DoneOk : PmSpeakerStatus::DoneFail;
    s_spk_task_busy = false;
  }
}

static uint32_t estimate_mp3_duration_ms(size_t mp3_len) {
  if (mp3_len == 0) {
    return 1000u;
  }
  /** ~96 kbps mono MP3 (Google TTS often a bit higher than 64k). */
  const uint32_t ms = static_cast<uint32_t>((mp3_len * 8ULL * 1000ULL) / 96000ULL);
  if (ms < 2000u) {
    return 2000u;
  }
  if (ms > s_max_play_seconds * 1000u) {
    return s_max_play_seconds * 1000u;
  }
  return ms;
}

void pm_speaker_set_max_play_seconds(uint32_t sec) {
  if (sec < 30u) {
    sec = 30u;
  }
  if (sec > 900u) {
    sec = 900u;
  }
  s_max_play_seconds = sec;
}

uint32_t pm_speaker_max_play_seconds(void) {
  return s_max_play_seconds;
}

static void speaker_task_ensure() {
  if (s_speaker_task) {
    return;
  }
  xTaskCreatePinnedToCore(speaker_play_task, "spk_play", kSpeakerTaskStack, nullptr, kSpeakerTaskPriority,
                          &s_speaker_task, 1);
}

bool pm_speaker_play_begin(const uint8_t *mp3, size_t mp3_len) {
  if (pm_speaker_pcm_active() || pm_usb_uac_speaker_active()) {
    ESP_LOGW(TAG, "MP3 blocked: PCM/UAC owns speaker");
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  speaker_task_ensure();
  if (!s_speaker_task || !mp3 || mp3_len == 0) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  if (!speaker_wait_idle(8000)) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  s_play_mode = 0;
  s_play_mp3 = mp3;
  s_play_mp3_len = mp3_len;
  s_play_start_ms = millis();
  s_play_est_ms = estimate_mp3_duration_ms(mp3_len);
  s_play_pcm_frames = 0;
  s_play_pcm_hz = 0;
  s_speaker_ok = false;
  s_speaker_status = PmSpeakerStatus::Playing;
  xTaskNotify(s_speaker_task, 1, eSetBits);
  return true;
}

PmSpeakerStatus pm_speaker_poll() {
  return s_speaker_status;
}

void pm_speaker_abort(void) {
  if (s_speaker_status == PmSpeakerStatus::Playing || s_spk_task_busy) {
    ESP_LOGW(TAG, "playback aborted (waiting for speaker task)");
    if (s_play_mode == 1 && s_play_tone_ms == 0) {
      s_tone_stop = true;
    } else if (s_play_mode == 2) {
      s_bowl_stop = true;
    }
    s_speaker_status = PmSpeakerStatus::DoneFail;
    (void)speaker_wait_idle(10000);
  }
}

void pm_speaker_tone_stop(void) {
  if (!pm_speaker_is_playing() || s_play_mode != 1 || s_play_tone_ms != 0) {
    return;
  }
  s_tone_stop = true;
  (void)speaker_wait_idle(10000);
}

bool pm_speaker_play_tone_begin(float hz, uint32_t duration_ms) {
  speaker_task_ensure();
  if (!s_speaker_task || hz < 20.f || duration_ms == 0) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  if (!speaker_wait_idle(8000)) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  s_play_mode = 1;
  s_play_tone_hz = hz;
  s_play_tone_ms = duration_ms;
  s_play_start_ms = millis();
  s_play_est_ms = duration_ms + 120u;
  s_play_pcm_frames = 0;
  s_play_pcm_hz = 0;
  s_tone_stop = false;
  s_speaker_ok = false;
  s_speaker_status = PmSpeakerStatus::Playing;
  xTaskNotify(s_speaker_task, 1, eSetBits);
  return true;
}

void pm_speaker_bowl_voice_push(const PmBowlVoiceCtrl &ctrl) {
  s_bowl.target_hz = ctrl.target_hz;
  s_bowl.excitation += ctrl.excitation;
  if (s_bowl.excitation > 1.f) {
    s_bowl.excitation = 1.f;
  }
  s_bowl.brightness = ctrl.brightness;
  s_bowl.pan = ctrl.pan;
  s_bowl.rim_quality = ctrl.rim_quality;
  s_bowl.finger_down = ctrl.finger_down;
  if (ctrl.center_strike) {
    s_bowl.center_strike = true;
  }

  const bool needs_audio = ctrl.finger_down || ctrl.center_strike || ctrl.excitation > 0.01f ||
                           s_bowl.energy > 0.01f;
  if (!needs_audio) {
    return;
  }

  speaker_task_ensure();
  if (!s_speaker_task) {
    return;
  }

  if (s_play_mode == 2 && (s_speaker_status == PmSpeakerStatus::Playing || s_spk_task_busy)) {
    return;
  }
  if (s_spk_task_busy || s_speaker_status == PmSpeakerStatus::Playing) {
    return;
  }

  s_bowl.want_run = true;
  s_play_mode = 2;
  s_play_start_ms = millis();
  s_play_est_ms = 60000u;
  s_play_pcm_frames = 0;
  s_play_pcm_hz = 0;
  s_bowl_stop = false;
  s_speaker_ok = false;
  s_speaker_status = PmSpeakerStatus::Playing;
  xTaskNotify(s_speaker_task, 1, eSetBits);
}

void pm_speaker_bowl_voice_stop(void) {
  s_bowl_stop = true;
  s_bowl.finger_down = false;
  s_bowl.excitation = 0.f;
  if (s_play_mode == 2 && (s_speaker_status == PmSpeakerStatus::Playing || s_spk_task_busy)) {
    (void)speaker_wait_idle(8000);
  }
}

float pm_speaker_bowl_voice_energy(void) { return bowl_voice_read_energy(); }

bool pm_speaker_bowl_voice_active(void) {
  return s_play_mode == 2 && (s_speaker_status == PmSpeakerStatus::Playing || s_spk_task_busy || s_bowl.energy > 0.01f);
}

bool pm_speaker_play_tone_loop_begin(float hz) {
  speaker_task_ensure();
  if (!s_speaker_task || hz < 20.f) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  if (!speaker_wait_idle(8000)) {
    s_speaker_status = PmSpeakerStatus::DoneFail;
    return false;
  }
  s_play_mode = 1;
  s_play_tone_hz = hz;
  s_play_tone_ms = 0;
  s_play_start_ms = millis();
  s_play_est_ms = 60000u;
  s_play_pcm_frames = 0;
  s_play_pcm_hz = 0;
  s_tone_stop = false;
  s_speaker_ok = false;
  s_speaker_status = PmSpeakerStatus::Playing;
  xTaskNotify(s_speaker_task, 1, eSetBits);
  return true;
}

bool pm_speaker_is_playing(void) {
  return s_speaker_status == PmSpeakerStatus::Playing || s_spk_task_busy;
}

bool pm_speaker_http_stream_active(void) {
  return s_http_mp3_stream_active;
}

float pm_speaker_play_progress(void) {
  if (s_speaker_status == PmSpeakerStatus::DoneOk) {
    return 1.f;
  }
  if (s_speaker_status != PmSpeakerStatus::Playing) {
    return 0.f;
  }
  if (s_play_pcm_hz > 0 && s_play_pcm_frames > 0) {
    const uint32_t played_ms = (s_play_pcm_frames * 1000u) / s_play_pcm_hz;
    const uint32_t elapsed = millis() - s_play_start_ms;
    const uint32_t est = s_play_est_ms > played_ms ? s_play_est_ms : played_ms + 80u;
    float p = static_cast<float>(elapsed) / static_cast<float>(est);
    if (p > 0.98f) {
      p = 0.98f;
    }
    return p;
  }
  const uint32_t elapsed = millis() - s_play_start_ms;
  if (s_play_est_ms == 0) {
    return 0.f;
  }
  float p = static_cast<float>(elapsed) / static_cast<float>(s_play_est_ms);
  if (p > 0.98f) {
    p = 0.98f;
  }
  return p;
}

bool pm_speaker_play_mp3(const uint8_t *mp3, size_t mp3_len) {
  if (!pm_speaker_play_begin(mp3, mp3_len)) {
    return false;
  }
  const uint32_t timeout_ms =
      s_max_play_seconds * 1000u + 15000u + static_cast<uint32_t>((mp3_len / 4000u) * 1000u);
  const uint32_t deadline = millis() + timeout_ms;
  for (;;) {
    const PmSpeakerStatus st = pm_speaker_poll();
    if (st == PmSpeakerStatus::DoneOk) {
      return true;
    }
    if (st == PmSpeakerStatus::DoneFail) {
      return false;
    }
    if (static_cast<int32_t>(millis() - deadline) >= 0) {
      ESP_LOGW(TAG, "speaker play timeout");
      return false;
    }
    delay(10);
    esp_task_wdt_reset();
  }
}
