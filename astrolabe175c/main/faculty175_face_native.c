#include "faculty175_face_native.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "astrolabe_round_bezel.h"
#include "faculty175_board.h"
#include "faculty175_cycle_arcs.h"
#include "faculty175_km.h"
#include "faculty175_lvgl.h"

static const char *TAG = "faculty175_face_native";
enum { INSTRUMENT_RATE_HZ = 24000, INSTRUMENT_CHUNK_FRAMES = 192 };

typedef struct {
    const char *name;
    float hz;
    uint8_t r;
    uint8_t g;
    uint8_t b;
} chakra_center_t;

static const chakra_center_t k_chakras[] = {
    {"ROOT", 396.0f, 180, 40, 45},
    {"SACRAL", 417.0f, 230, 112, 36},
    {"SOLAR", 528.0f, 236, 206, 66},
    {"HEART", 639.0f, 60, 190, 110},
    {"THROAT", 741.0f, 60, 150, 220},
    {"BROW", 852.0f, 76, 84, 210},
    {"CROWN", 963.0f, 170, 86, 210},
};

static uint32_t s_oracle_nonce;
static uint32_t s_focus_started_ms;
static bool s_focus_running;
static volatile bool s_instrument_audio_busy;
static uint8_t s_chakra_index;

bool faculty175_face_native_audio_busy(void)
{
    return s_instrument_audio_busy;
}

bool faculty175_face_native_chakra_delta(faculty175_face_id_t id, int delta)
{
    if (id != FACULTY175_FACE_CHAKRA && id != FACULTY175_FACE_BOWL) {
        return false;
    }
    const int max_index = (int)(sizeof(k_chakras) / sizeof(k_chakras[0])) - 1;
    const int next = (int)s_chakra_index + (delta >= 0 ? 1 : -1);
    if (next < 0 || next > max_index) {
        return false;
    }
    s_chakra_index = (uint8_t)next;
    return true;
}

const char *faculty175_face_native_chakra_name(void)
{
    return k_chakras[s_chakra_index].name;
}

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t bezel_rgb(uint8_t r, uint8_t g, uint8_t b, void *user)
{
    (void)user;
    return rgb(r, g, b);
}

static void bezel_line(int x0, int y0, int x1, int y1, uint16_t color, void *user)
{
    (void)user;
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void bezel_circle(int cx, int cy, int r, uint16_t color, void *user)
{
    (void)user;
    faculty175_display_draw_circle(cx, cy, r, color);
}

static void bezel_fill_circle(int cx, int cy, int r, uint16_t color, void *user)
{
    (void)user;
    faculty175_display_fill_circle(cx, cy, r, color);
}

static uint8_t clamp_u8(int v)
{
    if (v < 0) {
        return 0;
    }
    if (v > 255) {
        return 255;
    }
    return (uint8_t)v;
}

static uint16_t tone(uint8_t hue, int lift)
{
    const int phase = hue % 6;
    const int v = 105 + lift;
    const int lo = 36 + lift / 3;
    const int hi = 190 + lift;
    switch (phase) {
        case 0: return rgb(clamp_u8(hi), clamp_u8(v), clamp_u8(lo));
        case 1: return rgb(clamp_u8(v), clamp_u8(hi), clamp_u8(lo));
        case 2: return rgb(clamp_u8(lo), clamp_u8(hi), clamp_u8(v));
        case 3: return rgb(clamp_u8(lo), clamp_u8(v), clamp_u8(hi));
        case 4: return rgb(clamp_u8(v), clamp_u8(lo), clamp_u8(hi));
        default: return rgb(clamp_u8(hi), clamp_u8(lo), clamp_u8(v));
    }
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, cx - w / 2, y, color);
}

static void rect_outline(int x, int y, int w, int h, uint16_t color)
{
    faculty175_display_draw_line(x, y, x + w, y, color);
    faculty175_display_draw_line(x + w, y, x + w, y + h, color);
    faculty175_display_draw_line(x + w, y + h, x, y + h, color);
    faculty175_display_draw_line(x, y + h, x, y, color);
}

static void line_polar(int cx, int cy, float a, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(a) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(a) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(a) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(a) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void dot_polar(int cx, int cy, float a, int r, int dot_r, uint16_t color)
{
    const int x = cx + (int)lrintf(cosf(a) * (float)r);
    const int y = cy + (int)lrintf(sinf(a) * (float)r);
    faculty175_display_fill_circle(x, y, dot_r, color);
}

static void draw_star(int cx, int cy, int r, uint16_t color)
{
    for (int i = 0; i < 8; ++i) {
        const float a = ((float)i / 8.0f) * 6.2831853f;
        line_polar(cx, cy, a, 0, (i % 2) == 0 ? r : r / 2, color);
    }
}

static void draw_staff(int x, int y, int w, uint16_t color)
{
    for (int i = 0; i < 5; ++i) {
        faculty175_display_draw_line(x, y + i * 12, x + w, y + i * 12, color);
    }
}

static int16_t clamp_audio_i16(int32_t v)
{
    if (v > 32767) {
        return 32767;
    }
    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

static const char *instrument_name(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_CHAKRA: return "chakra";
        case FACULTY175_FACE_BOWL: return "bowl";
        case FACULTY175_FACE_OCARINA: return "ocarina";
        case FACULTY175_FACE_BONGO: return "bongo";
        case FACULTY175_FACE_PIANO: return "piano";
        case FACULTY175_FACE_PANDRUM: return "pandrum";
        case FACULTY175_FACE_KALIMBA: return "kalimba";
        case FACULTY175_FACE_DRONE: return "drone";
        case FACULTY175_FACE_CHORD: return "chord";
        default: return "instrument";
    }
}

static bool face_is_sounding_instrument(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_CHAKRA:
        case FACULTY175_FACE_BOWL:
        case FACULTY175_FACE_OCARINA:
        case FACULTY175_FACE_BONGO:
        case FACULTY175_FACE_PIANO:
        case FACULTY175_FACE_PANDRUM:
        case FACULTY175_FACE_KALIMBA:
        case FACULTY175_FACE_DRONE:
        case FACULTY175_FACE_CHORD:
            return true;
        default:
            return false;
    }
}

static float instrument_envelope(int frame, int total_frames, float attack_ms, float release_ms)
{
    float env = 1.0f;
    const int attack_frames = (int)((attack_ms * (float)INSTRUMENT_RATE_HZ) / 1000.0f);
    const int release_frames = (int)((release_ms * (float)INSTRUMENT_RATE_HZ) / 1000.0f);
    if (attack_frames > 0 && frame < attack_frames) {
        env *= (float)frame / (float)attack_frames;
    }
    if (release_frames > 0 && frame > total_frames - release_frames) {
        env *= (float)(total_frames - frame) / (float)release_frames;
    }
    if (env < 0.0f) {
        env = 0.0f;
    }
    return env;
}

static float instrument_decay(int frame, float half_life_ms)
{
    const float t_ms = ((float)frame * 1000.0f) / (float)INSTRUMENT_RATE_HZ;
    return expf(-0.69314718f * t_ms / half_life_ms);
}

static esp_err_t instrument_write_chunk(int16_t *pcm, int frames)
{
    if (frames <= 0) {
        return ESP_OK;
    }
    return faculty175_audio_write_pcm(pcm, (size_t)frames * 2u, 500);
}

static bool play_instrument_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    if (!face_is_sounding_instrument(id)) {
        return false;
    }
    if (!faculty175_board_audio_ready()) {
        ESP_LOGW(TAG, "instrument %s skipped: audio off", instrument_name(id));
        return true;
    }
    s_instrument_audio_busy = true;

    static const float piano_hz[] = {261.63f, 293.66f, 329.63f, 349.23f, 392.00f, 440.00f, 493.88f, 523.25f};
    static const float ocarina_hz[] = {392.00f, 440.00f, 493.88f, 523.25f, 587.33f, 659.25f};
    static const float pan_hz[] = {220.00f, 246.94f, 261.63f, 293.66f, 329.63f, 392.00f, 440.00f, 493.88f};
    static const float kalimba_hz[] = {261.63f, 293.66f, 329.63f, 392.00f, 440.00f, 523.25f};

    float f0 = 440.0f;
    float f1 = 0.0f;
    float f2 = 0.0f;
    int total_ms = 520;
    int amp = 5200;
    float half_life_ms = 420.0f;
    const uint32_t pick = seed_ms ^ esp_random() ^ ((uint32_t)id * 1103515245u);

    switch (id) {
        case FACULTY175_FACE_CHAKRA: {
            f0 = k_chakras[s_chakra_index].hz;
            f1 = f0 * 2.0f;
            f2 = f0 * 1.5f;
            total_ms = 1300;
            amp = 4300;
            half_life_ms = 1050.0f;
            break;
        }
        case FACULTY175_FACE_BOWL:
            f0 = k_chakras[s_chakra_index].hz * 0.5f;
            f1 = f0 * 2.01f;
            f2 = f0 * 2.98f;
            total_ms = 1700;
            amp = 6200;
            half_life_ms = 1250.0f;
            break;
        case FACULTY175_FACE_PIANO:
            f0 = piano_hz[pick % (sizeof(piano_hz) / sizeof(piano_hz[0]))];
            f1 = f0 * 2.0f;
            total_ms = 520;
            amp = 5600;
            half_life_ms = 360.0f;
            break;
        case FACULTY175_FACE_OCARINA:
            f0 = ocarina_hz[pick % (sizeof(ocarina_hz) / sizeof(ocarina_hz[0]))];
            f1 = f0 * 2.0f;
            total_ms = 620;
            amp = 4200;
            half_life_ms = 900.0f;
            break;
        case FACULTY175_FACE_BONGO:
            f0 = (pick & 1u) ? 148.0f : 196.0f;
            f1 = f0 * 1.58f;
            total_ms = 320;
            amp = 7600;
            half_life_ms = 95.0f;
            break;
        case FACULTY175_FACE_PANDRUM:
            f0 = pan_hz[pick % (sizeof(pan_hz) / sizeof(pan_hz[0]))];
            f1 = f0 * 2.0f;
            f2 = f0 * 3.01f;
            total_ms = 1050;
            amp = 5000;
            half_life_ms = 760.0f;
            break;
        case FACULTY175_FACE_KALIMBA:
            f0 = kalimba_hz[pick % (sizeof(kalimba_hz) / sizeof(kalimba_hz[0]))];
            f1 = f0 * 2.0f;
            total_ms = 650;
            amp = 4400;
            half_life_ms = 300.0f;
            break;
        case FACULTY175_FACE_DRONE:
            f0 = 146.83f;
            f1 = 220.0f;
            f2 = 293.66f;
            total_ms = 1400;
            amp = 3900;
            half_life_ms = 1600.0f;
            break;
        case FACULTY175_FACE_CHORD:
            f0 = 261.63f;
            f1 = 329.63f;
            f2 = 392.0f;
            total_ms = 900;
            amp = 4300;
            half_life_ms = 700.0f;
            break;
        default:
            break;
    }

    const esp_err_t rate_err = faculty175_audio_set_sample_rate(INSTRUMENT_RATE_HZ);
    if (rate_err != ESP_OK) {
        ESP_LOGW(TAG, "instrument %s sample_rate=%s", instrument_name(id), esp_err_to_name(rate_err));
        s_instrument_audio_busy = false;
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    faculty175_audio_set_speaker_mute(false);

    const int total_frames = (INSTRUMENT_RATE_HZ * total_ms) / 1000;
    int16_t pcm[INSTRUMENT_CHUNK_FRAMES * 2];
    float phase0 = 0.0f;
    float phase1 = 0.0f;
    float phase2 = 0.0f;
    const float step0 = 6.2831853f * f0 / (float)INSTRUMENT_RATE_HZ;
    const float step1 = 6.2831853f * f1 / (float)INSTRUMENT_RATE_HZ;
    const float step2 = 6.2831853f * f2 / (float)INSTRUMENT_RATE_HZ;
    uint32_t write_fail = 0;

    if (id == FACULTY175_FACE_CHAKRA || id == FACULTY175_FACE_BOWL) {
        ESP_LOGI(TAG, "instrument %s chakra=%s note=%.1fHz ms=%d", instrument_name(id),
                 k_chakras[s_chakra_index].name, (double)f0, total_ms);
    } else {
        ESP_LOGI(TAG, "instrument %s note=%.1fHz ms=%d", instrument_name(id), (double)f0, total_ms);
    }
    for (int base = 0; base < total_frames; base += INSTRUMENT_CHUNK_FRAMES) {
        const int frames =
            (total_frames - base) < INSTRUMENT_CHUNK_FRAMES ? (total_frames - base) : INSTRUMENT_CHUNK_FRAMES;
        for (int i = 0; i < frames; ++i) {
            const int t = base + i;
            float sample = sinf(phase0);
            if (f1 > 0.0f) {
                sample += 0.34f * sinf(phase1);
            }
            if (f2 > 0.0f) {
                sample += 0.18f * sinf(phase2);
            }
            if (id == FACULTY175_FACE_BOWL || id == FACULTY175_FACE_PANDRUM || id == FACULTY175_FACE_CHAKRA) {
                sample *= 1.0f + 0.07f * sinf((float)t * 6.2831853f * 5.3f / (float)INSTRUMENT_RATE_HZ);
            }
            if (id == FACULTY175_FACE_BONGO) {
                sample += 0.22f * sinf((float)t * 6.2831853f * 73.0f / (float)INSTRUMENT_RATE_HZ);
            }
            float env = instrument_envelope(t, total_frames, id == FACULTY175_FACE_BONGO ? 3.0f : 18.0f, 90.0f);
            env *= instrument_decay(t, half_life_ms);
            const int16_t v = clamp_audio_i16((int32_t)(sample * env * (float)amp));
            pcm[i * 2] = v;
            pcm[i * 2 + 1] = v;
            phase0 += step0;
            phase1 += step1;
            phase2 += step2;
            if (phase0 > 6.2831853f) phase0 -= 6.2831853f;
            if (phase1 > 6.2831853f) phase1 -= 6.2831853f;
            if (phase2 > 6.2831853f) phase2 -= 6.2831853f;
        }
        if (instrument_write_chunk(pcm, frames) != ESP_OK) {
            ++write_fail;
        }
        vTaskDelay(1);
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(faculty175_audio_set_sample_rate(FACULTY175_AUDIO_RATE));
    ESP_LOGI(TAG, "instrument %s done write_fail=%lu", instrument_name(id), (unsigned long)write_fail);
    s_instrument_audio_busy = false;
    return true;
}

static void draw_frame(const faculty175_native_face_t *face, uint16_t accent, uint16_t dim)
{
    faculty175_display_fill_rgb565(rgb(4, 5, 10));
    if (face->id != FACULTY175_FACE_MOON) {
        faculty175_display_fill_rect(0, 0, FACULTY175_LCD_W, 48, rgb(14, 15, 22));
        faculty175_display_draw_centered_text(face->title, 12, accent);
        if (face->subtitle != NULL) {
            faculty175_display_draw_centered_text(face->subtitle, 32, dim);
        }
    }
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    const astrolabe_round_bezel_ops_t ops = {
        .width = FACULTY175_LCD_W,
        .height = FACULTY175_LCD_H,
        .cx = cx,
        .cy = cy,
        .outer_radius = 205,
        .inner_radius = 188,
        .rgb565 = bezel_rgb,
        .draw_line = bezel_line,
        .draw_circle = bezel_circle,
        .fill_circle = bezel_fill_circle,
    };
    astrolabe_round_bezel_draw(&ops, rgb(42, 47, 58), rgb(54, 58, 68), NULL);
}

static void draw_analog(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 244;
    if (face->id == FACULTY175_FACE_CALCIFER) {
        for (int r = 142; r >= 42; r -= 18) {
            faculty175_display_draw_circle(cx, cy, r, tone(face->hue + r / 18, 18));
        }
        for (int i = 0; i < 18; ++i) {
            const float a = ((float)i / 18.0f) * 6.2831853f + (float)(anim_ms % 3000u) / 3000.0f;
            line_polar(cx, cy + 10, a, 18, 142 - (i % 4) * 12, tone(face->hue + i, 36));
        }
        faculty175_display_fill_circle(cx, cy, 58, rgb(255, 118, 44));
        faculty175_display_fill_circle(cx - 18, cy - 8, 9, rgb(18, 14, 12));
        faculty175_display_fill_circle(cx + 18, cy - 8, 9, rgb(18, 14, 12));
        faculty175_display_draw_line(cx - 22, cy + 24, cx + 22, cy + 24, rgb(70, 20, 12));
        centered_at("HEARTH CYCLE", cx, 360, dim);
        return;
    }
    for (int r = 52; r <= 154; r += 34) {
        faculty175_display_draw_circle(cx, cy, r, rgb(42, 48, 58));
    }
    for (int i = 0; i < 12; ++i) {
        line_polar(cx, cy, ((float)i / 12.0f) * 6.2831853f - 1.5707963f, 132, 150, accent);
    }
    const float m = ((float)(anim_ms % 60000u) / 60000.0f) * 6.2831853f - 1.5707963f;
    const float h = ((float)(anim_ms % 720000u) / 720000.0f) * 6.2831853f - 1.5707963f;
    line_polar(cx, cy, h, -12, 78, tone(face->hue + 1, 48));
    line_polar(cx, cy, m, -18, 124, accent);
    faculty175_display_fill_circle(cx, cy, 10, accent);
    centered_at(face->a, cx, 362, dim);
}

static void draw_digital(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const uint32_t seconds = (anim_ms / 1000u) % 86400u;
    char line[32];
    snprintf(line, sizeof(line), "%02lu:%02lu:%02lu", (unsigned long)(seconds / 3600u),
             (unsigned long)((seconds / 60u) % 60u), (unsigned long)(seconds % 60u));
    faculty175_display_fill_rect(80, 176, 306, 112, rgb(10, 18, 22));
    rect_outline(80, 176, 306, 112, accent);
    for (int i = 0; i < 8; ++i) {
        faculty175_display_fill_rect(96 + i * 34, 292, 18, 8 + (int)((anim_ms / 140u + (uint32_t)i * 3u) % 30u),
                                     tone(face->hue + i, 18));
    }
    centered_at(line, cx, 214, accent);
    centered_at(face->a, cx, 300, dim);
    centered_at(face->b, cx, 326, tone(face->hue + 2, 28));
}

static void draw_oracle(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 238;
    if (face->id == FACULTY175_FACE_APOCALYPSO) {
        for (int r = 42; r <= 168; r += 21) {
            faculty175_display_draw_circle(cx, cy, r, rgb(34, 38, 54));
        }
        const float tide = sinf((float)(anim_ms % 6000u) / 6000.0f * 6.2831853f);
        for (int i = 0; i < 9; ++i) {
            const int y = cy - 96 + i * 24;
            const int amp = 34 + i * 6;
            faculty175_display_draw_line(cx - amp, y + (int)(tide * (float)i), cx + amp, y - (int)(tide * (float)i),
                                         tone(face->hue + i, 26));
        }
        faculty175_display_fill_circle(cx, cy, 26, accent);
        draw_star(cx, cy, 58, rgb(238, 210, 130));
        centered_at("OMEN TIDE", cx, 366, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_GEOMANCY) {
        static const uint8_t figures[4] = {0x6, 0x9, 0x3, 0xc};
        for (int f = 0; f < 4; ++f) {
            const int x = 128 + f * 70;
            rect_outline(x - 24, cy - 76, 48, 152, tone(face->hue + f, 24));
            for (int row = 0; row < 4; ++row) {
                const bool pair = (figures[f] >> row) & 1;
                if (pair) {
                    faculty175_display_fill_circle(x - 9, cy - 48 + row * 30, 5, accent);
                    faculty175_display_fill_circle(x + 9, cy - 48 + row * 30, 5, accent);
                } else {
                    faculty175_display_fill_circle(x, cy - 48 + row * 30, 6, accent);
                }
            }
        }
        centered_at("MOTHERS OF THE HOUSE", cx, 360, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_LENORMAND || face->id == FACULTY175_FACE_INQ) {
        const int cards = face->id == FACULTY175_FACE_LENORMAND ? 5 : 3;
        for (int i = 0; i < cards; ++i) {
            const int x = cx - (cards - 1) * 34 + i * 68;
            const int y = cy - 56 + ((i % 2) ? -12 : 10);
            faculty175_display_fill_rect(x - 28, y, 56, 104, rgb(24, 18, 28));
            rect_outline(x - 28, y, 56, 104, tone(face->hue + i, 34));
            draw_star(x, y + 34, 18, tone(face->hue + i + 2, 38));
            char n[4];
            snprintf(n, sizeof(n), "%02d", 1 + (int)((anim_ms / 700u + (uint32_t)i * 7u) % (face->id == FACULTY175_FACE_LENORMAND ? 36u : 64u)));
            centered_at(n, x, y + 72, dim);
        }
        centered_at(face->id == FACULTY175_FACE_LENORMAND ? "GRAND TABLEAU" : "INQUIRY FIELD", cx, 370, dim);
        return;
    }
    const uint32_t spin = (anim_ms / 40u + s_oracle_nonce + (uint32_t)face->id * 13u);
    for (int i = 0; i < 3; ++i) {
        const int x = 132 + i * 101;
        faculty175_display_fill_rect(x - 34, cy - 58 + (i == 1 ? -12 : 8), 68, 116, rgb(20, 17, 26));
        faculty175_display_draw_line(x - 34, cy - 58 + (i == 1 ? -12 : 8), x + 34, cy - 58 + (i == 1 ? -12 : 8), accent);
        faculty175_display_draw_line(x + 34, cy - 58 + (i == 1 ? -12 : 8), x + 34, cy + 58 + (i == 1 ? -12 : 8), accent);
        faculty175_display_draw_line(x + 34, cy + 58 + (i == 1 ? -12 : 8), x - 34, cy + 58 + (i == 1 ? -12 : 8), accent);
        faculty175_display_draw_line(x - 34, cy + 58 + (i == 1 ? -12 : 8), x - 34, cy - 58 + (i == 1 ? -12 : 8), accent);
        faculty175_display_fill_circle(x, cy + (i == 1 ? -12 : 8), 21, tone(face->hue + i, 24));
        char mark[4];
        snprintf(mark, sizeof(mark), "%lu", (unsigned long)((spin + (uint32_t)i * 7u) % 22u));
        centered_at(mark, x, cy + (i == 1 ? -16 : 4), rgb(240, 234, 212));
    }
    centered_at(face->a, cx, 348, rgb(230, 224, 204));
    centered_at(face->b, cx, 374, dim);
}

static void draw_instrument(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 238;
    if (face->id == FACULTY175_FACE_SPECTRUM) {
        for (int i = 0; i < 24; ++i) {
            const int h = 24 + (int)((anim_ms / (70u + (uint32_t)i * 7u) + (uint32_t)i * 11u) % 126u);
            const int x = 70 + i * 14;
            faculty175_display_fill_rect(x, cy + 78 - h, 8, h, tone(face->hue + i, h / 8));
        }
        faculty175_display_draw_line(64, cy + 78, 402, cy + 78, dim);
        for (int r = 52; r <= 156; r += 26) {
            faculty175_display_draw_circle(cx, cy, r, rgb(28, 36, 48));
        }
        centered_at("FFT MICROPHONE FIELD", cx, 372, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_PIANO) {
        faculty175_display_fill_rect(54, 184, 358, 122, rgb(232, 232, 216));
        for (int i = 0; i <= 7; ++i) {
            faculty175_display_draw_line(54 + i * 51, 184, 54 + i * 51, 306, rgb(30, 32, 36));
        }
        static const int blacks[] = {1, 2, 4, 5, 6};
        for (int i = 0; i < 5; ++i) {
            faculty175_display_fill_rect(54 + blacks[i] * 51 - 15, 184, 30, 74, rgb(12, 12, 16));
        }
        faculty175_display_fill_rect(54 + (int)((anim_ms / 250u) % 7u) * 51 + 8, 286, 34, 14, accent);
        centered_at("C D E F G A B", cx, 346, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_BONGO) {
        faculty175_display_fill_circle(168, cy, 78, rgb(126, 72, 42));
        faculty175_display_fill_circle(168, cy, 58, rgb(232, 184, 126));
        faculty175_display_fill_circle(300, cy + 4, 66, rgb(112, 62, 38));
        faculty175_display_fill_circle(300, cy + 4, 48, rgb(230, 180, 122));
        faculty175_display_draw_circle(168, cy, 78 + (int)((anim_ms / 180u) % 5u), accent);
        faculty175_display_draw_circle(300, cy + 4, 66 + (int)((anim_ms / 220u) % 5u), accent);
        centered_at("TWO TOUCH DRUMS", cx, 358, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_KALIMBA) {
        faculty175_display_fill_rect(104, 152, 258, 222, rgb(86, 48, 30));
        rect_outline(104, 152, 258, 222, rgb(180, 116, 62));
        for (int i = 0; i < 9; ++i) {
            const int x = 126 + i * 27;
            const int h = 72 + (i % 5) * 18;
            faculty175_display_fill_rect(x, 178, 12, h, rgb(196, 206, 210));
            faculty175_display_fill_circle(x + 6, 178 + h + 12, 9, tone(face->hue + i, 22));
        }
        faculty175_display_draw_circle(cx, 302, 38, rgb(30, 18, 12));
        centered_at("PENTATONIC TINES", cx, 390, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_OCARINA) {
        faculty175_display_fill_circle(cx, cy, 96, rgb(72, 96, 118));
        faculty175_display_fill_circle(cx + 68, cy - 28, 44, rgb(72, 96, 118));
        for (int i = 0; i < 6; ++i) {
            dot_polar(cx, cy, -2.4f + (float)i * 0.52f, 54, 10, rgb(8, 14, 18));
        }
        faculty175_display_fill_circle(cx - 76, cy + 18, 16, rgb(8, 14, 18));
        centered_at("BREATH HOLES", cx, 358, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_PANDRUM) {
        faculty175_display_fill_circle(cx, cy, 138, rgb(56, 70, 80));
        faculty175_display_fill_circle(cx, cy, 42, rgb(36, 46, 54));
        for (int i = 0; i < 10; ++i) {
            const float a = ((float)i / 10.0f) * 6.2831853f - 1.5707963f;
            const int r = i < 5 ? 72 : 112;
            const int x = cx + (int)lrintf(cosf(a) * (float)r);
            const int y = cy + (int)lrintf(sinf(a) * (float)r);
            faculty175_display_fill_circle(x, y, 20, tone(face->hue + i, 18));
        }
        centered_at("HANDPAN NOTES", cx, 390, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_PITCH || face->id == FACULTY175_FACE_TUNING) {
        draw_staff(82, 178, 302, dim);
        for (int i = 0; i < 7; ++i) {
            const int x = 104 + i * 43;
            const int y = 228 - ((i * 9 + (int)(anim_ms / 90u)) % 48);
            faculty175_display_fill_circle(x, y, 10, tone(face->hue + i, 28));
            faculty175_display_draw_line(x + 10, y, x + 10, y - 40, accent);
        }
        centered_at(face->id == FACULTY175_FACE_PITCH ? "REFERENCE A4" : "LIVE PITCH STAFF", cx, 344, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_CHORD || face->id == FACULTY175_FACE_DRONE) {
        for (int i = 0; i < 6; ++i) {
            const int x = 96 + i * 53;
            faculty175_display_draw_line(x, 150, x, 344, dim);
            faculty175_display_fill_circle(x, 188 + (i % 3) * 42, 13, tone(face->hue + i, 34));
        }
        centered_at(face->id == FACULTY175_FACE_DRONE ? "ROOT FIFTH OCTAVE" : "AUTOHARP CHORDS", cx, 368, accent);
        return;
    }
    if (face->id == FACULTY175_FACE_CHAKRA) {
        const size_t chakra_count = sizeof(k_chakras) / sizeof(k_chakras[0]);
        for (size_t i = 0; i < chakra_count; ++i) {
            const int y = 330 - i * 42;
            const bool selected = i == s_chakra_index;
            faculty175_display_fill_circle(cx, y, selected ? 28 : 20,
                                           rgb(k_chakras[i].r, k_chakras[i].g, k_chakras[i].b));
            draw_star(cx, y, selected ? 21 : 14, rgb(245, 238, 220));
            if (selected) {
                faculty175_display_draw_circle(cx, y, 34, rgb(245, 238, 220));
            }
        }
        char line[32];
        snprintf(line, sizeof(line), "%s %.0fHZ", k_chakras[s_chakra_index].name,
                 (double)k_chakras[s_chakra_index].hz);
        centered_at(line, cx, 372, accent);
        return;
    }
    if (face->id == FACULTY175_FACE_BOWL) {
        const uint16_t chakra_color =
            rgb(k_chakras[s_chakra_index].r, k_chakras[s_chakra_index].g, k_chakras[s_chakra_index].b);
        faculty175_display_draw_circle(cx, cy, 122 + (int)((anim_ms / 100u) % 16u), dim);
        faculty175_display_draw_circle(cx, cy, 82 + (int)((anim_ms / 140u) % 14u), chakra_color);
        faculty175_display_fill_rect(cx - 96, cy + 32, 192, 34, rgb(146, 94, 42));
        faculty175_display_fill_circle(cx, cy + 32, 96, rgb(168, 116, 52));
        faculty175_display_draw_circle(cx, cy + 32, 102, chakra_color);
        faculty175_display_fill_rect(cx - 98, cy - 10, 196, 42, rgb(10, 10, 14));
        char line[32];
        snprintf(line, sizeof(line), "%s BOWL", k_chakras[s_chakra_index].name);
        centered_at(line, cx, 358, chakra_color);
        return;
    }
    for (int i = 0; i < 14; ++i) {
        const float a = ((float)i / 14.0f) * 6.2831853f - 1.5707963f;
        const int r = 124 + ((i % 2) * 22);
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        const int pulse = (int)((anim_ms / 120u + (uint32_t)i) % 14u) == 0 ? 9 : 0;
        faculty175_display_fill_circle(x, y, 14 + pulse, tone(face->hue + i, pulse));
    }
    faculty175_display_draw_circle(cx, cy, 88, rgb(46, 51, 62));
    faculty175_display_fill_circle(cx, cy, 36, rgb(18, 22, 28));
    centered_at(face->a, cx, cy - 4, accent);
    centered_at(face->b, cx, 362, dim);
}

static void draw_celestial(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 238;
    if (face->id == FACULTY175_FACE_MOON) {
        const float phase = faculty175_cycle_lunar_phase(anim_ms);
        const uint16_t highland = rgb(190, 190, 181);
        const uint16_t mare = rgb(112, 118, 120);
        const uint16_t crater_floor = rgb(146, 148, 144);
        const uint16_t crater_rim = rgb(220, 216, 202);
        faculty175_display_fill_circle(cx, cy, 126, highland);

        /* Broad, low-contrast maria establish recognizable lunar terrain. All
         * features stay inside the disc; the phase shadow below clips them to
         * the illuminated portion without requiring a framebuffer mask. */
        faculty175_display_fill_circle(cx + 52, cy - 46, 31, mare);
        faculty175_display_fill_circle(cx + 82, cy + 10, 23, rgb(126, 130, 128));
        faculty175_display_fill_circle(cx + 42, cy + 55, 27, rgb(130, 133, 130));
        faculty175_display_fill_circle(cx - 18, cy - 70, 20, rgb(132, 136, 136));

        static const int8_t craters[][3] = {
            {82, -58, 10}, {96, -22, 7}, {76, 39, 12}, {48, 82, 8},
            {28, -92, 6}, {12, 68, 9}, {-38, -72, 8}, {-64, 18, 11},
        };
        for (size_t i = 0; i < sizeof(craters) / sizeof(craters[0]); ++i) {
            const int x = cx + craters[i][0];
            const int y = cy + craters[i][1];
            const int r = craters[i][2];
            faculty175_display_fill_circle(x, y, r, crater_floor);
            faculty175_display_draw_circle(x, y, r, crater_rim);
            faculty175_display_draw_circle(x + 1, y + 1, r > 6 ? r - 3 : r - 2, mare);
        }

        const int shadow = -74 + (int)lrintf(phase * 148.0f);
        faculty175_display_fill_circle(cx + shadow, cy, 128, rgb(8, 10, 18));
        faculty175_display_draw_circle(cx, cy, 126, rgb(224, 220, 208));
        faculty175_cycle_draw_lunar_arc(cx, cy, 212, anim_ms);
        centered_at("WAXING / WANING", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_GLOBE) {
        faculty175_display_fill_circle(cx, cy, 126, rgb(20, 78, 126));
        for (int i = 0; i < 6; ++i) {
            faculty175_display_draw_circle(cx, cy, 34 + i * 18, rgb(48, 122, 160));
        }
        faculty175_display_fill_circle(cx - 36, cy - 18, 28, rgb(52, 154, 88));
        faculty175_display_fill_circle(cx + 42, cy + 28, 36, rgb(52, 154, 88));
        faculty175_display_fill_rect(cx + 18, cy - 126, 128, 252, rgb(7, 10, 18));
        centered_at("DAY NIGHT TERMINATOR", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_TRANSITS) {
        for (int r = 46; r <= 150; r += 26) {
            faculty175_display_draw_circle(cx, cy, r, rgb(46, 42, 70));
        }
        static const char *glyphs[] = {"SU", "MO", "ME", "VE", "MA", "JU", "SA"};
        for (int i = 0; i < 7; ++i) {
            const float a = ((float)(anim_ms % (6000u + (uint32_t)i * 700u)) / (float)(6000u + (uint32_t)i * 700u)) * 6.2831853f + (float)i;
            const int r = 46 + (i % 5) * 26;
            const int x = cx + (int)lrintf(cosf(a) * (float)r);
            const int y = cy + (int)lrintf(sinf(a) * (float)r);
            faculty175_display_fill_circle(x, y, 12, tone(face->hue + i, 28));
            centered_at(glyphs[i], x, y - 3, rgb(8, 10, 18));
        }
        centered_at("LIVE EPHEMERIS", cx, 370, dim);
        return;
    }
    for (int r = 54; r <= 164; r += 28) {
        faculty175_display_draw_circle(cx, cy, r, rgb(32, 38, 54));
    }
    for (int i = 0; i < 9; ++i) {
        const float a = ((float)i / 9.0f) * 6.2831853f + (float)(anim_ms % 9000u) / 9000.0f;
        const int r = 54 + (i % 5) * 27;
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        faculty175_display_fill_circle(x, y, 5 + (i % 3), tone(face->hue + i, 38));
    }
    faculty175_display_fill_circle(cx, cy, 30, accent);
    centered_at(face->a, cx, 352, rgb(230, 224, 204));
    centered_at(face->b, cx, 378, dim);
}

static void draw_radar(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 238;
    if (face->id == FACULTY175_FACE_ORIENT) {
        for (int r = 44; r <= 160; r += 28) {
            faculty175_display_draw_circle(cx, cy, r, rgb(40, 52, 50));
        }
        line_polar(cx, cy, -1.5707963f, 0, 168, accent);
        line_polar(cx, cy, 0, -160, 160, dim);
        const float roll = sinf((float)(anim_ms % 5000u) / 5000.0f * 6.2831853f) * 0.65f;
        line_polar(cx, cy, roll, -110, 110, rgb(220, 190, 80));
        faculty175_display_fill_circle(cx + (int)(sinf(roll) * 70.0f), cy + (int)(cosf(roll) * 36.0f), 12, accent);
        centered_at("ROLL / PITCH", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_WATCHER) {
        faculty175_display_draw_circle(cx, cy, 126, rgb(40, 58, 70));
        faculty175_display_fill_circle(cx, cy, 84, rgb(10, 18, 24));
        faculty175_display_fill_circle(cx - 28, cy - 8, 18, accent);
        faculty175_display_fill_circle(cx + 28, cy - 8, 18, accent);
        faculty175_display_draw_line(cx - 44, cy + 38, cx + 44, cy + 38, dim);
        for (int i = 0; i < 10; ++i) {
            dot_polar(cx, cy, ((float)i / 10.0f) * 6.2831853f + (float)(anim_ms % 3000u) / 3000.0f, 154, 4, tone(face->hue + i, 20));
        }
        centered_at("PRESENCE FIELD", cx, 370, dim);
        return;
    }
    for (int r = 44; r <= 172; r += 32) {
        faculty175_display_draw_circle(cx, cy, r, rgb(30, 54, 48));
    }
    const float sweep = ((float)(anim_ms % 5000u) / 5000.0f) * 6.2831853f;
    line_polar(cx, cy, sweep, 0, 172, accent);
    for (int i = 0; i < 7; ++i) {
        const float a = ((float)(i * 37) / 180.0f) * 3.1415926f;
        const int r = 42 + ((i * 29) % 120);
        const int x = cx + (int)lrintf(cosf(a) * (float)r);
        const int y = cy + (int)lrintf(sinf(a) * (float)r);
        faculty175_display_fill_circle(x, y, 4 + (i % 3), tone(face->hue + i, 40));
    }
    centered_at(face->a, cx, 360, dim);
}

static void draw_status(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    const int cy = 238;
    if (face->id == FACULTY175_FACE_SPOTIFY) {
        faculty175_display_fill_circle(cx, cy, 118, rgb(20, 120, 72));
        for (int i = 0; i < 3; ++i) {
            const int y = cy - 42 + i * 32;
            faculty175_display_draw_line(cx - 58, y, cx + 58, y + 10 + i * 2, rgb(8, 12, 10));
            faculty175_display_draw_line(cx - 58, y + 1, cx + 58, y + 11 + i * 2, rgb(8, 12, 10));
        }
        faculty175_display_fill_rect(112, 352, 244, 12, rgb(22, 28, 28));
        faculty175_display_fill_rect(112, 352, 64 + (int)((anim_ms / 80u) % 160u), 12, accent);
        centered_at("REMOTE NOW PLAYING", cx, 382, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_WEATHER) {
        faculty175_display_fill_circle(cx - 42, cy - 34, 42, rgb(248, 194, 72));
        faculty175_display_fill_circle(cx - 8, cy + 8, 50, rgb(142, 168, 188));
        faculty175_display_fill_circle(cx + 48, cy + 10, 44, rgb(132, 156, 180));
        faculty175_display_fill_rect(cx - 68, cy + 18, 148, 42, rgb(132, 156, 180));
        for (int i = 0; i < 5; ++i) {
            line_polar(cx - 80 + i * 42, cy + 94, 1.5707963f, 0, 34, rgb(92, 170, 230));
        }
        centered_at("TEMP  WIND  PRESSURE", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_ROCKET) {
        faculty175_display_fill_rect(cx - 14, cy - 92, 28, 138, rgb(214, 220, 222));
        faculty175_display_fill_circle(cx, cy - 96, 28, rgb(220, 70, 54));
        faculty175_display_fill_circle(cx, cy - 42, 14, rgb(40, 120, 220));
        faculty175_display_draw_line(cx - 14, cy + 24, cx - 54, cy + 78, accent);
        faculty175_display_draw_line(cx + 14, cy + 24, cx + 54, cy + 78, accent);
        for (int i = 0; i < 4; ++i) {
            faculty175_display_fill_circle(cx - 18 + i * 12, cy + 72 + (int)((anim_ms / 80u + (uint32_t)i) % 24u), 8, rgb(255, 130, 44));
        }
        centered_at("LAUNCH WINDOW", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_FOCUS) {
        const int progress = (int)((anim_ms / 120u) % 240u);
        faculty175_display_draw_circle(cx, cy, 128, dim);
        for (int i = 0; i < progress; ++i) {
            dot_polar(cx, cy, -1.5707963f + ((float)i / 240.0f) * 6.2831853f, 128, 2, accent);
        }
        centered_at(s_focus_running ? "FOCUS RUNNING" : "TAP TO START", cx, cy - 4, accent);
        centered_at("25 MINUTE FIELD", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_LEVEL) {
        faculty175_display_draw_circle(cx, cy, 126, dim);
        faculty175_display_draw_line(cx - 126, cy, cx + 126, cy, dim);
        faculty175_display_draw_line(cx, cy - 126, cx, cy + 126, dim);
        const int bx = cx + (int)(sinf((float)anim_ms / 900.0f) * 74.0f);
        const int by = cy + (int)(cosf((float)anim_ms / 1100.0f) * 54.0f);
        faculty175_display_fill_circle(bx, by, 24, accent);
        centered_at("BUBBLE LEVEL", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_HID) {
        rect_outline(cx - 88, cy - 112, 176, 224, dim);
        faculty175_display_draw_line(cx, cy - 112, cx, cy + 112, dim);
        faculty175_display_draw_line(cx - 88, cy - 40, cx + 88, cy - 40, dim);
        faculty175_display_fill_circle(cx + (int)(sinf((float)anim_ms / 700.0f) * 54.0f),
                                       cy + (int)(cosf((float)anim_ms / 900.0f) * 72.0f),
                                       14, accent);
        centered_at("USB POINTER", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_SETTINGS) {
        static const char *items[] = {"BATTERY", "WIFI", "BLUETOOTH", "FAMILY / OTA"};
        for (int i = 0; i < 4; ++i) {
            const int y = 156 + i * 48;
            rect_outline(cx - 122, y - 12, 244, 28, rgb(42, 46, 58));
            faculty175_display_fill_rect(cx - 118, y - 8, 88 + i * 28, 20, tone(face->hue + i, 18));
            centered_at(items[i], cx - 66, y - 5, rgb(236, 238, 242));
        }
        centered_at("SWIPE UP TO RETURN", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_BIOMETRICS) {
        for (int i = 0; i < 64; ++i) {
            const float a = ((float)i / 64.0f) * 6.2831853f;
            const int r = 78 + (int)(sinf((float)i * 0.7f + (float)anim_ms / 600.0f) * 22.0f);
            dot_polar(cx, cy, a, r, 2, tone(face->hue + i, 18));
        }
        faculty175_display_fill_circle(cx, cy, 44, rgb(18, 24, 26));
        centered_at("HRV", cx, cy - 6, accent);
        centered_at("READINESS MODEL", cx, 370, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_BIOMETRICS || face->id == FACULTY175_FACE_HID ||
        face->id == FACULTY175_FACE_SETTINGS) {
        for (int i = 0; i < 4; ++i) {
            const int y = 154 + i * 48;
            const int w = 72 + (int)((anim_ms / (160u + (uint32_t)i * 40u)) % 164u);
            rect_outline(cx - 128, y - 10, 256, 28, rgb(40, 44, 54));
            faculty175_display_fill_rect(cx - 124, y - 6, w, 20, tone(face->hue + i, 20));
        }
        centered_at(face->a, cx, 356, accent);
        centered_at(face->b, cx, 382, dim);
        return;
    }
    for (int i = 0; i < 5; ++i) {
        const int y = 154 + i * 36;
        const int w = 112 + (int)((anim_ms / (180u + (uint32_t)i * 30u)) % 96u);
        faculty175_display_fill_rect(cx - 112, y, 224, 12, rgb(22, 25, 32));
        faculty175_display_fill_rect(cx - 112, y, w, 12, tone(face->hue + i, 24));
    }
    centered_at(face->a, cx, cy + 58, accent);
    centered_at(face->b, cx, cy + 84, dim);
    centered_at(face->c, cx, cy + 110, dim);
}

static void draw_text(const faculty175_native_face_t *face, uint32_t anim_ms, uint16_t accent, uint16_t dim)
{
    const int cx = 233;
    if (face->id == FACULTY175_FACE_QUOTES) {
        faculty175_display_fill_rect(78, 150, 310, 210, rgb(20, 18, 24));
        rect_outline(78, 150, 310, 210, accent);
        faculty175_display_draw_text("\"", 110, 176, accent);
        faculty175_display_draw_text("\"", 348, 300, accent);
        centered_at("THE RIGHT WORD", cx, 210, rgb(232, 224, 204));
        centered_at("ARRIVES WHEN", cx, 238, rgb(232, 224, 204));
        centered_at("IT IS NEEDED", cx, 266, rgb(232, 224, 204));
        centered_at("COMMONPLACE QUOTES", cx, 382, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_CASTALIA) {
        for (int i = 0; i < 6; ++i) {
            dot_polar(cx, 232, ((float)i / 6.0f) * 6.2831853f + (float)(anim_ms % 5000u) / 5000.0f, 118, 16, tone(face->hue + i, 20));
            line_polar(cx, 232, ((float)i / 6.0f) * 6.2831853f, 0, 118, rgb(40, 54, 64));
        }
        faculty175_display_fill_circle(cx, 232, 46, accent);
        centered_at("CI", cx, 228, rgb(8, 12, 16));
        centered_at("SERVICE HUB", cx, 350, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_BABEL) {
        faculty175_display_fill_circle(cx - 54, 226, 50, rgb(44, 88, 130));
        faculty175_display_fill_circle(cx + 54, 226, 50, rgb(132, 92, 40));
        faculty175_display_draw_line(cx - 10, 226, cx + 10, 226, accent);
        centered_at("EN", cx - 54, 222, rgb(240, 244, 250));
        centered_at("ES", cx + 54, 222, rgb(240, 244, 250));
        centered_at("STREAM TRANSLATE SPEAK", cx, 352, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_ENOCHIAN) {
        for (int i = 0; i < 12; ++i) {
            const float a = ((float)i / 12.0f) * 6.2831853f;
            dot_polar(cx, 232, a, 116, 6, tone(face->hue + i, 28));
            line_polar(cx, 232, a, 42, 116, rgb(54, 46, 72));
        }
        draw_star(cx, 232, 72, accent);
        centered_at("TABLET SIGIL", cx, 352, dim);
        return;
    }
    if (face->id == FACULTY175_FACE_QDAY) {
        centered_at("WHAT IS ASKING", cx, 210, rgb(230, 224, 204));
        centered_at("FOR ATTENTION", cx, 236, rgb(230, 224, 204));
        centered_at("TODAY?", cx, 262, accent);
        centered_at("COMMONPLACE PROMPT", cx, 374, dim);
        return;
    }
    const int breathe = (int)((anim_ms / 80u) % 28u);
    faculty175_display_draw_circle(cx, 230, 78 + breathe / 3, accent);
    faculty175_display_draw_circle(cx, 230, 118 - breathe / 4, rgb(48, 52, 64));
    faculty175_display_fill_circle(cx, 230, 36, tone(face->hue, 35));
    centered_at(face->a, cx, 326, rgb(230, 224, 204));
    centered_at(face->b, cx, 352, dim);
    centered_at(face->c, cx, 378, dim);
}

void faculty175_face_native_draw(const faculty175_native_face_t *face, uint32_t anim_ms)
{
    if (face == NULL) {
        return;
    }
    if (faculty175_lvgl_draw_native_face(face, anim_ms)) {
        return;
    }
    const uint16_t accent = tone(face->hue, 42);
    const uint16_t dim = rgb(132, 142, 154);
    draw_frame(face, accent, dim);
    switch (face->style) {
        case FACULTY175_NATIVE_ANALOG: draw_analog(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_DIGITAL: draw_digital(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_ORACLE: draw_oracle(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_INSTRUMENT: draw_instrument(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_CELESTIAL: draw_celestial(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_RADAR: draw_radar(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_STATUS: draw_status(face, anim_ms, accent, dim); break;
        case FACULTY175_NATIVE_TEXT: draw_text(face, anim_ms, accent, dim); break;
    }
    faculty175_display_flush();
}

bool faculty175_face_native_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    if (id == FACULTY175_FACE_HID) {
        const esp_err_t err = faculty175_km_install_pi_agent();
        ESP_LOGI(TAG, "Pi agent installer action: %s", esp_err_to_name(err));
        return true;
    }
    if (id == FACULTY175_FACE_FOCUS) {
        s_focus_running = !s_focus_running;
        s_focus_started_ms = seed_ms;
        (void)s_focus_started_ms;
        return true;
    }
    s_oracle_nonce = esp_random() ^ seed_ms ^ ((uint32_t)id * 2654435761u);
    if (play_instrument_action(id, seed_ms)) {
        return true;
    }
    return true;
}
