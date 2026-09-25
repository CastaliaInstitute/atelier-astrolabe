/*
 * BLE Audio Bridge (device peripheral) — contract: docs/design/ble-audio-bridge.md
 *
 * One Audio primary service with five characteristics:
 *   0x11 mic audio data  READ+NOTIFY      self-contained chunks device -> app
 *   0x12 tts audio data  WRITE_WO_RSP     chunks app -> device (spec 3 fills playout)
 *   0x13 codec           READ             1-byte active codec id
 *   0x14 stream control  READ+WRITE       0x01 epoch / 0x02 {"mic":0|1}; read = state JSON
 *   0x15 buffer credit   READ+NOTIFY      u32 LE free bytes of the 16 kB TTS ring
 *
 * Shipped byte-identically in astrolabe175c/main and astrolabe185b/main.
 */
#include "faculty175_ble_audio.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"

#include "faculty175_board.h"

static const char *TAG = "faculty175_ble_audio";

/* Codec rows per contract §5 */
enum { BLEA_CODEC_PCM16_16K = 0, BLEA_CODEC_ULAW_16K = 10, BLEA_CODEC_ULAW_8K = 11 };
enum { BLEA_MIN_MTU_ROW0 = 332, BLEA_MIN_MTU_ROW10 = 172, BLEA_MIN_MTU_ROW11 = 92 };
enum { BLEA_PCM16_SAMPLES_PER_CHUNK = 160 };
enum { BLEA_TTS_RING_BYTES = 16384 };
enum { BLEA_MIC_TASK_STACK = 3072, BLEA_MIC_TASK_PRIO = 5 };

typedef struct {
    volatile uint16_t conn;    /* audio-capable connection handle, 0 = none */
    volatile uint16_t mtu;
    volatile uint8_t codec;    /* BLEA_CODEC_* */
    volatile bool mic_on;      /* app sent {"mic":1} */
    volatile bool ring;        /* Colmi window open -> pause chunking */
    volatile uint32_t backoff_ms; /* adaptive tx throttle (no per-notify cb in NimBLE gatts) */
    /* QA counters */
    volatile uint32_t chunks_sent;
    volatile uint32_t dropped_notify;
    volatile uint32_t seq_wraps;
} blea_state_t;

static blea_state_t s_state;
static faculty175_ble_audio_hooks_t s_hooks;
static TaskHandle_t s_mic_task;
static bool s_task_started;

const ble_uuid128_t faculty175_ble_audio_svc_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x10);
static const ble_uuid128_t k_chr_mic_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x11);
static const ble_uuid128_t k_chr_tts_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x12);
static const ble_uuid128_t k_chr_codec_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x13);
static const ble_uuid128_t k_chr_control_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x14);
static const ble_uuid128_t k_chr_credit_uuid = BLE_UUID128_INIT(
    0x41, 0x73, 0x74, 0x72, 0x6f, 0x6c, 0x61, 0x62, 0x65, 0x00, 0x17, 0x50, 0x00, 0x00, 0x00, 0x15);

static uint16_t s_chr_mic_handle;
static uint16_t s_chr_credit_handle;

/* ------------------------------------------------------------------ µ-law */

static uint16_t s_seq;
static portMUX_TYPE s_seq_lock = portMUX_INITIALIZER_UNLOCKED;

static uint16_t blea_seq_next(void)
{
    uint16_t value;
    portENTER_CRITICAL(&s_seq_lock);
    value = s_seq;
    s_seq = (uint16_t)(s_seq + 1u);
    if (s_seq == 0) {
        s_state.seq_wraps++;
    }
    portEXIT_CRITICAL(&s_seq_lock);
    return value;
}

/* Classic telephony µ-law compander for 16-bit linear input (compute, no LUT). */
static uint8_t blea_ulaw16(int16_t pcm)
{
    const int16_t bias = 0x84;
    const uint8_t seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};
    uint16_t sign = (pcm < 0) ? 0x7F : 0x00;
    int32_t mag = pcm < 0 ? -(int32_t)pcm : (int32_t)pcm;
    if (mag > 32767) {
        mag = 32767;
    }
    mag += bias;
    const int32_t mask = mag >> 8;
    int seg = 0;
    while (seg < 8 && mag > seg_end[seg]) {
        seg++;
    }
    if (seg >= 8) {
        seg = 8;
    }
    return (uint8_t)(sign | ((uint8_t)(seg << 4)) | ((mag >> (seg + 3)) & 0x0F));
}

static uint8_t blea_ulaw8(int16_t sample)
{
    /* Caller downsamples by taking every other 16k sample; same compander. */
    return blea_ulaw16(sample);
}

/* --------------------------------------------------------------- access cbs */

static int blea_chr_mic_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        static const uint8_t zero = 0;
        return os_mbuf_append(ctxt->om, &zero, sizeof(zero)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int blea_chr_tts_access(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    /* Spec 3 wires playout; until then consume and count. */
    return 0;
}

static int blea_chr_codec_access(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    const uint8_t codec = (uint8_t)s_state.codec;
    return os_mbuf_append(ctxt->om, &codec, sizeof(codec)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int blea_chr_control_access(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t msg[1 + 60];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len < 1 || len > sizeof(msg)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (ble_hs_mbuf_to_flat(ctxt->om, msg, sizeof(msg), &len) != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        if (msg[0] == 0x01 && len == 5) {
            const uint32_t epoch = (uint32_t)msg[1] | ((uint32_t)msg[2] << 8) |
                                   ((uint32_t)msg[3] << 16) | ((uint32_t)msg[4] << 24);
            if (s_hooks.time_set != NULL) {
                s_hooks.time_set(epoch);
            }
            ESP_LOGI(TAG, "time sync epoch=%" PRIu32, epoch);
            return 0;
        }
        if (msg[0] == 0x02) {
            const char *json = (const char *)msg + 1;
            const bool on = strstr(json, "\"mic\":1") != NULL ||
                            strstr(json, "\"mic\": true") != NULL;
            s_state.mic_on = on;
            ESP_LOGI(TAG, "mic %s", on ? "on" : "off");
            return 0;
        }
        return BLE_ATT_ERR_REQ_NOT_SUPPORTED;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        char json[112];
        const int len = snprintf(json,
                                 sizeof(json),
                                 "{\"arb\":\"%s\",\"mic\":%d,\"codec\":%u,\"mtu\":%u}",
                                 s_state.ring ? "ring" : "free",
                                 (int)s_state.mic_on,
                                 (unsigned)s_state.codec,
                                 (unsigned)s_state.mtu);
        if (len <= 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        return os_mbuf_append(ctxt->om, json, (uint16_t)len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_READ_NOT_PERMITTED;
}

static int blea_chr_credit_access(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    const uint32_t credit = BLEA_TTS_RING_BYTES; /* full ring until spec 3 */
    const uint8_t le[4] = {(uint8_t)(credit & 0xFF),
                           (uint8_t)((credit >> 8) & 0xFF),
                           (uint8_t)((credit >> 16) & 0xFF),
                           (uint8_t)((credit >> 24) & 0xFF)};
    return os_mbuf_append(ctxt->om, le, sizeof(le)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

const struct ble_gatt_chr_def faculty175_ble_audio_chr_defs[] = {
        {
            .uuid = &k_chr_mic_uuid.u,
            .access_cb = blea_chr_mic_access,
            .arg = NULL,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &s_chr_mic_handle,
        },
        {
            .uuid = &k_chr_tts_uuid.u,
            .access_cb = blea_chr_tts_access,
            .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
        },
        {
            .uuid = &k_chr_codec_uuid.u,
            .access_cb = blea_chr_codec_access,
            .flags = BLE_GATT_CHR_F_READ,
        },
        {
            .uuid = &k_chr_control_uuid.u,
            .access_cb = blea_chr_control_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
        },
        {
            .uuid = &k_chr_credit_uuid.u,
            .access_cb = blea_chr_credit_access,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &s_chr_credit_handle,
        },
    {0},
};

/* ------------------------------------------------------------------ capture */

static int blea_codec_row(void)
{
    const uint16_t mtu = s_state.mtu;
    if (mtu >= BLEA_MIN_MTU_ROW0) {
        return BLEA_CODEC_PCM16_16K;
    }
    if (mtu >= BLEA_MIN_MTU_ROW10) {
        return BLEA_CODEC_ULAW_16K;
    }
    (void)BLEA_MIN_MTU_ROW11;
    return BLEA_CODEC_ULAW_8K;
}

static void blea_negotiate(void)
{
    s_state.codec = (uint8_t)blea_codec_row();
    ESP_LOGI(TAG, "codec row %u (mtu=%u)", (unsigned)s_state.codec, (unsigned)s_state.mtu);
}

static bool blea_send_chunk(uint16_t conn, const uint8_t *payload, size_t len)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, len);
    if (om == NULL) {
        s_state.dropped_notify++;
        s_state.backoff_ms += 4;
        if (s_state.backoff_ms > 60) {
            s_state.backoff_ms = 60;
        }
        return false;
    }
    const int rc = ble_gatts_notify_custom(conn, s_chr_mic_handle, om);
    if (rc != 0) {
        os_mbuf_free_chain(om);
        s_state.dropped_notify++;
        s_state.backoff_ms += 4;
        if (s_state.backoff_ms > 60) {
            s_state.backoff_ms = 60;
        }
        return false;
    }
    s_state.chunks_sent++;
    if (s_state.backoff_ms > 0) {
        s_state.backoff_ms /= 2;
    }
    return true;
}

static void blea_send_frame(const int16_t *pcm, size_t samples)
{
    const bool subscribed = s_state.conn != 0 && s_chr_mic_handle != 0;
    if (!subscribed || s_state.ring) {
        return;
    }
    uint8_t chunk[4 + 324];
    size_t payload_len;
    switch (blea_codec_row()) {
        case BLEA_CODEC_ULAW_8K: { /* 80 samples @8k from 160 @16k (decimate) */
            for (size_t i = 0; i < 80; ++i) {
                chunk[4 + i] = blea_ulaw8(pcm[i * 2]);
            }
            payload_len = 80;
            break;
        }
        case BLEA_CODEC_ULAW_16K: {
            for (size_t i = 0; i < samples; ++i) {
                chunk[4 + i] = blea_ulaw16(pcm[i]);
            }
            payload_len = samples;
            break;
        }
        default: {
            payload_len = samples * sizeof(uint16_t);
            memcpy(chunk + 4, pcm, payload_len);
            break;
        }
    }
    const uint16_t seq = blea_seq_next();
    chunk[0] = (uint8_t)(seq & 0xFF);
    chunk[1] = (uint8_t)((seq >> 8) & 0xFF);
    chunk[2] = blea_codec_row();
    chunk[3] = 0;
    const uint16_t mtu = s_state.mtu ? s_state.mtu : 23;
    if (4 + payload_len > (size_t)(mtu - 3)) {
        s_state.dropped_notify++;
        return;
    }
    blea_send_chunk(s_state.conn, chunk, 4 + payload_len);
}

static void blea_mic_task(void *arg)
{
    (void)arg;
    int16_t pcm[BLEA_PCM16_SAMPLES_PER_CHUNK];
    size_t got;
    for (;;) {
        if (!faculty175_board_audio_ready()) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!s_state.mic_on || s_state.conn == 0) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (faculty175_audio_read(pcm, BLEA_PCM16_SAMPLES_PER_CHUNK, &got, 60) != ESP_OK || got == 0) {
            continue;
        }
        /* flash-ring route on link loss: live frames keep streaming on the
         * app-facing path; frames captured while the peer is absent spool. */
        if (s_state.conn == 0 && s_hooks.spool_pcm != NULL) {
            s_hooks.spool_pcm(pcm, got);
        }
        blea_send_frame(pcm, got);
        if (s_state.backoff_ms != 0) {
            vTaskDelay(pdMS_TO_TICKS(s_state.backoff_ms));
        }
        taskYIELD();
    }
}

/* ------------------------------------------------------------------- public */

void faculty175_ble_audio_hooks(const faculty175_ble_audio_hooks_t *hooks)
{
    if (hooks != NULL) {
        s_hooks = *hooks;
    }
}

void faculty175_ble_audio_set_ring(bool on)
{
    s_state.ring = on;
}

void faculty175_ble_audio_gap(const struct ble_gap_event *event)
{
    if (event == NULL) {
        return;
    }
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                s_state.conn = event->connect.conn_handle;
                s_state.mtu = ble_att_mtu(event->connect.conn_handle);
                blea_negotiate();
            }
            break;
        }
        case BLE_GAP_EVENT_DISCONNECT: {
            if (s_state.conn == event->disconnect.conn.conn_handle) {
                s_state.conn = 0;
                s_state.mic_on = false;
                s_state.mtu = 0;
            }
            break;
        }
        case BLE_GAP_EVENT_MTU: {
            if (s_state.conn == event->mtu.conn_handle) {
                s_state.mtu = event->mtu.value;
                blea_negotiate();
            }
            break;
        }
        case BLE_GAP_EVENT_SUBSCRIBE: {
            if (event->subscribe.attr_handle == s_chr_mic_handle) {
                if (event->subscribe.cur_notify) {
                    s_state.conn = event->subscribe.conn_handle;
                    s_state.mtu = ble_att_mtu(event->subscribe.conn_handle);
                    blea_negotiate();
                    ESP_LOGI(TAG, "mic subscribed conn=%u", (unsigned)s_state.conn);
                } else if (s_state.conn == event->subscribe.conn_handle) {
                    s_state.conn = 0;
                    ESP_LOGI(TAG, "mic unsubscribed");
                }
            }
            break;
        }
        default:
            break;
    }
}

void faculty175_ble_audio_qa(void)
{
    printf("blemic: conn=%u mtu=%u codec=%u mic=%u ring=%u\n",
           (unsigned)s_state.conn,
           (unsigned)s_state.mtu,
           (unsigned)s_state.codec,
           (int)s_state.mic_on,
           (int)s_state.ring);
    printf("blemic: chunks_sent=%lu dropped_notify=%lu seq_wraps=%lu\n",
           (unsigned long)s_state.chunks_sent,
           (unsigned long)s_state.dropped_notify,
           (unsigned long)s_state.seq_wraps);
}

bool faculty175_ble_audio_start(void)
{
    if (s_task_started) {
        return true;
    }
    s_task_started = true;
    const BaseType_t rc = xTaskCreate(blea_mic_task, "blea_mic", BLEA_MIC_TASK_STACK, NULL,
                                      BLEA_MIC_TASK_PRIO, &s_mic_task);
    if (rc != pdPASS) {
        s_task_started = false;
        ESP_LOGE(TAG, "mic task spawn failed rc=%d", (int)rc);
        return false;
    }
    return true;
}
