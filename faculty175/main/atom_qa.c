#include "atom_qa.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "atom_faculty.h"
#include "faculty175_board_id.h"
#include "faculty175_board.h"

static atom_qa_bind_t s_bind = {};

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

static const char *bust_status_name(atom_faculty_bust_status_t status)
{
    switch (status) {
        case ATOM_FACULTY_BUST_IDLE:
            return "idle";
        case ATOM_FACULTY_BUST_LOADING:
            return "loading";
        case ATOM_FACULTY_BUST_READY:
            return "ready";
        case ATOM_FACULTY_BUST_ERROR:
            return "error";
        default:
            return "?";
    }
}

void atom_qa_bind(const atom_qa_bind_t *bind)
{
    if (bind == NULL) {
        memset(&s_bind, 0, sizeof(s_bind));
        return;
    }
    s_bind = *bind;
}

static void qa_print_help(void)
{
    printf("qa commands:\n");
    printf("  qa status   heap, audio, lcd, wifi rssi\n");
    printf("  qa ui       current UI state + faculty\n");
    printf("  qa listen   VAD + waveform ring (passive)\n");
    printf("  qa audio    mic probe ~400ms (active read)\n");
    printf("  qa audio-stress [sec]  mic read + speaker tone loop\n");
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

    printf("qa: face=faculty lcd=%dx%d audio=%s pi4ioe=%s mic_probe_peak=%ld heap=%u psram=%u wifi_rssi=%d ch=%u\n",
           FACULTY175_LCD_W,
           FACULTY175_LCD_H,
           faculty175_board_audio_ready() ? "ok" : "off",
           faculty175_board_pi4ioe_ok() ? "ok" : "fail",
           (long)faculty175_board_mic_probe_peak(),
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
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
           bust_status_name(atom_faculty_bust_status()));
    fflush(stdout);
}

static void qa_emit_listen(void)
{
    atom_listen_t *listen = s_bind.listen;
    if (listen == NULL) {
        printf("qa: listen unavailable\n");
        fflush(stdout);
        return;
    }

    uint8_t wave[ATOM_LISTEN_WAVEFORM_LEN];
    atom_listen_waveform_copy(listen, wave, sizeof(wave));
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
           atom_listen_speech_active(listen) ? "yes" : "no",
           (unsigned)atom_listen_last_rms(listen),
           (unsigned)atom_listen_meter_level(listen),
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

static void qa_emit_audio(void)
{
    if (!faculty175_board_audio_ready()) {
        printf("qa: audio off (pi4ioe=%s mic_probe_peak=%ld)\n",
               faculty175_board_pi4ioe_ok() ? "ok" : "fail",
               (long)faculty175_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    int16_t frame[ATOM_LISTEN_FRAME_SAMPLES];
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    int frames_ok = 0;
    int frames_zero = 0;

    for (int i = 0; i < 12; ++i) {
        size_t got = 0;
        if (faculty175_audio_read(frame, ATOM_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
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

    atom_listen_t *listen = s_bind.listen;
    if (listen != NULL) {
        printf("qa: audio passive rms=%u meter=%u speech=%s\n",
               (unsigned)atom_listen_last_rms(listen),
               (unsigned)atom_listen_meter_level(listen),
               atom_listen_speech_active(listen) ? "yes" : "no");
    }
    fflush(stdout);
}

static int16_t stress_tone_sample(uint32_t *phase)
{
    const uint32_t step = (uint32_t)(((uint64_t)440 * UINT32_MAX) / FACULTY175_AUDIO_RATE);
    *phase += step;
    const uint32_t p = *phase >> 16;
    const int32_t tri = p < 32768 ? (int32_t)p : (int32_t)(65535u - p);
    return (int16_t)(((tri - 16384) * 1800) / 16384);
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

    int16_t *in = heap_caps_malloc(ATOM_LISTEN_FRAME_SAMPLES * sizeof(int16_t),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    int16_t *out = heap_caps_malloc(ATOM_LISTEN_FRAME_SAMPLES * sizeof(int16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (in == NULL || out == NULL) {
        free(in);
        free(out);
        printf("qa: audio-stress alloc failed heap=%lu largest=%lu\n",
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        fflush(stdout);
        return;
    }
    const uint32_t total_frames = (seconds * FACULTY175_AUDIO_RATE) / ATOM_LISTEN_FRAME_SAMPLES;
    uint32_t phase = 0;
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

    faculty175_audio_set_speaker_mute(false);
    printf("qa: audio-stress begin sec=%u frames=%lu heap=%lu largest=%lu psram=%lu\n",
           seconds,
           (unsigned long)total_frames,
           (unsigned long)min_internal,
           (unsigned long)min_largest,
           (unsigned long)psram_start);
    fflush(stdout);

    for (uint32_t frame_idx = 0; frame_idx < total_frames; ++frame_idx) {
        for (size_t i = 0; i < ATOM_LISTEN_FRAME_SAMPLES; ++i) {
            out[i] = stress_tone_sample(&phase);
        }

        size_t got = 0;
        if (faculty175_audio_read(in, ATOM_LISTEN_FRAME_SAMPLES, &got, 120) == ESP_OK && got > 0) {
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

        if (faculty175_audio_write_pcm(out, ATOM_LISTEN_FRAME_SAMPLES, 120) == ESP_OK) {
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
    free(in);
    free(out);
    fflush(stdout);
}

static void qa_emit_bust(void)
{
    const atom_faculty_bust_status_t status = atom_faculty_bust_status();
    const char *loaded = atom_faculty_loaded_slug();
    if (loaded == NULL || loaded[0] == '\0') {
        loaded = "-";
    }

    printf("qa: bust status=%s loaded=%s size=%dx%d\n",
           bust_status_name(status),
           loaded,
           ATOM_FACULTY_BUST_W,
           ATOM_FACULTY_BUST_H);
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

bool atom_qa_handle(const char *line)
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
