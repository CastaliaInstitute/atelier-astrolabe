#include "astrolabe_round_bezel.h"

#include <math.h>
#include <string.h>

static void polar_line(const astrolabe_round_bezel_ops_t *ops,
                       float angle,
                       int r0,
                       int r1,
                       uint16_t color)
{
    const int x0 = ops->cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = ops->cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = ops->cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = ops->cy + (int)lrintf(sinf(angle) * (float)r1);
    ops->draw_line(x0, y0, x1, y1, color, ops->user);
}

static void draw_wave(const astrolabe_round_bezel_ops_t *ops, const astrolabe_round_bezel_wave_t *wave)
{
    if (wave == NULL || wave->levels == NULL || wave->count == 0) {
        return;
    }
    const size_t max_cols = wave->count > 160 ? 160 : wave->count;
    for (size_t i = 0; i < max_cols; ++i) {
        const float a = -1.5707963f + ((float)i / (float)max_cols) * 6.2831853f;
        const int level = wave->levels[i] > 64 ? 64 : wave->levels[i];
        const int base = ops->inner_radius + 1;
        const int peak = base + 2 + level / 5;
        const uint16_t color = (wave->stream_mask != NULL && wave->stream_mask[i] != 0) ? wave->stream_color : wave->idle_color;
        polar_line(ops, a, base, peak, color);
    }
}

void astrolabe_round_bezel_draw(const astrolabe_round_bezel_ops_t *ops,
                                uint16_t ring_color,
                                uint16_t tick_color,
                                const astrolabe_round_bezel_wave_t *wave)
{
    if (ops == NULL || ops->draw_line == NULL || ops->draw_circle == NULL || ops->rgb565 == NULL) {
        return;
    }

    ops->draw_circle(ops->cx, ops->cy, ops->outer_radius, ring_color, ops->user);
    ops->draw_circle(ops->cx, ops->cy, ops->outer_radius - 1, ring_color, ops->user);
    ops->draw_circle(ops->cx, ops->cy, ops->inner_radius, ring_color, ops->user);

    for (int i = 0; i < 96; ++i) {
        const float a = ((float)i / 96.0f) * 6.2831853f - 1.5707963f;
        const int major = (i % 8) == 0;
        const int minor = (i % 2) == 0;
        const int r0 = ops->outer_radius - (major ? 16 : (minor ? 10 : 6));
        polar_line(ops, a, r0, ops->outer_radius, major ? ring_color : tick_color);
    }

    draw_wave(ops, wave);
}

void astrolabe_round_bezel_draw_label(const astrolabe_round_bezel_ops_t *ops,
                                      const astrolabe_round_bezel_label_t *label)
{
    if (ops == NULL || label == NULL || ops->draw_text == NULL || label->text == NULL || label->text[0] == '\0' ||
        label->radius <= 0) {
        return;
    }

    const int len = (int)strlen(label->text);
    if (len <= 0) {
        return;
    }

    const float char_w = 6.0f;
    const float full = 6.2831853f;
    const float visible_span = label->position == ASTROLABE_BEZEL_LABEL_TOP ? 2.35f : 2.18f;
    const float text_span = ((float)len * char_w) / (float)label->radius;
    float offset = 0.0f;
    if (label->scroll_ms > 0 && text_span > visible_span) {
        const uint32_t period = 6000u + (uint32_t)len * 80u;
        const float phase = (float)(label->scroll_ms % period) / (float)period;
        offset = (text_span - visible_span) * phase;
    }

    const float center = label->position == ASTROLABE_BEZEL_LABEL_TOP ? -1.5707963f : 1.5707963f;
    const float start = center - (text_span * 0.5f) - offset;
    for (int i = 0; i < len; ++i) {
        float t = start + ((float)i + 0.5f) * char_w / (float)label->radius;
        while (t < -3.1415926f) {
            t += full;
        }
        while (t > 3.1415926f) {
            t -= full;
        }
        if (fabsf(t - center) > visible_span * 0.5f) {
            continue;
        }
        const int px = ops->cx + (int)lrintf(cosf(t) * (float)label->radius) - 2;
        const int py = ops->cy + (int)lrintf(sinf(t) * (float)label->radius) - 3;
        char ch[2] = {label->text[i], '\0'};
        ops->draw_text(ch, px, py, label->color, ops->user);
    }
}
