#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "faculty175_board.h"
#include "faculty175_charts.h"

typedef struct {
    int user_body;
    int target_body;
    int aspect_deg;
    double orb;
} synastry_aspect_t;

static uint16_t c(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static double norm360(double v)
{
    v = fmod(v, 360.0);
    if (v < 0.0) {
        v += 360.0;
    }
    return v;
}

static double aspect_distance(double a, double b)
{
    double d = fabs(norm360(a) - norm360(b));
    return d > 180.0 ? 360.0 - d : d;
}

static float angle_for_lon(double lon)
{
    return 3.1415926f + (float)norm360(lon) * 0.0174532925f;
}

static uint16_t aspect_color(int deg)
{
    switch (deg) {
        case 0:
            return c(255, 226, 142);
        case 60:
            return c(120, 210, 250);
        case 90:
            return c(245, 100, 92);
        case 120:
            return c(120, 226, 154);
        case 180:
            return c(198, 138, 245);
        default:
            return c(150, 160, 184);
    }
}

static void centered_at(const char *text, int cx, int y, uint16_t color)
{
    int len = 0;
    while (text != NULL && text[len] != '\0') {
        ++len;
    }
    if (len > 0) {
        faculty175_display_draw_text(text, cx - len * 3, y, color);
    }
}

static void radial_line(int cx, int cy, float angle, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static void draw_body_marker(const faculty175_chart_positions_t *pos,
                             int body,
                             int cx,
                             int cy,
                             int radius,
                             uint16_t color,
                             bool outer)
{
    const float a = angle_for_lon(pos->lon[body]);
    const int x = cx + (int)lrintf(cosf(a) * (float)radius);
    const int y = cy + (int)lrintf(sinf(a) * (float)radius);
    faculty175_display_fill_circle(x, y, body == 0 ? 7 : 5, c(8, 10, 18));
    faculty175_display_draw_circle(x, y, body == 0 ? 8 : 6, color);
    if (body == 0) {
        faculty175_display_fill_circle(x, y, 2, color);
    }
    const int label_r = outer ? radius + 18 : radius - 26;
    const int lx = cx + (int)lrintf(cosf(a) * (float)label_r);
    const int ly = cy + (int)lrintf(sinf(a) * (float)label_r);
    centered_at(faculty175_charts_body_label(body), lx, ly - 3, color);
}

static int rebuild_aspects(const faculty175_chart_positions_t *user,
                           const faculty175_chart_positions_t *target,
                           synastry_aspect_t *out,
                           int cap)
{
    static const int k_major[] = {0, 60, 90, 120, 180};
    int count = 0;
    for (int ub = 0; ub < FACULTY175_CHART_BODY_COUNT; ++ub) {
        for (int tb = 0; tb < FACULTY175_CHART_BODY_COUNT; ++tb) {
            const double sep = aspect_distance(user->lon[ub], target->lon[tb]);
            for (size_t ai = 0; ai < sizeof(k_major) / sizeof(k_major[0]); ++ai) {
                const double orb = fabs(sep - (double)k_major[ai]);
                if (orb > 4.5) {
                    continue;
                }
                synastry_aspect_t a = {
                    .user_body = ub,
                    .target_body = tb,
                    .aspect_deg = k_major[ai],
                    .orb = orb,
                };
                int ins = count < cap ? count : cap;
                for (int k = 0; k < ins; ++k) {
                    if (a.orb < out[k].orb) {
                        ins = k;
                        break;
                    }
                }
                if (count < cap) {
                    ++count;
                }
                if (ins < cap) {
                    for (int k = count - 1; k > ins; --k) {
                        out[k] = out[k - 1];
                    }
                    out[ins] = a;
                }
                break;
            }
        }
    }
    return count;
}

static void draw_aspects(const faculty175_chart_positions_t *user,
                         const faculty175_chart_positions_t *target,
                         const synastry_aspect_t *aspects,
                         int count,
                         int cx,
                         int cy,
                         int r_user,
                         int r_target)
{
    for (int i = 0; i < count && i < 8; ++i) {
        const synastry_aspect_t *a = &aspects[i];
        const float au = angle_for_lon(user->lon[a->user_body]);
        const float at = angle_for_lon(target->lon[a->target_body]);
        const int x0 = cx + (int)lrintf(cosf(au) * (float)r_user);
        const int y0 = cy + (int)lrintf(sinf(au) * (float)r_user);
        const int x1 = cx + (int)lrintf(cosf(at) * (float)r_target);
        const int y1 = cy + (int)lrintf(sinf(at) * (float)r_target);
        faculty175_display_draw_line(x0, y0, x1, y1, aspect_color(a->aspect_deg));
    }
}

static const char *aspect_word(int deg)
{
    switch (deg) {
        case 0:
            return "conj";
        case 60:
            return "sext";
        case 90:
            return "sq";
        case 120:
            return "tri";
        case 180:
            return "opp";
        default:
            return "asp";
    }
}

void faculty175_face_synastry_draw(uint32_t anim_ms)
{
    (void)anim_ms;
    faculty175_charts_ensure_family_seed();

    faculty175_birth_chart_t user = {};
    faculty175_birth_chart_t target = {};
    faculty175_chart_positions_t user_pos = {};
    faculty175_chart_positions_t target_pos = {};
    if (!faculty175_charts_primary(&user) || !faculty175_charts_active(&target) ||
        !faculty175_charts_birth_positions(&user, &user_pos) ||
        !faculty175_charts_birth_positions(&target, &target_pos)) {
        faculty175_display_fill_rgb565(c(8, 9, 18));
        faculty175_display_draw_bezel_label("SYNASTRY", false, 222, anim_ms, c(220, 224, 244));
        faculty175_display_draw_centered_text("CHART DATA NEEDED", 190, c(150, 158, 184));
        faculty175_display_draw_bezel_label("serial: charts seed", true, 222, anim_ms, c(150, 158, 184));
        faculty175_display_flush();
        return;
    }

    synastry_aspect_t aspects[12] = {};
    const int aspect_count = rebuild_aspects(&user_pos, &target_pos, aspects, 12);
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 4;
    const int r_outer = 204;
    const int r_target = 152;
    const int r_user = 104;
    const int r_inner = 72;
    const uint16_t bg = c(7, 9, 18);
    const uint16_t ring = c(52, 58, 82);
    const uint16_t spoke = c(62, 72, 98);
    const uint16_t user_col = c(116, 208, 255);
    const uint16_t target_col = c(236, 204, 255);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 216, c(24, 28, 42));
    faculty175_display_draw_circle(cx, cy, r_outer, ring);
    faculty175_display_draw_circle(cx, cy, r_target + 20, ring);
    faculty175_display_draw_circle(cx, cy, r_user + 18, ring);
    faculty175_display_draw_circle(cx, cy, r_inner, ring);
    for (int s = 0; s < 12; ++s) {
        radial_line(cx, cy, -1.5707963f + (float)s * 6.2831853f / 12.0f, r_inner, r_outer,
                    s % 3 == 0 ? c(100, 112, 144) : spoke);
    }

    draw_aspects(&user_pos, &target_pos, aspects, aspect_count, cx, cy, r_user, r_target);
    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        draw_body_marker(&target_pos, i, cx, cy, r_target, target_col, true);
        draw_body_marker(&user_pos, i, cx, cy, r_user, user_col, false);
    }

    faculty175_display_fill_circle(cx, cy, 39, c(9, 10, 20));
    faculty175_display_draw_circle(cx, cy, 40, c(86, 72, 112));
    faculty175_display_draw_circle(cx, cy, 32, c(36, 44, 66));
    char header[80];
    snprintf(header, sizeof(header), "%s + %s", user.name, target.name);
    faculty175_display_draw_bezel_label("SYNASTRY", false, 222, anim_ms, c(230, 228, 246));
    faculty175_display_draw_bezel_label(header, false, 204, anim_ms, c(178, 188, 218));

    char line[96];
    if (aspect_count > 0) {
        const synastry_aspect_t *a = &aspects[0];
        snprintf(line, sizeof(line), "%s %s %s  orb %.1f",
                 faculty175_charts_body_label(a->user_body), aspect_word(a->aspect_deg),
                 faculty175_charts_body_label(a->target_body), a->orb);
    } else {
        snprintf(line, sizeof(line), "%s %s  %s %s", user.name,
                 faculty175_charts_zodiac_abbr(user_pos.lon[0]), target.name,
                 faculty175_charts_zodiac_abbr(target_pos.lon[0]));
    }
    faculty175_display_draw_bezel_label(line, true, 222, anim_ms, c(196, 204, 226));
    faculty175_display_flush();
}
