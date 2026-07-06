#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_board.h"
#include "faculty175_charts.h"

#define SYNASTRY_ORRERY_MAX_PEOPLE (1 + FACULTY175_CHART_PROFILE_SLOTS)

typedef struct {
    int user_body;
    int target_body;
    int aspect_deg;
    double orb;
} synastry_aspect_t;

typedef struct {
    faculty175_birth_chart_t chart;
    faculty175_chart_positions_t pos;
    int slot;
    int age_years;
    float fx;
    float fy;
    int x;
    int y;
    int radius;
    uint16_t color;
    bool primary;
    bool active;
} synastry_person_t;

static float s_orrery_x[SYNASTRY_ORRERY_MAX_PEOPLE];
static float s_orrery_y[SYNASTRY_ORRERY_MAX_PEOPLE];
static float s_orrery_vx[SYNASTRY_ORRERY_MAX_PEOPLE];
static float s_orrery_vy[SYNASTRY_ORRERY_MAX_PEOPLE];
static uint32_t s_orrery_signature;
static int s_orrery_count;
static uint32_t s_orrery_last_ms;

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

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

static float aspect_weight(int deg)
{
    switch (deg) {
        case 0: return 1.0f;
        case 60: return 0.64f;
        case 90: return 0.72f;
        case 120: return 0.88f;
        case 180: return 0.76f;
        default: return 0.45f;
    }
}

static float body_weight(int body)
{
    switch (body) {
        case 0:
        case 1:
            return 1.0f;
        case 3:
        case 4:
            return 0.92f;
        case 2:
            return 0.82f;
        case 5:
            return 0.72f;
        case 6:
            return 0.78f;
        default:
            return 0.7f;
    }
}

static float synastry_gravity_between(const faculty175_chart_positions_t *a,
                                      const faculty175_chart_positions_t *b)
{
    if (a == NULL || b == NULL || !a->ok || !b->ok) {
        return 0.0f;
    }
    static const int k_major[] = {0, 60, 90, 120, 180};
    float score = 0.0f;
    for (int ai = 0; ai < FACULTY175_CHART_BODY_COUNT; ++ai) {
        for (int bi = 0; bi < FACULTY175_CHART_BODY_COUNT; ++bi) {
            const double sep = aspect_distance(a->lon[ai], b->lon[bi]);
            for (size_t mi = 0; mi < sizeof(k_major) / sizeof(k_major[0]); ++mi) {
                const double orb = fabs(sep - (double)k_major[mi]);
                if (orb > 6.5) {
                    continue;
                }
                const float exact = 1.0f - (float)(orb / 6.5);
                score += exact * aspect_weight(k_major[mi]) * body_weight(ai) * body_weight(bi);
                break;
            }
        }
    }
    return clamp01(score / 8.5f);
}

static int age_years_for_chart(const faculty175_birth_chart_t *chart)
{
    if (chart == NULL || chart->year == 0) {
        return 0;
    }
    struct tm local = {
        .tm_year = 2026 - 1900,
        .tm_mon = 7 - 1,
        .tm_mday = 6,
    };
    if (astrolabe_time_valid()) {
        astrolabe_time_local(&local);
    }
    int age = (local.tm_year + 1900) - (int)chart->year;
    const int month = local.tm_mon + 1;
    if (month < (int)chart->month || (month == (int)chart->month && local.tm_mday < (int)chart->day)) {
        --age;
    }
    return age < 0 ? 0 : age;
}

static int sphere_radius_for_age(int age_years, bool primary)
{
    float r = 7.0f + sqrtf((float)(age_years < 0 ? 0 : age_years)) * 1.35f;
    if (primary) {
        r += 2.5f;
    }
    if (r < 8.0f) {
        r = 8.0f;
    }
    if (r > 22.0f) {
        r = 22.0f;
    }
    return (int)lrintf(r);
}

static uint16_t person_color(faculty175_chart_role_t role, bool primary)
{
    if (primary || role == FACULTY175_CHART_ROLE_SELF) {
        return c(122, 214, 255);
    }
    if (role == FACULTY175_CHART_ROLE_CHILD) {
        return c(148, 236, 178);
    }
    return c(236, 204, 255);
}

static void radial_line(int cx, int cy, float angle, int r0, int r1, uint16_t color)
{
    const int x0 = cx + (int)lrintf(cosf(angle) * (float)r0);
    const int y0 = cy + (int)lrintf(sinf(angle) * (float)r0);
    const int x1 = cx + (int)lrintf(cosf(angle) * (float)r1);
    const int y1 = cy + (int)lrintf(sinf(angle) * (float)r1);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
}

static bool add_orrery_person(synastry_person_t *people,
                              int *count,
                              int cap,
                              const faculty175_birth_chart_t *chart,
                              const faculty175_chart_positions_t *pos,
                              int slot,
                              bool primary,
                              bool active)
{
    if (people == NULL || count == NULL || chart == NULL || pos == NULL || *count >= cap || !pos->ok) {
        return false;
    }
    synastry_person_t *p = &people[(*count)++];
    memset(p, 0, sizeof(*p));
    p->chart = *chart;
    p->pos = *pos;
    p->slot = slot;
    p->primary = primary;
    p->active = active;
    p->age_years = age_years_for_chart(chart);
    p->radius = sphere_radius_for_age(p->age_years, primary);
    p->color = person_color(chart->role, primary);
    return true;
}

static int build_orrery_people(synastry_person_t *people,
                               int cap,
                               const faculty175_birth_chart_t *user,
                               const faculty175_chart_positions_t *user_pos)
{
    int count = 0;
    (void)add_orrery_person(people, &count, cap, user, user_pos, -1, true, false);
    const int active_slot = faculty175_charts_active_slot();
    for (int slot = 0; slot < FACULTY175_CHART_PROFILE_SLOTS && count < cap; ++slot) {
        faculty175_birth_chart_t profile = {};
        faculty175_chart_positions_t pos = {};
        if (!faculty175_charts_profile_get(slot, &profile) ||
            !faculty175_charts_birth_positions(&profile, &pos)) {
            continue;
        }
        (void)add_orrery_person(people, &count, cap, &profile, &pos, slot, false, slot == active_slot);
    }
    return count;
}

static uint32_t orrery_signature(const synastry_person_t *people, int count)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < count; ++i) {
        const uint8_t *name = (const uint8_t *)people[i].chart.name;
        for (size_t j = 0; j < sizeof(people[i].chart.name) && name[j] != '\0'; ++j) {
            h ^= name[j];
            h *= 16777619u;
        }
        h ^= (uint32_t)people[i].chart.year;
        h *= 16777619u;
        h ^= (uint32_t)people[i].slot;
        h *= 16777619u;
    }
    return h;
}

static void layout_orrery_people(synastry_person_t *people, int count, int cx, int cy, uint32_t anim_ms)
{
    if (people == NULL || count <= 0) {
        return;
    }
    const uint32_t sig = orrery_signature(people, count);
    const float primary_sun = people[0].pos.ok ? (float)norm360(people[0].pos.lon[0]) : 0.0f;
    const float step = count > 1 ? 6.2831853f / (float)(count - 1) : 6.2831853f;

    if (s_orrery_signature != sig || s_orrery_count != count) {
        memset(s_orrery_vx, 0, sizeof(s_orrery_vx));
        memset(s_orrery_vy, 0, sizeof(s_orrery_vy));
        s_orrery_x[0] = (float)cx;
        s_orrery_y[0] = (float)cy;
        for (int i = 1; i < count; ++i) {
            const float natal_phase = people[i].pos.ok ?
                (float)norm360(people[i].pos.lon[0] - primary_sun) * 0.0174532925f : 0.0f;
            const float a = -1.5707963f + step * (float)(i - 1) + natal_phase * 0.18f;
            const float gravity = synastry_gravity_between(&people[0].pos, &people[i].pos);
            const float r = 156.0f - gravity * 54.0f;
            s_orrery_x[i] = (float)cx + cosf(a) * r;
            s_orrery_y[i] = (float)cy + sinf(a) * r;
        }
        s_orrery_signature = sig;
        s_orrery_count = count;
        s_orrery_last_ms = anim_ms;
    }

    float dt = 0.016f;
    if (s_orrery_last_ms != 0 && anim_ms > s_orrery_last_ms) {
        dt = (float)(anim_ms - s_orrery_last_ms) / 1000.0f;
        if (dt > 0.05f) {
            dt = 0.05f;
        }
    }
    s_orrery_last_ms = anim_ms;

    float ax[SYNASTRY_ORRERY_MAX_PEOPLE] = {};
    float ay[SYNASTRY_ORRERY_MAX_PEOPLE] = {};
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            float dx = s_orrery_x[j] - s_orrery_x[i];
            float dy = s_orrery_y[j] - s_orrery_y[i];
            float dist2 = dx * dx + dy * dy;
            if (dist2 < 4.0f) {
                const float a = -1.5707963f + step * (float)j;
                dx = cosf(a) * 2.0f;
                dy = sinf(a) * 2.0f;
                dist2 = 4.0f;
            }
            const float dist = sqrtf(dist2);
            const float ux = dx / dist;
            const float uy = dy / dist;
            const float gravity = synastry_gravity_between(&people[i].pos, &people[j].pos);
            const float touch = (float)(people[i].radius + people[j].radius) + 14.0f;
            const float attraction = gravity * (18.0f + dist * 0.035f);
            float repulsion = 8600.0f / dist2;
            if (dist < touch) {
                repulsion += (touch - dist) * 9.0f;
            }
            const float force = attraction - repulsion;
            if (!people[i].primary) {
                ax[i] += ux * force;
                ay[i] += uy * force;
            }
            if (!people[j].primary) {
                ax[j] -= ux * force;
                ay[j] -= uy * force;
            }
        }
    }
    for (int i = 1; i < count; ++i) {
        const float dx = s_orrery_x[i] - (float)cx;
        const float dy = s_orrery_y[i] - (float)cy;
        const float dist = sqrtf(dx * dx + dy * dy);
        ax[i] -= dx * 0.42f;
        ay[i] -= dy * 0.42f;
        const float max_r = 188.0f - (float)people[i].radius;
        if (dist > max_r && dist > 1.0f) {
            ax[i] -= (dx / dist) * (dist - max_r) * 55.0f;
            ay[i] -= (dy / dist) * (dist - max_r) * 55.0f;
        }
    }
    s_orrery_x[0] = (float)cx;
    s_orrery_y[0] = (float)cy;
    s_orrery_vx[0] = 0.0f;
    s_orrery_vy[0] = 0.0f;
    for (int i = 1; i < count; ++i) {
        s_orrery_vx[i] = (s_orrery_vx[i] + ax[i] * dt) * 0.88f;
        s_orrery_vy[i] = (s_orrery_vy[i] + ay[i] * dt) * 0.88f;
        s_orrery_x[i] += s_orrery_vx[i] * dt * 36.0f;
        s_orrery_y[i] += s_orrery_vy[i] * dt * 36.0f;
    }
    for (int i = 0; i < count; ++i) {
        people[i].fx = s_orrery_x[i];
        people[i].fy = s_orrery_y[i];
        people[i].x = (int)lrintf(s_orrery_x[i]);
        people[i].y = (int)lrintf(s_orrery_y[i]);
    }
}

static void draw_gravity_bonds(const synastry_person_t *people, int count)
{
    if (people == NULL) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            const float gravity = synastry_gravity_between(&people[i].pos, &people[j].pos);
            if (gravity < 0.08f) {
                continue;
            }
            const uint8_t v = (uint8_t)(95.0f + gravity * 140.0f);
            const uint16_t col = c(v, (uint8_t)(125.0f + gravity * 85.0f),
                                   (uint8_t)(170.0f + gravity * 70.0f));
            faculty175_display_draw_line(people[i].x, people[i].y, people[j].x, people[j].y, col);
            if (gravity > 0.62f) {
                faculty175_display_draw_line(people[i].x + 1, people[i].y, people[j].x + 1, people[j].y, col);
            }
        }
    }
}

static void draw_person_sphere(const synastry_person_t *p)
{
    if (p == NULL) {
        return;
    }
    faculty175_display_fill_circle(p->x + 2, p->y + 3, p->radius + 2, c(5, 7, 14));
    faculty175_display_fill_circle(p->x, p->y, p->radius, p->color);
    faculty175_display_draw_circle(p->x, p->y, p->radius + 1,
                                   p->active ? c(255, 230, 150) : c(230, 238, 255));
    faculty175_display_fill_circle(p->x - p->radius / 3, p->y - p->radius / 3,
                                   p->radius / 4 > 2 ? p->radius / 4 : 2,
                                   c(255, 255, 255));
    char initial[2] = {p->chart.name[0] != '\0' ? p->chart.name[0] : '?', '\0'};
    faculty175_display_draw_text(initial, p->x - 3, p->y - 4, c(8, 10, 18));
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
    const int r_mid = 146;
    const int r_inner = 54;
    const uint16_t bg = c(7, 9, 18);
    const uint16_t ring = c(52, 58, 82);
    const uint16_t spoke = c(62, 72, 98);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_circle(cx, cy, 216, c(24, 28, 42));
    faculty175_display_draw_circle(cx, cy, r_outer, ring);
    faculty175_display_draw_circle(cx, cy, r_mid, ring);
    faculty175_display_draw_circle(cx, cy, r_inner, ring);
    for (int s = 0; s < 12; ++s) {
        radial_line(cx, cy, -1.5707963f + (float)s * 6.2831853f / 12.0f, r_inner, r_outer,
                    s % 3 == 0 ? c(100, 112, 144) : spoke);
    }

    synastry_person_t people[SYNASTRY_ORRERY_MAX_PEOPLE] = {};
    const int person_count = build_orrery_people(people, SYNASTRY_ORRERY_MAX_PEOPLE, &user, &user_pos);
    layout_orrery_people(people, person_count, cx, cy, anim_ms);
    draw_gravity_bonds(people, person_count);
    for (int i = 0; i < person_count; ++i) {
        draw_person_sphere(&people[i]);
    }

    faculty175_display_fill_circle(cx, cy, 32, c(9, 10, 20));
    faculty175_display_draw_circle(cx, cy, 33, c(86, 72, 112));
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
