#include "paper_qa.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_wifi.h"

#include "paper_faculty.h"
#include "paper_memory.h"

static paper_qa_bind_t s_bind = {};

static const char *ui_state_name(paper_ui_state_t state)
{
    switch (state) {
        case PAPER_UI_BOOT:
            return "boot";
        case PAPER_UI_WIFI:
            return "wifi";
        case PAPER_UI_LISTEN:
            return "listen";
        case PAPER_UI_CAPTURE:
            return "capture";
        case PAPER_UI_THINK:
            return "think";
        case PAPER_UI_SPEAK:
            return "speak";
        case PAPER_UI_ERROR:
            return "error";
        default:
            return "?";
    }
}

static const char *bust_status_name(paper_faculty_bust_status_t status)
{
    switch (status) {
        case PAPER_FACULTY_BUST_IDLE:
            return "idle";
        case PAPER_FACULTY_BUST_LOADING:
            return "loading";
        case PAPER_FACULTY_BUST_READY:
            return "ready";
        case PAPER_FACULTY_BUST_ERROR:
            return "error";
        default:
            return "?";
    }
}

void paper_qa_bind(const paper_qa_bind_t *bind)
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
    printf("  qa listen   pipeline VAD state (passive)\n");
    printf("  qa audio    mic probe ~400ms (active read)\n");
    printf("  qa audio-raw raw I2S RX probe (bypasses codec data_if)\n");
    printf("  qa codec    selected ES7210 register dump\n");
    printf("  qa speaker  play a short 440 Hz square-wave tone\n");
    printf("  qa bust     faculty bust load status\n");
    printf("  qa memory   conversation/note storage paths\n");
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

    printf("qa: face=faculty lcd=%dx%d audio=%s pm1=%s sd=%s memory=%s mic_probe_peak=%ld heap=%u psram=%u wifi_rssi=%d ch=%u\n",
           PAPER_LCD_W,
           PAPER_LCD_H,
           paper_board_audio_ready() ? "ok" : "off",
           paper_board_pi4ioe_ok() ? "ok" : "off",
           paper_board_sd_ready() ? "ok" : "fallback",
           paper_memory_ready() ? "ok" : "off",
           (long)paper_board_mic_probe_peak(),
           (unsigned)esp_get_free_heap_size(),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           rssi,
           (unsigned)ch);
    fflush(stdout);
}

static void qa_emit_memory(void)
{
    printf("qa: memory=%s root=%s conversations=%s notes=%s\n",
           paper_memory_ready() ? "ok" : "off",
           paper_memory_root(),
           paper_memory_conversation_path(),
           paper_memory_notes_path());
    fflush(stdout);
}

static void qa_emit_ui(void)
{
    const paper_ui_state_t ui = s_bind.ui != NULL ? *s_bind.ui : PAPER_UI_BOOT;
    const char *slug = s_bind.faculty_slug != NULL ? s_bind.faculty_slug : "";
    const char *name = s_bind.faculty_name != NULL ? s_bind.faculty_name : "";
    const char *detail = s_bind.ui_detail != NULL ? s_bind.ui_detail : "";

    printf("qa: ui=%s detail=\"%s\" faculty=\"%s\" slug=%s bust=%s\n",
           ui_state_name(ui),
           detail,
           name,
           slug,
           bust_status_name(paper_faculty_bust_status()));
    fflush(stdout);
}

static void qa_emit_listen(void)
{
    astrolabe_audio_pipeline_t *pipeline = s_bind.pipeline;
    if (pipeline == NULL) {
        printf("qa: pipeline unavailable\n");
        fflush(stdout);
        return;
    }

    const uint32_t rms = astrolabe_audio_pipeline_last_rms(pipeline);
    const uint32_t noise = astrolabe_audio_pipeline_noise_rms(pipeline);
    const uint32_t start = astrolabe_audio_pipeline_start_threshold(pipeline);
    printf("qa: listen speech=%s rms=%u noise=%u start=%u meter=%u storage=%s\n",
           astrolabe_audio_pipeline_speech_active(pipeline) ? "yes" : "no",
           (unsigned)rms,
           (unsigned)noise,
           (unsigned)start,
           (unsigned)(rms > 2200 ? 255 : (rms * 255u) / 2200u),
           paper_capture_mount_path());
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
    if (!paper_board_audio_ready()) {
        printf("qa: audio off (sd=%s mic_probe_peak=%ld)\n",
               paper_board_sd_ready() ? "ok" : "fallback",
               (long)paper_board_mic_probe_peak());
        fflush(stdout);
        return;
    }

    int16_t frame[320];
    int32_t peak_max = 0;
    uint32_t rms_max = 0;
    int frames_ok = 0;
    int frames_zero = 0;

    for (int i = 0; i < 12; ++i) {
        size_t got = 0;
        if (paper_audio_read(frame, 320, &got, 100) != ESP_OK || got == 0) {
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

    astrolabe_audio_pipeline_t *pipeline = s_bind.pipeline;
    if (pipeline != NULL) {
        const uint32_t passive_rms = astrolabe_audio_pipeline_last_rms(pipeline);
        printf("qa: audio passive rms=%u noise=%u start=%u meter=%u speech=%s\n",
               (unsigned)passive_rms,
               (unsigned)astrolabe_audio_pipeline_noise_rms(pipeline),
               (unsigned)astrolabe_audio_pipeline_start_threshold(pipeline),
               (unsigned)(passive_rms > 2200 ? 255 : (passive_rms * 255u) / 2200u),
               astrolabe_audio_pipeline_speech_active(pipeline) ? "yes" : "no");
    }
    fflush(stdout);
}

static void qa_emit_audio_raw(void)
{
    int32_t peak = 0;
    size_t samples = 0;
    size_t nonzero = 0;
    const esp_err_t err = paper_audio_raw_probe(&peak, &samples, &nonzero, 200);
    if (err != ESP_OK) {
        printf("qa: audio-raw err=%s\n", esp_err_to_name(err));
    } else {
        printf("qa: audio-raw samples=%u nonzero=%u peak=%ld\n",
               (unsigned)samples,
               (unsigned)nonzero,
               (long)peak);
    }
    fflush(stdout);
}

static void qa_emit_codec(void)
{
    static const int regs[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                               0x08, 0x11, 0x12, 0x13, 0x40, 0x41, 0x42, 0x43,
                               0x44, 0x47, 0x48, 0x4b};
    printf("qa: codec es7210");
    for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
        int value = 0;
        const esp_err_t err = paper_audio_codec_reg(regs[i], &value);
        if (err == ESP_OK) {
            printf(" r%02x=%02x", regs[i], value & 0xff);
        } else {
            printf(" r%02x=err", regs[i]);
        }
    }
    printf("\n");
    fflush(stdout);
}

static void qa_emit_speaker(void)
{
    int16_t frame[320];
    int writes_ok = 0;
    int writes_fail = 0;
    const int half_period = PAPER_AUDIO_RATE / (440 * 2);
    int phase = 0;

    paper_audio_set_speaker_mute(false);
    for (int chunk = 0; chunk < 25; ++chunk) {
        for (size_t i = 0; i < sizeof(frame) / sizeof(frame[0]); ++i) {
            frame[i] = ((phase / half_period) & 1) ? 6000 : -6000;
            phase++;
        }
        if (paper_audio_write_pcm(frame, sizeof(frame) / sizeof(frame[0]), 500) == ESP_OK) {
            writes_ok++;
        } else {
            writes_fail++;
        }
    }

    printf("qa: speaker writes_ok=%d writes_fail=%d codec=%s\n",
           writes_ok,
           writes_fail,
           writes_ok > 0 ? "ok" : "off");
    fflush(stdout);
}

static void qa_emit_bust(void)
{
    const paper_faculty_bust_status_t status = paper_faculty_bust_status();
    const char *loaded = paper_faculty_loaded_slug();
    if (loaded == NULL || loaded[0] == '\0') {
        loaded = "-";
    }

    printf("qa: bust status=%s loaded=%s size=%dx%d\n",
           bust_status_name(status),
           loaded,
           PAPER_FACULTY_BUST_W,
           PAPER_FACULTY_BUST_H);
    fflush(stdout);
}

bool paper_qa_handle(const char *line)
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
    if (strcasecmp(sub, "audio-raw") == 0 || strcasecmp(sub, "raw-audio") == 0) {
        qa_emit_audio_raw();
        return true;
    }
    if (strcasecmp(sub, "codec") == 0 || strcasecmp(sub, "es7210") == 0) {
        qa_emit_codec();
        return true;
    }
    if (strcasecmp(sub, "speaker") == 0 || strcasecmp(sub, "audio-out") == 0) {
        qa_emit_speaker();
        return true;
    }
    if (strcasecmp(sub, "bust") == 0) {
        qa_emit_bust();
        return true;
    }
    if (strcasecmp(sub, "memory") == 0 || strcasecmp(sub, "storage") == 0) {
        qa_emit_memory();
        return true;
    }

    printf("qa: unknown subcommand \"%s\" (try: qa help)\n", sub);
    fflush(stdout);
    return true;
}
