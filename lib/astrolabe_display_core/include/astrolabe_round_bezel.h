#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t (*astrolabe_bezel_rgb565_fn)(uint8_t r, uint8_t g, uint8_t b, void *user);
typedef void (*astrolabe_bezel_line_fn)(int x0, int y0, int x1, int y1, uint16_t color, void *user);
typedef void (*astrolabe_bezel_circle_fn)(int cx, int cy, int r, uint16_t color, void *user);
typedef void (*astrolabe_bezel_fill_circle_fn)(int cx, int cy, int r, uint16_t color, void *user);
typedef void (*astrolabe_bezel_text_fn)(const char *text, int x, int y, uint16_t color, void *user);

typedef struct {
    int width;
    int height;
    int cx;
    int cy;
    int outer_radius;
    int inner_radius;
    astrolabe_bezel_rgb565_fn rgb565;
    astrolabe_bezel_line_fn draw_line;
    astrolabe_bezel_circle_fn draw_circle;
    astrolabe_bezel_fill_circle_fn fill_circle;
    astrolabe_bezel_text_fn draw_text;
    void *user;
} astrolabe_round_bezel_ops_t;

typedef struct {
    const uint8_t *levels;
    const uint8_t *stream_mask;
    size_t count;
    uint16_t idle_color;
    uint16_t stream_color;
} astrolabe_round_bezel_wave_t;

typedef enum {
    ASTROLABE_BEZEL_LABEL_TOP = 0,
    ASTROLABE_BEZEL_LABEL_BOTTOM = 1,
} astrolabe_round_bezel_label_position_t;

typedef struct {
    const char *text;
    int radius;
    uint16_t color;
    astrolabe_round_bezel_label_position_t position;
    uint32_t scroll_ms;
} astrolabe_round_bezel_label_t;

void astrolabe_round_bezel_draw(const astrolabe_round_bezel_ops_t *ops,
                                uint16_t ring_color,
                                uint16_t tick_color,
                                const astrolabe_round_bezel_wave_t *wave);
void astrolabe_round_bezel_draw_label(const astrolabe_round_bezel_ops_t *ops,
                                      const astrolabe_round_bezel_label_t *label);

#ifdef __cplusplus
}
#endif
