#include "pm_usb_uac.h"

#include "esp_log.h"
#include "esp_attr.h"
#include "pm_audio_route.h"
#include "pm_log.h"
#include "pm_speaker_pcm.h"
#include "sdkconfig.h"

extern "C" {
RTC_NOINIT_ATTR volatile uint32_t g_pm_uac_backend;
RTC_NOINIT_ATTR volatile uint32_t g_pm_uac_stage;
RTC_NOINIT_ATTR volatile int32_t g_pm_uac_error;
RTC_NOINIT_ATTR volatile uint32_t g_pm_uac_ep_out;
RTC_NOINIT_ATTR volatile uint32_t g_pm_uac_ep_fb;
}

static void uac_mark(uint32_t backend, uint32_t stage, int32_t error = 0) {
  g_pm_uac_backend = backend;
  g_pm_uac_stage = stage;
  g_pm_uac_error = error;
}

static volatile uint32_t s_speaker_stream_events = 0;

static void uac_note_speaker_stream_used(void) {
  ++s_speaker_stream_events;
}

bool pm_usb_uac_consume_speaker_stream_event(void) {
  if (s_speaker_stream_events == 0) {
    return false;
  }
  s_speaker_stream_events = 0;
  return true;
}

#if CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && CONFIG_TINYUSB_AUDIO_ENABLED && !defined(ASTROLABE_QEMU) && __has_include("USBAudioCard.h")

#include "USB.h"
#include "USBAudioCard.h"

static const char *TAG = "pm_usb_uac";

#ifndef ASTROLABE_UAC_SAMPLE_HZ
#define ASTROLABE_UAC_SAMPLE_HZ 48000
#endif

static USBAudioCard s_uac(ASTROLABE_UAC_SAMPLE_HZ, UAC_BPS_16, UAC_SPK_STEREO, UAC_MIC_NONE);
static bool s_uac_ready = false;
static bool s_pcm_open = false;
static bool s_host_stream_open = false;

static void uac_output_cb(void *data, uint16_t len) {
  if (!data || len == 0) {
    return;
  }
  if (!s_host_stream_open) {
    s_host_stream_open = true;
    uac_note_speaker_stream_used();
  }
  if (!pm_audio_route_output_usb()) {
    return;
  }
  if (!s_pcm_open) {
    if (pm_speaker_pcm_begin(ASTROLABE_UAC_SAMPLE_HZ, 2) != ESP_OK) {
      return;
    }
    s_pcm_open = true;
  }
  s_uac.applyVolume(data, len);
  const size_t num_s16 = len / sizeof(int16_t);
  (void)pm_speaker_pcm_write(static_cast<const int16_t *>(data), num_s16);
}

static void uac_event_cb(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  (void)arg;
  if (event_base != ARDUINO_USB_AUDIO_CARD_EVENTS || !event_data) {
    return;
  }
  auto *data = static_cast<arduino_usb_audio_card_event_data_t *>(event_data);
  switch (event_id) {
  case ARDUINO_USB_AUDIO_CARD_MUTE_EVENT:
    pm_speaker_pcm_set_mute(data->mute.muted);
    ESP_LOGI(TAG, "mute ch=%d muted=%d", data->mute.channel, data->mute.muted);
    break;
  case ARDUINO_USB_AUDIO_CARD_VOLUME_EVENT:
    pm_speaker_pcm_set_volume(data->volume.db);
    ESP_LOGI(TAG, "volume ch=%d db=%d", data->volume.channel, data->volume.db);
    break;
  case ARDUINO_USB_AUDIO_CARD_INTERFACE_ENABLE_EVENT:
    if (data->interface_enable.interface != UAC_INTERFACE_MIC) {
      if (data->interface_enable.enable) {
        if (!s_host_stream_open) {
          s_host_stream_open = true;
          uac_note_speaker_stream_used();
        }
      } else {
        s_host_stream_open = false;
      }
    }
    ESP_LOGI(TAG, "interface %s enabled=%d", data->interface_enable.interface == UAC_INTERFACE_MIC ? "mic" : "speaker",
             data->interface_enable.enable);
    break;
  default:
    break;
  }
}

bool pm_usb_uac_begin(void) {
  uac_mark(1, 10);
  if (s_uac_ready) {
    uac_mark(1, 60);
    return true;
  }

  USB.VID(0x303A);
  USB.PID(0x8000);
  USB.manufacturerName("Castalia Institute");
  USB.productName("Astrolabe");
  USB.serialNumber("astrolabe");

  s_uac.onEvent(uac_event_cb);
  s_uac.onData(uac_output_cb);
  uac_mark(1, 20);
  if (!s_uac.begin()) {
    uac_mark(1, 31, -1);
    ESP_LOGE(TAG, "USBAudioCard begin failed");
    return false;
  }
  uac_mark(1, 40);
  if (!USB.begin()) {
    uac_mark(1, 51, -1);
    ESP_LOGE(TAG, "USB begin failed");
    return false;
  }

  s_uac_ready = true;
  uac_mark(1, 60);
  ESP_LOGI(TAG, "USB UAC speaker @ %d Hz", ASTROLABE_UAC_SAMPLE_HZ);
  return true;
}

bool pm_usb_uac_ready(void) {
  return s_uac_ready;
}

void pm_usb_uac_release_speaker(void) {
  if (s_pcm_open) {
    pm_speaker_pcm_end();
    s_pcm_open = false;
  }
  s_host_stream_open = false;
}

bool pm_usb_uac_speaker_active(void) {
  return s_uac_ready && pm_audio_route_output_usb();
}

#elif CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && CONFIG_TINYUSB_AUDIO_ENABLED && !defined(ASTROLABE_QEMU) && \
    __has_include("esp32-hal-tinyusb.h")

#include "pm_usb_uac_tinyusb_config.h"
#include "class/audio/audio.h"
#include "common/tusb_fifo.h"
#include "class/audio/audio_device.h"
#include "device/usbd_pvt.h"
#include "esp32-hal-tinyusb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/periph_ctrl.h"
#include "soc/periph_defs.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/usb_serial_jtag_reg.h"
#include "soc/usb_wrap_reg.h"
#include "tusb.h"

static const char *TAG = "pm_usb_uac";

#ifndef ASTROLABE_UAC_SAMPLE_HZ
#define ASTROLABE_UAC_SAMPLE_HZ 48000
#endif

#if CONFIG_UAC_SPEAKER_CHANNEL_NUM != 2
#error "Astrolabe raw TinyUSB UAC backend currently expects stereo speaker output"
#endif

enum {
  UAC_ENTITY_SPK_INPUT_TERMINAL = 0x01,
  UAC_ENTITY_SPK_FEATURE_UNIT = 0x02,
  UAC_ENTITY_SPK_OUTPUT_TERMINAL = 0x03,
  UAC_ENTITY_CLOCK = 0x04,
};

static bool s_uac_ready = false;
static bool s_pcm_open = false;
static bool s_host_stream_open = false;
static uint32_t s_rx_cb_count = 0;
static uint32_t s_rx_bytes_total = 0;
static uint32_t s_rx_read_total = 0;
static volatile uint32_t s_fb_done_count = 0;
static volatile uint32_t s_fb_isr_count = 0;
static uint8_t s_ep_out = 0x01;
static uint8_t s_ep_fb = 0x81;
static constexpr uint8_t kStrManufacturer = 0x01;
static constexpr uint8_t kStrProduct = 0x02;
static constexpr uint8_t kStrSerial = 0x03;
static constexpr uint8_t kStrControl = 0x04;
static constexpr uint8_t kStrStream = 0x05;
static int16_t s_volume_db_8_8 = 0;
static bool s_muted = false;
static TaskHandle_t s_tusb_task = nullptr;

typedef struct {
  bool external_phy;
} tinyusb_phy_config_t;

extern "C" esp_err_t tinyusb_driver_install(tinyusb_phy_config_t const *config);

static void uac_prepare_usb_otg_peripheral(void) {
  CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_USB_PAD_ENABLE);
  SET_PERI_REG_MASK(RTC_CNTL_USB_CONF_REG, RTC_CNTL_SW_HW_USB_PHY_SEL | RTC_CNTL_SW_USB_PHY_SEL);
  SET_PERI_REG_MASK(USB_WRAP_OTG_CONF_REG, USB_WRAP_USB_PAD_ENABLE);
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_IO_MUX_RESET_DISABLE);
  REG_CLR_BIT(RTC_CNTL_USB_CONF_REG, RTC_CNTL_USB_RESET_DISABLE);
  periph_module_reset(PERIPH_USB_MODULE);
  periph_module_enable(PERIPH_USB_MODULE);
}

extern "C" {
void dcd_sof_enable(uint8_t rhport, bool en) {
  (void)rhport;
  (void)en;
}

void audiod_init(void);
void audiod_reset(uint8_t rhport);
uint16_t audiod_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len);
bool audiod_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request);
bool audiod_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes);
void audiod_sof_isr(uint8_t rhport, uint32_t frame_count);
}

extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
  static usbd_class_driver_t const audio_driver = {
#if CFG_TUSB_DEBUG >= CFG_TUD_LOG_LEVEL
      "AUDIO",
#endif
      audiod_init,
      audiod_reset,
      audiod_open,
      audiod_control_xfer_cb,
      audiod_xfer_cb,
      audiod_sof_isr,
  };
  *driver_count = 1;
  return &audio_driver;
}

static void tusb_device_task(void *arg) {
  (void)arg;
  while (true) {
    tud_task();
  }
}

static tusb_desc_device_t const s_uac_device_desc = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x8000,
    .bcdDevice = 0x0100,
    .iManufacturer = kStrManufacturer,
    .iProduct = kStrProduct,
    .iSerialNumber = kStrSerial,
    .bNumConfigurations = 0x01,
};

static uint8_t s_uac_config_desc[TUD_CONFIG_DESC_LEN + CFG_TUD_AUDIO_FUNC_1_DESC_LEN];
static bool s_uac_config_desc_ready = false;

static void uac_build_config_descriptor(void) {
  if (s_uac_config_desc_ready) {
    return;
  }
  uint8_t *dst = s_uac_config_desc;
  const uint8_t config[] = {
      TUD_CONFIG_DESCRIPTOR(1, 2, 0, sizeof(s_uac_config_desc), 0x00, 100),
  };
  memcpy(dst, config, sizeof(config));
  dst += sizeof(config);

  const uint8_t first_itf = 0;
  const uint8_t channel_ctrl =
      (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_MUTE_POS) |
      (AUDIO_CTRL_RW << AUDIO_FEATURE_UNIT_CTRL_VOLUME_POS);
  const uint8_t desc[] = {
      TUD_AUDIO_DESC_IAD(first_itf, 2, kStrControl),
      TUD_AUDIO_DESC_STD_AC(first_itf, 0x00, kStrControl),
      TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_DESKTOP_SPEAKER,
                           TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN +
                               TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN,
                           AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),
      TUD_AUDIO_DESC_CLK_SRC(UAC_ENTITY_CLOCK, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                             (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) |
                                 (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS),
                             UAC_ENTITY_SPK_INPUT_TERMINAL, 0x00),
      TUD_AUDIO_DESC_INPUT_TERM(UAC_ENTITY_SPK_INPUT_TERMINAL, AUDIO_TERM_TYPE_USB_STREAMING, 0x00,
                                UAC_ENTITY_CLOCK, 0x02,
                                AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, 0x00,
                                (AUDIO_CTRL_R << AUDIO_IN_TERM_CTRL_CONNECTOR_POS), 0x00),
      TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(UAC_ENTITY_SPK_FEATURE_UNIT, UAC_ENTITY_SPK_INPUT_TERMINAL,
                                              channel_ctrl, channel_ctrl, channel_ctrl, 0x00),
      TUD_AUDIO_DESC_OUTPUT_TERM(UAC_ENTITY_SPK_OUTPUT_TERMINAL, AUDIO_TERM_TYPE_OUT_DESKTOP_SPEAKER, 0x00,
                                 UAC_ENTITY_SPK_FEATURE_UNIT, UAC_ENTITY_CLOCK, 0x0000, 0x00),
      TUD_AUDIO_DESC_STD_AS_INT(static_cast<uint8_t>(first_itf + 1), 0x00, 0x00, kStrStream),
      TUD_AUDIO_DESC_STD_AS_INT(static_cast<uint8_t>(first_itf + 1), 0x01, 0x02, kStrStream),
      TUD_AUDIO_DESC_CS_AS_INT(UAC_ENTITY_SPK_INPUT_TERMINAL, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I,
                               AUDIO_DATA_FORMAT_TYPE_I_PCM, 0x02,
                               AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT, 0x00),
      TUD_AUDIO_DESC_TYPE_I_FORMAT(CFG_TUD_AUDIO_FUNC_1_FORMAT_1_N_BYTES_PER_SAMPLE_RX,
                                   CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX),
      TUD_AUDIO_DESC_STD_AS_ISO_EP(s_ep_out, (TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS |
                                              TUSB_ISO_EP_ATT_DATA),
                                   CFG_TUD_AUDIO_FUNC_1_FORMAT_1_EP_SZ_OUT, 0x01),
      TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK, AUDIO_CTRL_NONE,
                                  AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_MILLISEC, 0x0001),
      TUD_AUDIO_DESC_STD_AS_ISO_FB_EP(s_ep_fb, 0x01),
  };
  static_assert(sizeof(desc) == CFG_TUD_AUDIO_FUNC_1_DESC_LEN, "UAC descriptor length mismatch");
  memcpy(dst, desc, sizeof(desc));
  s_uac_config_desc_ready = true;
}

extern "C" uint8_t const *tud_descriptor_device_cb(void) {
  return reinterpret_cast<uint8_t const *>(&s_uac_device_desc);
}

extern "C" uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  uac_build_config_descriptor();
  return s_uac_config_desc;
}

extern "C" uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  static uint16_t desc_str[32];
  const char *str = nullptr;
  uint8_t chr_count = 0;

  if (index == 0) {
    desc_str[1] = 0x0409;
    chr_count = 1;
  } else {
    switch (index) {
    case kStrManufacturer:
      str = "Castalia Institute";
      break;
    case kStrProduct:
      str = "Astrolabe";
      break;
    case kStrSerial:
      str = "astrolabe";
      break;
    case kStrControl:
      str = "Astrolabe";
      break;
    case kStrStream:
      str = "Astrolabe";
      break;
    default:
      return nullptr;
    }
    chr_count = static_cast<uint8_t>(strlen(str));
    if (chr_count > 31) {
      chr_count = 31;
    }
    for (uint8_t i = 0; i < chr_count; ++i) {
      desc_str[1 + i] = static_cast<uint8_t>(str[i]);
    }
  }
  desc_str[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
  return desc_str;
}

static bool uac_pcm_open(void) {
  if (s_pcm_open) {
    return true;
  }
  if (!pm_audio_route_output_usb()) {
    pm_log_printf(false, "uac: pcm open skipped route=onboard");
    return false;
  }
  if (pm_speaker_pcm_begin(ASTROLABE_UAC_SAMPLE_HZ, 2) != ESP_OK) {
    pm_log_printf(false, "uac: pcm open failed hz=%d ch=2", ASTROLABE_UAC_SAMPLE_HZ);
    return false;
  }
  s_pcm_open = true;
  pm_log_printf(false, "uac: pcm open hz=%d ch=2", ASTROLABE_UAC_SAMPLE_HZ);
  return true;
}

static void uac_drain_audio(void) {
  static int16_t pcm[256];
  while (tud_audio_available() >= sizeof(int16_t)) {
    const uint16_t n = tud_audio_read(pcm, sizeof(pcm));
    if (n == 0) {
      break;
    }
    s_rx_read_total += n;
    if (!s_host_stream_open) {
      s_host_stream_open = true;
      uac_note_speaker_stream_used();
      pm_log_printf(false, "uac: speaker packets active");
    }
    if (uac_pcm_open()) {
      (void)pm_speaker_pcm_write(pcm, n / sizeof(int16_t));
    }
  }
}

extern "C" bool tud_audio_rx_done_post_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id,
                                                uint8_t ep_out, uint8_t cur_alt_setting) {
  (void)rhport;
  ++s_rx_cb_count;
  s_rx_bytes_total += n_bytes_received;
  const uint16_t avail = tud_audio_available();
  if (s_rx_cb_count <= 4 || (s_rx_cb_count & 0x3f) == 0) {
    pm_log_printf(false, "uac: rx cb=%u bytes=%u total=%u read=%u avail=%u func=%u ep=0x%02x alt=%u",
                  static_cast<unsigned>(s_rx_cb_count), static_cast<unsigned>(n_bytes_received),
                  static_cast<unsigned>(s_rx_bytes_total), static_cast<unsigned>(s_rx_read_total),
                  static_cast<unsigned>(avail), static_cast<unsigned>(func_id), static_cast<unsigned>(ep_out),
                  static_cast<unsigned>(cur_alt_setting));
  }
  if (cur_alt_setting != 0) {
    uac_drain_audio();
  }
  return true;
}

extern "C" bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *request) {
  (void)rhport;
  if (request && request->wValue != 0) {
    const uint32_t feedback = (static_cast<uint32_t>(ASTROLABE_UAC_SAMPLE_HZ) / 1000u) << 16;
    (void)tud_audio_fb_set(feedback);
    if (!s_host_stream_open) {
      s_host_stream_open = true;
      uac_note_speaker_stream_used();
      pm_log_printf(false, "uac: speaker stream open");
    }
    ESP_LOGI(TAG, "speaker stream open");
  } else {
    s_host_stream_open = false;
    pm_log_printf(false, "uac: speaker stream closed rx_cb=%u rx_bytes=%u rx_read=%u fb_isr=%u fb_done=%u",
                  static_cast<unsigned>(s_rx_cb_count), static_cast<unsigned>(s_rx_bytes_total),
                  static_cast<unsigned>(s_rx_read_total), static_cast<unsigned>(s_fb_isr_count),
                  static_cast<unsigned>(s_fb_done_count));
    pm_usb_uac_release_speaker();
    ESP_LOGI(TAG, "speaker stream closed");
  }
  return true;
}

extern "C" void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf,
                                              audio_feedback_params_t *feedback_param) {
  (void)func_id;
  (void)alt_itf;
  feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED;
  feedback_param->sample_freq = ASTROLABE_UAC_SAMPLE_HZ;
  feedback_param->frequency.mclk_freq = ASTROLABE_UAC_SAMPLE_HZ * 256u;
}

extern "C" void tud_audio_fb_done_cb(uint8_t func_id) {
  (void)func_id;
  ++s_fb_done_count;
}

static bool uac_clock_get(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ) {
    if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_4_t cur = {.bCur = static_cast<int32_t>(ASTROLABE_UAC_SAMPLE_HZ)};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                        &cur, sizeof(cur));
    }
    if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_4_n_t(1) range = {
          .wNumSubRanges = 1,
          .subrange = {{static_cast<int32_t>(ASTROLABE_UAC_SAMPLE_HZ),
                        static_cast<int32_t>(ASTROLABE_UAC_SAMPLE_HZ), 0}},
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                        &range, sizeof(range));
    }
  }
  if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t valid = {.bCur = 1};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                      &valid, sizeof(valid));
  }
  return false;
}

static bool uac_feature_get(uint8_t rhport, audio_control_request_t const *request) {
  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR) {
    audio_control_cur_1_t mute = {.bCur = static_cast<int8_t>(s_muted ? 1 : 0)};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                      &mute, sizeof(mute));
  }
  if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME) {
    if (request->bRequest == AUDIO_CS_REQ_CUR) {
      audio_control_cur_2_t vol = {.bCur = s_volume_db_8_8};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                        &vol, sizeof(vol));
    }
    if (request->bRequest == AUDIO_CS_REQ_RANGE) {
      audio_control_range_2_n_t(1) range = {
          .wNumSubRanges = 1,
          .subrange = {{static_cast<int16_t>(-50 * 256), 0, 256}},
      };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, reinterpret_cast<tusb_control_request_t const *>(request),
                                                        &range, sizeof(range));
    }
  }
  return false;
}

extern "C" bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request) {
  audio_control_request_t const *audio_request = reinterpret_cast<audio_control_request_t const *>(request);
  if (audio_request->bEntityID == UAC_ENTITY_CLOCK) {
    return uac_clock_get(rhport, audio_request);
  }
  if (audio_request->bEntityID == UAC_ENTITY_SPK_FEATURE_UNIT) {
    return uac_feature_get(rhport, audio_request);
  }
  return false;
}

extern "C" bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *request, uint8_t *buf) {
  (void)rhport;
  audio_control_request_t const *audio_request = reinterpret_cast<audio_control_request_t const *>(request);
  if (audio_request->bEntityID != UAC_ENTITY_SPK_FEATURE_UNIT || audio_request->bRequest != AUDIO_CS_REQ_CUR) {
    return false;
  }
  if (audio_request->bControlSelector == AUDIO_FU_CTRL_MUTE && audio_request->wLength == sizeof(audio_control_cur_1_t)) {
    s_muted = reinterpret_cast<audio_control_cur_1_t const *>(buf)->bCur != 0;
    pm_speaker_pcm_set_mute(s_muted);
    ESP_LOGI(TAG, "mute=%d", s_muted ? 1 : 0);
    return true;
  }
  if (audio_request->bControlSelector == AUDIO_FU_CTRL_VOLUME &&
      audio_request->wLength == sizeof(audio_control_cur_2_t)) {
    s_volume_db_8_8 = reinterpret_cast<audio_control_cur_2_t const *>(buf)->bCur;
    int volume = ((static_cast<int>(s_volume_db_8_8) / 256) + 50) * 2;
    if (volume < 0) {
      volume = 0;
    } else if (volume > 100) {
      volume = 100;
    }
    pm_speaker_pcm_set_volume(volume);
    ESP_LOGI(TAG, "volume_db=%d volume=%d", static_cast<int>(s_volume_db_8_8) / 256, volume);
    return true;
  }
  return false;
}

bool pm_usb_uac_begin(void) {
  uac_mark(2, 10);
  if (s_uac_ready) {
    uac_mark(2, 60);
    return true;
  }

  s_ep_out = 0x01;
  s_ep_fb = 0x81;
  g_pm_uac_ep_out = s_ep_out;
  g_pm_uac_ep_fb = s_ep_fb;
  uac_mark(2, 20);
  uac_build_config_descriptor();
  uac_mark(2, 40);

  uac_prepare_usb_otg_peripheral();
  uac_mark(2, 45);

  const tinyusb_phy_config_t tusb_cfg = {
      .external_phy = false,
  };
  const esp_err_t install_err = tinyusb_driver_install(&tusb_cfg);
  if (install_err != ESP_OK) {
    uac_mark(2, 51, install_err);
    pm_log_printf(false, "uac: TinyUSB install failed err=%d", static_cast<int>(install_err));
    ESP_LOGE(TAG, "TinyUSB driver install failed: %d", static_cast<int>(install_err));
    return false;
  }
  uac_mark(2, 50);

  const BaseType_t task_ok =
      xTaskCreate(tusb_device_task, "uac_tusb", 4096, nullptr, configMAX_PRIORITIES - 1, &s_tusb_task);
  if (task_ok != pdPASS) {
    uac_mark(2, 52, -1);
    pm_log_printf(false, "uac: TinyUSB task create failed");
    ESP_LOGE(TAG, "TinyUSB task create failed");
    return false;
  }

  s_uac_ready = true;
  uac_mark(2, 60);
  pm_log_printf(false, "uac: ready backend=raw-tinyusb hz=%d ep_out=0x%02x ep_fb=0x%02x",
                ASTROLABE_UAC_SAMPLE_HZ, s_ep_out, s_ep_fb);
  ESP_LOGI(TAG, "USB UAC speaker @ %d Hz ep_out=0x%02x ep_fb=0x%02x", ASTROLABE_UAC_SAMPLE_HZ, s_ep_out, s_ep_fb);
  return true;
}

bool pm_usb_uac_ready(void) {
  return s_uac_ready;
}

void pm_usb_uac_release_speaker(void) {
  if (s_pcm_open) {
    pm_speaker_pcm_end();
    s_pcm_open = false;
    pm_log_printf(false, "uac: pcm closed");
  }
  s_host_stream_open = false;
}

bool pm_usb_uac_speaker_active(void) {
  return s_uac_ready && pm_audio_route_output_usb();
}

#elif CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && !CONFIG_TINYUSB_AUDIO_ENABLED && !defined(ASTROLABE_QEMU) && __has_include("usb_device_uac.h")

#include "usb_device_uac.h"

static const char *TAG = "pm_usb_uac";

#ifndef ASTROLABE_UAC_SAMPLE_HZ
#define ASTROLABE_UAC_SAMPLE_HZ 48000
#endif

static bool s_uac_ready = false;
static bool s_pcm_open = false;
static bool s_host_stream_open = false;

static esp_err_t uac_output_cb(uint8_t *buf, size_t len, void *arg) {
  (void)arg;
  if (!buf || len == 0) {
    return ESP_OK;
  }
  if (!s_host_stream_open) {
    s_host_stream_open = true;
    uac_note_speaker_stream_used();
  }
  if (!pm_audio_route_output_usb()) {
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
  uac_mark(3, 10);
  if (s_uac_ready) {
    uac_mark(3, 60);
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
    uac_mark(3, 31, -1);
    ESP_LOGE(TAG, "uac_device_init failed");
    return false;
  }
  s_uac_ready = true;
  uac_mark(3, 60);
  ESP_LOGI(TAG, "USB UAC speaker @ %d Hz", ASTROLABE_UAC_SAMPLE_HZ);
  return true;
}

bool pm_usb_uac_ready(void) {
  return s_uac_ready;
}

void pm_usb_uac_release_speaker(void) {
  if (s_pcm_open) {
    pm_speaker_pcm_end();
    s_pcm_open = false;
  }
  s_host_stream_open = false;
}

bool pm_usb_uac_speaker_active(void) {
  return s_uac_ready && pm_audio_route_output_usb();
}

#elif CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0 && !defined(ASTROLABE_QEMU)

static const char *TAG = "pm_usb_uac";

bool pm_usb_uac_begin(void) {
  uac_mark(4, 99);
  ESP_LOGW(TAG, "UAC enabled but usb_device_uac not available (build waveshare_s3_175_uac, run hybrid once)");
  return false;
}

bool pm_usb_uac_ready(void) {
  return false;
}

void pm_usb_uac_release_speaker(void) {}

bool pm_usb_uac_speaker_active(void) {
  return false;
}

#else

bool pm_usb_uac_begin(void) {
  uac_mark(0, 0);
  return false;
}

bool pm_usb_uac_ready(void) {
  return false;
}

void pm_usb_uac_release_speaker(void) {}

bool pm_usb_uac_speaker_active(void) {
  return false;
}

#endif
