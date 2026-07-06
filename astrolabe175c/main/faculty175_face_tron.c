#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"

#include "faculty175_board.h"

#define TRON_GRID_W 58
#define TRON_GRID_H 58
#define TRON_CELL 8
#define TRON_ORIGIN_X 1
#define TRON_ORIGIN_Y 1
#define TRON_STEP_MS 86
#define TRON_RESET_MS 1300
#define TRON_TILT_DEADZONE_DEG 4.0f
#define TRON_TILT_FULLSCALE_DEG 28.0f

typedef enum {
    TRON_UP = 0,
    TRON_RIGHT = 1,
    TRON_DOWN = 2,
    TRON_LEFT = 3,
} tron_dir_t;

typedef struct {
    int x;
    int y;
    tron_dir_t dir;
    uint16_t color;
    uint16_t glow;
    bool alive;
    uint8_t trail;
} tron_cycle_t;

__attribute__((weak)) bool faculty175_motion_pitch_roll(float *pitch_deg, float *roll_deg)
{
    (void)pitch_deg;
    (void)roll_deg;
    return false;
}

static uint8_t s_trail[TRON_GRID_W * TRON_GRID_H];
static tron_cycle_t s_player;
static tron_cycle_t s_rival;
static uint32_t s_last_step_ms;
static uint32_t s_crash_ms;
static bool s_ready;
static bool s_motion_seen;
static float s_pitch_deg;
static float s_roll_deg;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static int cell_index(int x, int y)
{
    return y * TRON_GRID_W + x;
}

static bool occupied(int x, int y)
{
    if (x < 0 || x >= TRON_GRID_W || y < 0 || y >= TRON_GRID_H) {
        return true;
    }
    return s_trail[cell_index(x, y)] != 0;
}

static void occupy(int x, int y, uint8_t trail)
{
    if (x < 0 || x >= TRON_GRID_W || y < 0 || y >= TRON_GRID_H) {
        return;
    }
    s_trail[cell_index(x, y)] = trail;
}

static bool opposite(tron_dir_t a, tron_dir_t b)
{
    return ((int)a + 2) % 4 == (int)b;
}

static void step_xy(tron_dir_t dir, int *x, int *y)
{
    switch (dir) {
        case TRON_UP: --*y; break;
        case TRON_RIGHT: ++*x; break;
        case TRON_DOWN: ++*y; break;
        case TRON_LEFT: --*x; break;
    }
}

void faculty175_face_tron_reset(void)
{
    memset(s_trail, 0, sizeof(s_trail));
    s_player = (tron_cycle_t) {
        .x = TRON_GRID_W / 2 - 8,
        .y = TRON_GRID_H - 6,
        .dir = TRON_UP,
        .color = 0x0000,
        .glow = 0x0000,
        .alive = true,
        .trail = 1,
    };
    s_rival = (tron_cycle_t) {
        .x = TRON_GRID_W / 2 + 8,
        .y = TRON_GRID_H - 6,
        .dir = TRON_UP,
        .color = 0x0000,
        .glow = 0x0000,
        .alive = true,
        .trail = 2,
    };
    s_player.color = rgb(44, 226, 255);
    s_player.glow = rgb(10, 82, 110);
    s_rival.color = rgb(255, 86, 220);
    s_rival.glow = rgb(100, 16, 84);
    occupy(s_player.x, s_player.y, s_player.trail);
    occupy(s_rival.x, s_rival.y, s_rival.trail);
    s_last_step_ms = 0;
    s_crash_ms = 0;
    s_ready = true;
}

static void set_player_dir_from_motion(uint32_t anim_ms)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    tron_dir_t next = s_player.dir;
    if (faculty175_motion_pitch_roll(&pitch, &roll)) {
        s_motion_seen = true;
        s_pitch_deg = (s_pitch_deg * 0.65f) + (pitch * 0.35f);
        s_roll_deg = (s_roll_deg * 0.65f) + (roll * 0.35f);
        pitch = s_pitch_deg;
        roll = s_roll_deg;
        if (fabsf(roll) >= fabsf(pitch) && fabsf(roll) > TRON_TILT_DEADZONE_DEG) {
            next = roll > 0.0f ? TRON_RIGHT : TRON_LEFT;
        } else if (fabsf(pitch) > TRON_TILT_DEADZONE_DEG) {
            next = pitch > 0.0f ? TRON_DOWN : TRON_UP;
        }
    } else if (!s_motion_seen) {
        const uint32_t phase = (anim_ms / 900u) & 3u;
        next = (tron_dir_t)phase;
    }
    if (!opposite(next, s_player.dir)) {
        s_player.dir = next;
    }
}

static int free_score(int x, int y, tron_dir_t dir)
{
    int score = 0;
    for (int i = 0; i < 7; ++i) {
        step_xy(dir, &x, &y);
        if (occupied(x, y)) {
            break;
        }
        ++score;
    }
    return score;
}

static void set_rival_dir(void)
{
    tron_dir_t best = s_rival.dir;
    int best_score = -1;
    for (int turn = -1; turn <= 1; ++turn) {
        tron_dir_t d = (tron_dir_t)(((int)s_rival.dir + turn + 4) % 4);
        if (opposite(d, s_rival.dir)) {
            continue;
        }
        int tx = s_rival.x;
        int ty = s_rival.y;
        step_xy(d, &tx, &ty);
        if (occupied(tx, ty)) {
            continue;
        }
        int score = free_score(s_rival.x, s_rival.y, d);
        if ((esp_random() & 3u) == 0u) {
            score += (int)(esp_random() & 1u);
        }
        if (score > best_score) {
            best_score = score;
            best = d;
        }
    }
    s_rival.dir = best;
}

static void move_cycle(tron_cycle_t *cycle)
{
    if (cycle == NULL || !cycle->alive) {
        return;
    }
    int nx = cycle->x;
    int ny = cycle->y;
    step_xy(cycle->dir, &nx, &ny);
    if (occupied(nx, ny)) {
        cycle->alive = false;
        return;
    }
    cycle->x = nx;
    cycle->y = ny;
    occupy(nx, ny, cycle->trail);
}

static uint8_t trail_at(int x, int y)
{
    if (x < 0 || x >= TRON_GRID_W || y < 0 || y >= TRON_GRID_H) {
        return 0;
    }
    return s_trail[cell_index(x, y)];
}

static uint16_t trail_color(uint8_t trail, bool glow)
{
    if (trail == s_player.trail) {
        return glow ? s_player.glow : s_player.color;
    }
    if (trail == s_rival.trail) {
        return glow ? s_rival.glow : s_rival.color;
    }
    return glow ? rgb(40, 58, 80) : rgb(120, 150, 190);
}

static void update_game(uint32_t anim_ms)
{
    float pitch = 0.0f;
    float roll = 0.0f;
    if (faculty175_motion_pitch_roll(&pitch, &roll)) {
        s_motion_seen = true;
        s_pitch_deg = (s_pitch_deg * 0.70f) + (pitch * 0.30f);
        s_roll_deg = (s_roll_deg * 0.70f) + (roll * 0.30f);
    }
    if (!s_ready) {
        faculty175_face_tron_reset();
    }
    if (!s_player.alive || !s_rival.alive) {
        if (s_crash_ms == 0) {
            s_crash_ms = anim_ms;
        } else if (anim_ms - s_crash_ms > TRON_RESET_MS) {
            faculty175_face_tron_reset();
        }
        return;
    }
    if (s_last_step_ms != 0 && anim_ms - s_last_step_ms < TRON_STEP_MS) {
        return;
    }
    s_last_step_ms = anim_ms;
    set_player_dir_from_motion(anim_ms);
    set_rival_dir();
    move_cycle(&s_player);
    move_cycle(&s_rival);
}

static void draw_thick_line(int x0, int y0, int x1, int y1, uint16_t color, int width)
{
    const int half = width / 2;
    for (int o = -half; o <= half; ++o) {
        if (abs(x1 - x0) >= abs(y1 - y0)) {
            faculty175_display_draw_line(x0, y0 + o, x1, y1 + o, color);
        } else {
            faculty175_display_draw_line(x0 + o, y0, x1 + o, y1, color);
        }
    }
}

static void cell_center(int x, int y, int *px, int *py)
{
    *px = TRON_ORIGIN_X + x * TRON_CELL + TRON_CELL / 2;
    *py = TRON_ORIGIN_Y + y * TRON_CELL + TRON_CELL / 2;
}

static void draw_trail_segment(int x0, int y0, int x1, int y1, uint8_t trail)
{
    int px0 = 0;
    int py0 = 0;
    int px1 = 0;
    int py1 = 0;
    cell_center(x0, y0, &px0, &py0);
    cell_center(x1, y1, &px1, &py1);
    draw_thick_line(px0, py0, px1, py1, trail_color(trail, true), 7);
    draw_thick_line(px0, py0, px1, py1, trail_color(trail, false), 3);
}

static void draw_trails(void)
{
    for (int y = 0; y < TRON_GRID_H; ++y) {
        for (int x = 0; x < TRON_GRID_W; ++x) {
            const uint8_t trail = trail_at(x, y);
            if (trail == 0) {
                continue;
            }
            bool linked = false;
            if (trail_at(x + 1, y) == trail) {
                draw_trail_segment(x, y, x + 1, y, trail);
                linked = true;
            }
            if (trail_at(x, y + 1) == trail) {
                draw_trail_segment(x, y, x, y + 1, trail);
                linked = true;
            }
            if (!linked && trail_at(x - 1, y) != trail && trail_at(x, y - 1) != trail) {
                int px = 0;
                int py = 0;
                cell_center(x, y, &px, &py);
                faculty175_display_fill_circle(px, py, 4, trail_color(trail, false));
            }
        }
    }
}

static void draw_cycle(const tron_cycle_t *cycle)
{
    if (cycle == NULL) {
        return;
    }
    const int px = TRON_ORIGIN_X + cycle->x * TRON_CELL + TRON_CELL / 2;
    const int py = TRON_ORIGIN_Y + cycle->y * TRON_CELL + TRON_CELL / 2;
    faculty175_display_fill_circle(px, py, 10, cycle->glow);
    faculty175_display_fill_circle(px, py, 6, cycle->alive ? cycle->color : rgb(255, 240, 190));
    int hx = px;
    int hy = py;
    switch (cycle->dir) {
        case TRON_UP: hy -= 11; break;
        case TRON_RIGHT: hx += 11; break;
        case TRON_DOWN: hy += 11; break;
        case TRON_LEFT: hx -= 11; break;
    }
    faculty175_display_draw_line(px, py, hx, hy, rgb(245, 252, 255));
}

static float clampf_local(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void draw_bezel_motion_ring(void)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    faculty175_display_draw_circle(cx, cy, 228, rgb(12, 58, 84));
    faculty175_display_draw_circle(cx, cy, 221, rgb(9, 34, 58));

    const float roll = clampf_local(s_roll_deg / TRON_TILT_FULLSCALE_DEG, -1.0f, 1.0f);
    const float pitch = clampf_local(s_pitch_deg / TRON_TILT_FULLSCALE_DEG, -1.0f, 1.0f);
    const int roll_x = cx + (int)lrintf(roll * 196.0f);
    const int pitch_y = cy + (int)lrintf(pitch * 196.0f);

    draw_thick_line(cx, cy + 216, roll_x, cy + 216, rgb(44, 226, 255), 5);
    draw_thick_line(cx - 216, cy, cx - 216, pitch_y, rgb(255, 86, 220), 5);
    faculty175_display_fill_circle(roll_x, cy + 216, 8, rgb(44, 226, 255));
    faculty175_display_fill_circle(cx - 216, pitch_y, 8, rgb(255, 86, 220));
}

void faculty175_face_tron_draw(uint32_t anim_ms)
{
    update_game(anim_ms);

    faculty175_display_fill_rgb565(rgb(1, 4, 12));
    for (int i = 0; i <= TRON_GRID_W; i += 4) {
        const int x = TRON_ORIGIN_X + i * TRON_CELL;
        faculty175_display_draw_line(x, 0, x, FACULTY175_LCD_H - 1, rgb(4, 24, 44));
    }
    for (int i = 0; i <= TRON_GRID_H; i += 4) {
        const int y = TRON_ORIGIN_Y + i * TRON_CELL;
        faculty175_display_draw_line(0, y, FACULTY175_LCD_W - 1, y, rgb(4, 24, 44));
    }
    faculty175_display_draw_line(0, 0, FACULTY175_LCD_W - 1, 0, rgb(23, 132, 178));
    faculty175_display_draw_line(0, FACULTY175_LCD_H - 1, FACULTY175_LCD_W - 1, FACULTY175_LCD_H - 1, rgb(23, 132, 178));
    faculty175_display_draw_line(0, 0, 0, FACULTY175_LCD_H - 1, rgb(23, 132, 178));
    faculty175_display_draw_line(FACULTY175_LCD_W - 1, 0, FACULTY175_LCD_W - 1, FACULTY175_LCD_H - 1, rgb(23, 132, 178));

    draw_trails();

    draw_cycle(&s_player);
    draw_cycle(&s_rival);
    draw_bezel_motion_ring();
    if (!s_player.alive || !s_rival.alive) {
        faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 92, rgb(255, 236, 120));
        faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 116, rgb(42, 236, 255));
    }
    faculty175_display_flush();
}
