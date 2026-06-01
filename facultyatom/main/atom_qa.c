#include "atom_qa.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"

#include "atom_faculty.h"

static atom_qa_bind_t s_bind = {};

static const char *ui_state_name(atom_ui_state_t state)
{
    switch (state) {
        case ATOM_UI_BOOT:
            return "boot";
        case ATOM_UI_WIFI:
            return "wifi";
        case ATOM_UI_LISTEN:
            return "listen";
        case ATOM_UI_CAPTURE:
            return "capture";
        case ATOM_UI_THINK:
            return "think";
        case ATOM_UI_SPEAK:
            return "speak";
        case ATOM_UI_ERROR:
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
    printf("  qa bust     faculty bust load status\n");
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
           ATOM_LCD_W,
           ATOM_LCD_H,
           atom_board_audio_ready() ? "ok" : "off",
           atom_board_pi4ioe_ok() ? "ok" : "fail",
           (long)atom_board_mic_probe_peak(),
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           rssi,
           (unsigned)ch);
    fflush(stdout);
}

static void qa_emit_ui(void)
{
    const atom_ui_state_t ui = s_bind.ui != NULL ? *s_bind.ui : ATOM_UI_BOOT;
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
    if (!atom_board_audio_ready()) {
        printf("qa: audio off (pi4ioe=%s mic_probe_peak=%ld)\n",
               atom_board_pi4ioe_ok() ? "ok" : "fail",
               (long)atom_board_mic_probe_peak());
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
        if (atom_audio_read(frame, ATOM_LISTEN_FRAME_SAMPLES, &got, 100) != ESP_OK || got == 0) {
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
    if (strcasecmp(sub, "bust") == 0) {
        qa_emit_bust();
        return true;
    }

    printf("qa: unknown subcommand \"%s\" (try: qa help)\n", sub);
    fflush(stdout);
    return true;
}
