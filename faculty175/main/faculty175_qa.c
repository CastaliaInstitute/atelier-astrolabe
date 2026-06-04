#include "faculty175_qa.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"

#include "faculty175_faculty.h"
#include "faculty175_board_id.h"
#include "faculty175_board.h"

static faculty175_qa_bind_t s_bind = {};
EXT_RAM_BSS_ATTR static int16_t s_audio_stress_in[FACULTY175_LISTEN_FRAME_SAMPLES];
EXT_RAM_BSS_ATTR static int16_t s_audio_stress_out[FACULTY175_LISTEN_FRAME_SAMPLES * 2];
static volatile bool s_audio_busy;

static const char *ui_state_name(faculty175_ui_state_t state)
{
    switch (state) {
        case FACULTY175_UI_BOOT:
            return "boot";
        case FACULTY175_UI_WIFI:
            return "wifi";
        case FACULTY175_UI_LISTEN:
            return "listen";
        case FACULTY175_UI_CAPTURE:
            return "capture";
        case FACULTY175_UI_THINK:
            return "think";
        case FACULTY175_UI_SPEAK:
            return "speak";
        case FACULTY175_UI_ERROR:
            return "error";
        default:
            return "?";
    }
}

static const char *bust_status_name(faculty175_faculty_bust_status_t status)
{
    switch (status) {
        case FACULTY175_FACULTY_BUST_IDLE:
            return "idle";
        case FACULTY175_FACULTY_BUST_LOADING:
            return "loading";
        case FACULTY175_FACULTY_BUST_READY:
            return "ready";
        case FACULTY175_FACULTY_BUST_ERROR:
            return "error";
        default:
            return "?";
    }
}

void faculty175_qa_bind(const faculty175_qa_bind_t *bind)
{
    if (bind == NULL) {
        memset(&s_bind, 0, sizeof(s_bind));
        return;
    }
    s_bind = *bind;
}

bool faculty175_qa_audio_busy(void)
{
    return s_audio_busy;
}

static void qa_print_help(void)
{
    printf("qa commands:\n");
    printf("  qa status   heap, internal heap, audio, lcd, wifi rssi\n");
    printf("  qa ui       current UI state + faculty\n");
    printf("  qa listen   VAD + waveform ring (passive)\n");
    printf("  qa audio    mic probe ~400ms (active read)\n");
    printf("  qa pcm [ms]  base64 raw mono s16le mic capture\n");
    printf("  qa pcm4 [ms]  base64 raw 4-slot ES7210 s16le capture\n");
    printf("  qa speaker [hz] [ms]  audible speaker tone\n");
    printf("  qa audio-stress [sec]  mic read + muted/silent speaker write loop\n");
    printf("  qa faculty [slug [name]]  show or set active faculty\n");
    printf("  qa faculty-fetch <slug> [name]  set faculty and refetch bust\n");
    printf("  qa bust     faculty bust load status\n");
    printf("  qa board    Waveshare module guess + I2C/flash probe\n");
    printf("  qa screen   framebuffer BMP (same as screen)\n");
    fflush(stdout);
}

static void qa_emit_status(void)
{
    int rssi = 0;
    uint8_t ch = 0;
    wifi_ap_record_t ap = {};
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        rssi = (int)ap.rssi;
        ch = ap.primary;
    }

    const uint32_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    printf("qa: face=faculty lcd=%dx%d audio=%s pi4ioe=%s mic_probe_peak=%ld heap=%u internal=%lu largest=%lu psram=%lu wifi_rssi=%d ch=%u\n",
           FACULTY175_LCD_W,
           FACULTY175_LCD_H,
           faculty175_board_audio_ready() ? "ok" : "off",
           faculty175_board_pi4ioe_ok() ? "ok" : "fail",
           (long)faculty175_board_mic_probe_peak(),
           (unsigned)esp_get_free_heap_size(),
           (unsigned long)internal,
           (unsigned long)largest,
           (unsigned long)psram,
           rssi,
           (unsigned)ch);
    fflush(stdout);
}

static void qa_emit_ui(void)
{
    const faculty175_ui_state_t ui = s_bind.ui != NULL ? *s_bind.ui : FACULTY175_UI_BOOT;
    const char *slug = s_bind.faculty_slug != NULL ? s_bind.faculty_slug : "";
    const char *name = s_bind.faculty_name != NULL ? s_bind.faculty_name : "";
    const char *detail = s_bind.ui_detail != NULL ? s_bind.ui_detail : "";

    printf("qa: ui=%s detail=\"%s\" faculty=\"%s\" slug=%s bust=%s\n",
           ui_state_name(ui),
           detail,
           name,
           slug,
           bust_status_name(faculty175_faculty_bust_status()));
    fflush(stdout);
}

static void qa_emit_faculty(void)
{
    const char *slug = s_bind.faculty_slug != NULL ? s_bind.faculty_slug : "";
    const char *name = s_bind.faculty_name != NULL ? s_bind.faculty_name : "";
    printf("qa: faculty=\"%s\" slug=%s bust=%s\n",
           name,
           slug,
           bust_status_name(faculty175_faculty_bust_status()));
    fflush(stdout);
}

static void qa_set_faculty(char *arg, bool force_fetch)
{
    if (arg == NULL || *arg == '\0') {
        qa_emit_faculty();
        return;
    }
    char *slug = arg;
    while (*slug == ' ') {
        ++slug;
    }
    char *name = slug;
    while (*name != '\0' && *name != ' ') {
        ++name;
    }
    if (*name != '\0') {
        *name++ = '\0';
        while (*name == ' ') {
            ++name;
        }
    }
    if (*slug == '\0') {
        qa_emit_faculty();
        return;
    }
    if (name == NULL || *name == '\0') {
        name = slug;
    }
    bool ok = false;
    if (force_fetch && s_bind.fetch_faculty != NULL) {
        ok = s_bind.fetch_faculty(slug, name);
    } else if (s_bind.set_faculty != NULL) {
        ok = s_bind.set_faculty(slug, name);
    }
    if (!ok) {
        printf("qa: faculty set failed slug=%s\n", slug);
        fflush(stdout);
        return;
    }
    qa_emit_faculty();
}

static void qa_emit_listen(void)
{
    faculty175_listen_t *listen = s_bind.listen;
    if (listen == NULL) {
        printf("qa: listen unavailable\n");
        fflush(stdout);
        return;
    }

    uint8_t wave[FACULTY175_LISTEN_WAVEFORM_LEN];
    faculty175_listen_waveform_copy(listen, wave, sizeof(wave));
    uint8_t wave_max = 0;
    uint8_t wave_min = 255;
    uint32_t wave_sum = 0;
    for (size_t i = 0; i < sizeof(wave); ++i) {
        if (wave[i] > wave_max) {
            wave_max = wave[i];
        }
        if (wave[i] < wave_min) {
            wave_min = wave[i];
        }
        wave_sum += wave[i];
    }

    printf("qa: listen speech=%s rms=%u meter=%u wave_min=%u wave_max=%u wave_avg=%u\n",
           faculty175_listen_speech_active(listen) ? "yes" : "no",
           (unsigned)faculty175_listen_last_rms(listen),
           (unsigned)faculty175_listen_meter_level(listen),
           (unsigned)wave_min,
           (unsigned)wave_max,
           (unsigned)(wave_sum / (sizeof(wave) ? sizeof(wave) : 1)));
    fflush(stdout);
}

static void frame_stats(const int16_t *frame, size_t count, int32_t *peak, uint32_t *rms)
{
    uint64_t acc = 0;
    int32_t max_abs = 0;
    for (size_t i = 0; i < count; ++i) {
        const int32_t s = frame[i];
        const int32_t abs_s = s < 0 ? -s : s;
        if (abs_s > max_abs) {
            max_abs = abs_s;
        }
        acc += (uint64_t)(s * s);
    }
    *peak = max_abs;
    *rms = count > 0 ? (uint32_t)(acc / count) : 0;
}

static void frame_centered_stats(const int16_t *frame, size_t count, int32_t *peak, uint32_t *rms)
{
    if (frame == NULL || count == 0) {
        *peak = 0;
        *rms = 0;
        return;
    }
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += frame[i];
    }
    const int32_t mean = (int32_t)(sum / (int64_t)count);
    uint64_t acc = 0;
    int32_t max_abs = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t s = (int32_t)frame[i] - mean;
        if (s < 0) {
            s = -s;
        }
        if (s > max_abs) {
            max_abs = s;
        }
        acc += (uint64_t)(s * s);
    }
    *peak = max_abs;
    *rms = (uint32_t)(acc / count);
}

static void qa_emit_audio(void)
{
    if (!faculty175_board_audio_ready()) {
        printf("qa: audio off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    int16_t frame[FACULTY175_LISTEN_FRAME_SAMPLES];
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    int frames_ok = 0;
    int frames_zero = 0;

    for (int i = 0; i < 12; ++i) {
        size_t got = 0;
        if (faculty175_audio_read(frame, FACULTY175_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
            continue;
        }
        int32_t peak = 0;
        uint32_t rms = 0;
        frame_stats(frame, got, &peak, &rms);
        if (peak == 0 && rms == 0) {
            frames_zero++;
        }
        if (peak > peak_max) {
            peak_max = peak;
        }
        if (rms > rms_max) {
            rms_max = rms;
        }
        frames_ok++;
    }

    printf("qa: audio probe_frames=%d zero_frames=%d peak=%ld rms=%lu\n",
           frames_ok,
           frames_zero,
           (long)peak_max,
           (unsigned long)rms_max);

    faculty175_listen_t *listen = s_bind.listen;
    if (listen != NULL) {
        printf("qa: audio passive rms=%u meter=%u speech=%s\n",
               (unsigned)faculty175_listen_last_rms(listen),
               (unsigned)faculty175_listen_meter_level(listen),
               faculty175_listen_speech_active(listen) ? "yes" : "no");
    }
    fflush(stdout);
}

static void qa_emit_pcm(unsigned ms)
{
    if (ms == 0) {
        ms = 1000;
    }
    if (ms < 100) {
        ms = 100;
    } else if (ms > 3000) {
        ms = 3000;
    }
    if (!faculty175_board_audio_ready()) {
        printf("qa: pcm off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    const size_t sample_count = ((size_t)FACULTY175_AUDIO_RATE * (size_t)ms) / 1000u;
    const size_t pcm_bytes = sample_count * sizeof(int16_t);
    int16_t *pcm = heap_caps_malloc(pcm_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(pcm_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        printf("qa: pcm alloc failed bytes=%u\n", (unsigned)pcm_bytes);
        fflush(stdout);
        return;
    }

    s_audio_busy = true;
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    vTaskDelay(pdMS_TO_TICKS(80));

    size_t written = 0;
    uint32_t read_fail = 0;
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    while (written < sample_count) {
        size_t want = sample_count - written;
        if (want > FACULTY175_LISTEN_FRAME_SAMPLES) {
            want = FACULTY175_LISTEN_FRAME_SAMPLES;
        }
        size_t got = 0;
        if (faculty175_audio_read(pcm + written, want, &got, 200) != ESP_OK || got == 0) {
            read_fail++;
            continue;
        }
        int32_t peak = 0;
        uint32_t rms = 0;
        frame_stats(pcm + written, got, &peak, &rms);
        if (peak > peak_max) {
            peak_max = peak;
        }
        if (rms > rms_max) {
            rms_max = rms;
        }
        written += got;
    }

    const size_t out_cap = ((written * sizeof(int16_t) + 2u) / 3u) * 4u + 1u;
    unsigned char *b64 = heap_caps_malloc(out_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (b64 == NULL) {
        b64 = heap_caps_malloc(out_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (b64 == NULL) {
        printf("qa: pcm b64 alloc failed bytes=%u\n", (unsigned)out_cap);
        fflush(stdout);
        free(pcm);
        s_audio_busy = false;
        return;
    }

    size_t out_len = 0;
    const esp_log_level_t prev = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);
    const int enc = mbedtls_base64_encode(b64, out_cap, &out_len, (const unsigned char *)pcm, written * sizeof(int16_t));
    printf("pcm: BEGIN rate=%u bits=16 channels=1 samples=%u bytes=%u b64=%u peak=%ld rms=%lu read_fail=%lu\n",
           (unsigned)FACULTY175_AUDIO_RATE,
           (unsigned)written,
           (unsigned)(written * sizeof(int16_t)),
           (unsigned)out_len,
           (long)peak_max,
           (unsigned long)rms_max,
           (unsigned long)read_fail);
    fflush(stdout);
    if (enc == 0) {
        fwrite(b64, 1, out_len, stdout);
        printf("\npcm: END\n");
    } else {
        printf("pcm: error base64=%d\n", enc);
    }
    fflush(stdout);
    esp_log_level_set("*", prev);

    free(b64);
    free(pcm);
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    s_audio_busy = false;
}

static void qa_emit_pcm4(unsigned ms)
{
    if (ms == 0) {
        ms = 1000;
    }
    if (ms < 100) {
        ms = 100;
    } else if (ms > 1500) {
        ms = 1500;
    }
    if (!faculty175_board_audio_ready()) {
        printf("qa: pcm4 off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    const size_t frame_count = ((size_t)FACULTY175_AUDIO_RATE * (size_t)ms) / 1000u;
    const size_t sample_count = frame_count * 4u;
    const size_t pcm_bytes = sample_count * sizeof(int16_t);
    int16_t *pcm = heap_caps_malloc(pcm_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pcm == NULL) {
        pcm = heap_caps_malloc(pcm_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pcm == NULL) {
        printf("qa: pcm4 alloc failed bytes=%u\n", (unsigned)pcm_bytes);
        fflush(stdout);
        return;
    }

    s_audio_busy = true;
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    vTaskDelay(pdMS_TO_TICKS(80));

    size_t frames_written = 0;
    uint32_t read_fail = 0;
    int32_t peak_max[4] = {0};
    uint64_t energy[4] = {0};
    while (frames_written < frame_count) {
        size_t want = frame_count - frames_written;
        if (want > FACULTY175_LISTEN_FRAME_SAMPLES) {
            want = FACULTY175_LISTEN_FRAME_SAMPLES;
        }
        size_t got = 0;
        if (faculty175_audio_read_tdm_raw(pcm + frames_written * 4u, want, &got, 200) != ESP_OK || got == 0) {
            read_fail++;
            continue;
        }
        for (size_t i = 0; i < got; ++i) {
            for (int ch = 0; ch < 4; ++ch) {
                const int16_t sample = pcm[(frames_written + i) * 4u + (size_t)ch];
                const int32_t abs_s = sample < 0 ? -(int32_t)sample : (int32_t)sample;
                if (abs_s > peak_max[ch]) {
                    peak_max[ch] = abs_s;
                }
                energy[ch] += (uint64_t)((int32_t)sample * (int32_t)sample);
            }
        }
        frames_written += got;
    }

    const size_t out_cap = ((frames_written * 4u * sizeof(int16_t) + 2u) / 3u) * 4u + 1u;
    unsigned char *b64 = heap_caps_malloc(out_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (b64 == NULL) {
        b64 = heap_caps_malloc(out_cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (b64 == NULL) {
        printf("qa: pcm4 b64 alloc failed bytes=%u\n", (unsigned)out_cap);
        fflush(stdout);
        free(pcm);
        s_audio_busy = false;
        return;
    }

    size_t out_len = 0;
    const esp_log_level_t prev = esp_log_level_get("*");
    esp_log_level_set("*", ESP_LOG_ERROR);
    const int enc = mbedtls_base64_encode(
        b64, out_cap, &out_len, (const unsigned char *)pcm, frames_written * 4u * sizeof(int16_t));
    printf("pcm4: BEGIN rate=%u bits=16 channels=4 frames=%u bytes=%u b64=%u peak=%ld/%ld/%ld/%ld rms=%lu/%lu/%lu/%lu read_fail=%lu\n",
           (unsigned)FACULTY175_AUDIO_RATE,
           (unsigned)frames_written,
           (unsigned)(frames_written * 4u * sizeof(int16_t)),
           (unsigned)out_len,
           (long)peak_max[0],
           (long)peak_max[1],
           (long)peak_max[2],
           (long)peak_max[3],
           (unsigned long)(frames_written ? energy[0] / frames_written : 0),
           (unsigned long)(frames_written ? energy[1] / frames_written : 0),
           (unsigned long)(frames_written ? energy[2] / frames_written : 0),
           (unsigned long)(frames_written ? energy[3] / frames_written : 0),
           (unsigned long)read_fail);
    fflush(stdout);
    if (enc == 0) {
        fwrite(b64, 1, out_len, stdout);
        printf("\npcm4: END\n");
    } else {
        printf("pcm4: error base64=%d\n", enc);
    }
    fflush(stdout);
    esp_log_level_set("*", prev);

    free(b64);
    free(pcm);
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    s_audio_busy = false;
}

static unsigned qa_audio_settle_and_reset(unsigned min_ms, unsigned max_ms, int16_t *drain_frame, int32_t *last_peak, uint32_t *last_rms)
{
    if (min_ms == 0) {
        min_ms = 1;
    }
    if (max_ms < min_ms) {
        max_ms = min_ms;
    }
    const TickType_t start = xTaskGetTickCount();
    const TickType_t min_deadline = start + pdMS_TO_TICKS(min_ms);
    const TickType_t max_deadline = start + pdMS_TO_TICKS(max_ms);
    unsigned quiet_frames = 0;
    int32_t peak = 0;
    uint32_t rms = 0;
    while ((int32_t)(max_deadline - xTaskGetTickCount()) > 0) {
        if (drain_frame != NULL && faculty175_board_audio_ready()) {
            size_t got = 0;
            if (faculty175_audio_read(drain_frame, FACULTY175_LISTEN_FRAME_SAMPLES, &got, 40) == ESP_OK && got > 0) {
                frame_centered_stats(drain_frame, got, &peak, &rms);
                if (rms < FACULTY175_LISTEN_RMS_END && peak < FACULTY175_LISTEN_PEAK_END) {
                    quiet_frames++;
                } else {
                    quiet_frames = 0;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(40));
        }
        if (s_bind.listen != NULL) {
            faculty175_listen_reset(s_bind.listen);
        }
        if ((int32_t)(xTaskGetTickCount() - min_deadline) >= 0 && quiet_frames >= 60) {
            break;
        }
    }
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    if (last_peak != NULL) {
        *last_peak = peak;
    }
    if (last_rms != NULL) {
        *last_rms = rms;
    }
    return (unsigned)((xTaskGetTickCount() - start) * portTICK_PERIOD_MS);
}

static void qa_audio_stress(unsigned seconds)
{
    if (seconds == 0) {
        seconds = 10;
    }
    if (seconds > 120) {
        seconds = 120;
    }
    if (!faculty175_board_audio_ready()) {
        printf("qa: audio-stress off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    int16_t *in = s_audio_stress_in;
    int16_t *out = s_audio_stress_out;
    const size_t tx_samples = FACULTY175_LISTEN_FRAME_SAMPLES * 2;
    const uint32_t total_frames = (seconds * FACULTY175_AUDIO_RATE) / FACULTY175_LISTEN_FRAME_SAMPLES;
    uint32_t frames_read = 0;
    uint32_t frames_write = 0;
    uint32_t read_fail = 0;
    uint32_t write_fail = 0;
    uint32_t zero_frames = 0;
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    uint32_t min_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t min_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const uint32_t psram_start = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    s_audio_busy = true;
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    vTaskDelay(pdMS_TO_TICKS(60));

    faculty175_audio_set_speaker_mute(true);
    memset(out, 0, tx_samples * sizeof(out[0]));
    printf("qa: audio-stress begin sec=%u frames=%lu muted=1 silent=1 heap=%lu largest=%lu psram=%lu\n",
           seconds,
           (unsigned long)total_frames,
           (unsigned long)min_internal,
           (unsigned long)min_largest,
           (unsigned long)psram_start);
    fflush(stdout);

    for (uint32_t frame_idx = 0; frame_idx < total_frames; ++frame_idx) {
        size_t got = 0;
        if (faculty175_audio_read(in, FACULTY175_LISTEN_FRAME_SAMPLES, &got, 120) == ESP_OK && got > 0) {
            int32_t peak = 0;
            uint32_t rms = 0;
            frame_stats(in, got, &peak, &rms);
            if (peak == 0 && rms == 0) {
                zero_frames++;
            }
            if (peak > peak_max) {
                peak_max = peak;
            }
            if (rms > rms_max) {
                rms_max = rms;
            }
            frames_read++;
        } else {
            read_fail++;
        }

        if (faculty175_audio_write_pcm(out, tx_samples, 120) == ESP_OK) {
            frames_write++;
        } else {
            write_fail++;
        }

        const uint32_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (free_internal < min_internal) {
            min_internal = free_internal;
        }
        if (largest < min_largest) {
            min_largest = largest;
        }
        if ((frame_idx % 50u) == 49u) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    printf("qa: audio-stress end read=%lu write=%lu read_fail=%lu write_fail=%lu zero=%lu peak=%ld rms=%lu heap_min=%lu largest_min=%lu psram_delta=%ld\n",
           (unsigned long)frames_read,
           (unsigned long)frames_write,
           (unsigned long)read_fail,
           (unsigned long)write_fail,
           (unsigned long)zero_frames,
           (long)peak_max,
           (unsigned long)rms_max,
           (unsigned long)min_internal,
           (unsigned long)min_largest,
           (long)((int32_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM) - (int32_t)psram_start));
    fflush(stdout);

    const esp_err_t audio_reset = faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE);
    printf("qa: audio-stress audio_reset=%s\n", esp_err_to_name(audio_reset));
    fflush(stdout);
    faculty175_audio_set_speaker_mute(true);
    vTaskDelay(pdMS_TO_TICKS(120));
    int32_t settle_peak = 0;
    uint32_t settle_rms = 0;
    const unsigned settle_ms = qa_audio_settle_and_reset(3000, 15000, in, &settle_peak, &settle_rms);
    s_audio_busy = false;
    printf("qa: audio-stress settled vad=muted_ms=%u peak=%ld rms=%lu\n",
           settle_ms,
           (long)settle_peak,
           (unsigned long)settle_rms);
    fflush(stdout);
}

static void qa_emit_speaker_tone(unsigned hz, unsigned ms)
{
    if (hz == 0) {
        hz = 880;
    }
    if (hz > 4000) {
        hz = 4000;
    }
    if (ms == 0) {
        ms = 900;
    }
    if (ms > 5000) {
        ms = 5000;
    }
    if (!faculty175_board_audio_ready()) {
        printf("qa: speaker off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    s_audio_busy = true;
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    esp_err_t err = faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE);
    if (err != ESP_OK) {
        printf("qa: speaker set_rate=%s\n", esp_err_to_name(err));
        fflush(stdout);
        s_audio_busy = false;
        return;
    }

    int16_t *out = s_audio_stress_out;
    const uint32_t total_frames = (ms * FACULTY175_AUDIO_RATE) / 1000u;
    const uint32_t step = (uint32_t)(((uint64_t)hz << 16) / FACULTY175_AUDIO_RATE);
    uint32_t phase = 0;
    uint32_t written_frames = 0;
    uint32_t write_fail = 0;

    printf("qa: speaker begin hz=%u ms=%u muted=0\n", hz, ms);
    fflush(stdout);
    faculty175_audio_set_speaker_mute(false);

    while (written_frames < total_frames) {
        uint32_t chunk = total_frames - written_frames;
        if (chunk > FACULTY175_LISTEN_FRAME_SAMPLES) {
            chunk = FACULTY175_LISTEN_FRAME_SAMPLES;
        }
        for (uint32_t i = 0; i < chunk; ++i) {
            const int16_t v = (phase & 0x8000u) ? 9000 : -9000;
            out[i * 2] = v;
            out[i * 2 + 1] = v;
            phase += step;
        }
        if (faculty175_audio_write_pcm(out, (size_t)chunk * 2u, 500) != ESP_OK) {
            write_fail++;
        }
        written_frames += chunk;
        vTaskDelay(1);
    }

    printf("qa: speaker end frames=%lu write_fail=%lu\n",
           (unsigned long)written_frames,
           (unsigned long)write_fail);
    fflush(stdout);
    err = faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE);
    printf("qa: speaker audio_reset=%s\n", esp_err_to_name(err));
    fflush(stdout);
    if (s_bind.listen != NULL) {
        faculty175_listen_reset(s_bind.listen);
    }
    s_audio_busy = false;
}

static void qa_emit_bust(void)
{
    const faculty175_faculty_bust_status_t status = faculty175_faculty_bust_status();
    const char *loaded = faculty175_faculty_loaded_slug();
    if (loaded == NULL || loaded[0] == '\0') {
        loaded = "-";
    }

    printf("qa: bust status=%s loaded=%s size=%dx%d\n",
           bust_status_name(status),
           loaded,
           FACULTY175_FACULTY_BUST_W,
           FACULTY175_FACULTY_BUST_H);
    fflush(stdout);
}

static void qa_emit_board(void)
{
    const faculty175_board_identity_t *id = faculty175_board_identity();
    printf("qa: board guess=%s project=%s flash=%uMB cfg_flash=",
           faculty175_board_guess_name(id->guess),
           faculty175_board_recommended_project(),
           (unsigned)id->flash_mb);
#if CONFIG_ESPTOOLPY_FLASHSIZE_32MB
    printf("32MB");
#elif CONFIG_ESPTOOLPY_FLASHSIZE_16MB
    printf("16MB");
#else
    printf("?");
#endif
    printf(" psram=%uMB\n", (unsigned)id->psram_mb);
    printf("qa: board mac=%02x:%02x:%02x:%02x:%02x:%02x i2c axp=%d tca9554=%d es7210=%d es8311=%d touch=%d\n",
           id->mac[0],
           id->mac[1],
           id->mac[2],
           id->mac[3],
           id->mac[4],
           id->mac[5],
           id->axp2101,
           id->tca9554,
           id->es7210,
           id->es8311,
           id->cst9217);
    if (id->flash_config_mismatch) {
        printf("qa: board WARN sdkconfig flash size does not match chip\n");
    }
    if (id->guess == FACULTY175_GUESS_18_WRONG_FW) {
        printf("qa: board ERROR use ./scripts/faculty18_build.sh for the 1.8\" module\n");
    }
    fflush(stdout);
}

bool faculty175_qa_handle(const char *line)
{
    if (line == NULL) {
        return false;
    }
    if (strcasecmp(line, "qa") != 0 && strncasecmp(line, "qa ", 3) != 0) {
        return false;
    }

    const char *sub = line + 2;
    while (*sub == ' ') {
        ++sub;
    }
    if (*sub == '\0' || strcasecmp(sub, "help") == 0) {
        qa_print_help();
        return true;
    }
    if (strncasecmp(sub, "faculty-fetch", 13) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", sub + 13);
        char *arg = buf;
        while (*arg == ' ') {
            ++arg;
        }
        qa_set_faculty(arg, true);
        return true;
    }
    if (strncasecmp(sub, "faculty", 7) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", sub + 7);
        char *arg = buf;
        while (*arg == ' ') {
            ++arg;
        }
        qa_set_faculty(arg, false);
        return true;
    }
    if (strcasecmp(sub, "status") == 0) {
        qa_emit_status();
        return true;
    }
    if (strcasecmp(sub, "ui") == 0) {
        qa_emit_ui();
        return true;
    }
    if (strcasecmp(sub, "listen") == 0) {
        qa_emit_listen();
        return true;
    }
    if (strcasecmp(sub, "audio") == 0) {
        qa_emit_audio();
        return true;
    }
    if (strncasecmp(sub, "pcm4", 4) == 0) {
        const char *arg = sub + 4;
        while (*arg == ' ') {
            ++arg;
        }
        qa_emit_pcm4((unsigned)strtoul(arg, NULL, 10));
        return true;
    }
    if (strncasecmp(sub, "pcm", 3) == 0) {
        const char *arg = sub + 3;
        while (*arg == ' ') {
            ++arg;
        }
        qa_emit_pcm((unsigned)strtoul(arg, NULL, 10));
        return true;
    }
    if (strncasecmp(sub, "speaker", 7) == 0) {
        char *end = NULL;
        const char *arg = sub + 7;
        while (*arg == ' ') {
            ++arg;
        }
        unsigned hz = (unsigned)strtoul(arg, &end, 10);
        while (end != NULL && *end == ' ') {
            ++end;
        }
        unsigned ms = end != NULL ? (unsigned)strtoul(end, NULL, 10) : 0;
        qa_emit_speaker_tone(hz, ms);
        return true;
    }
    if (strncasecmp(sub, "audio-stress", 12) == 0) {
        const char *arg = sub + 12;
        while (*arg == ' ') {
            ++arg;
        }
        qa_audio_stress((unsigned)strtoul(arg, NULL, 10));
        return true;
    }
    if (strcasecmp(sub, "bust") == 0) {
        qa_emit_bust();
        return true;
    }
    if (strcasecmp(sub, "board") == 0) {
        qa_emit_board();
        return true;
    }

    printf("qa: unknown subcommand \"%s\" (try: qa help)\n", sub);
    fflush(stdout);
    return true;
}
