#include "faculty175_lvgl.h"

#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "src/libs/qrcode/lv_qrcode.h"

#define STBI_NO_STDIO
#include "stb_image.h"

#include "faculty175_board.h"
#include "faculty175_apocalypso.h"
#include "faculty175_charts.h"
#include "faculty175_device_settings.h"
#include "faculty175_face_alethiometer.h"
#include "faculty175_face_alethiometer_glyphs.h"
#include "faculty175_face_runes.h"
#include "faculty175_face_scale_earth_texture.h"
#include "faculty175_face_tarot.h"
#include "faculty175_face_tarot_assets.h"
#include "faculty175_face_tarot_spiffs_image.h"
#include "faculty175_faculty.h"
#include "faculty175_lenormand_glyphs.h"
#include "faculty175_pocketwatch.h"
#include "faculty175_quotes.h"
#include "faculty175_rocket.h"
#include "faculty175_touch.h"
#include "faculty175_usb.h"
#include "faculty175_wifi_settings.h"
#include "faculty175_wifi_lab.h"
#include "faculty175_wifi_monitor.h"
#include "faculty175_face_incidents.h"
#include "astrolabe_time.h"

#define FACULTY175_ENABLE_ALMANAC_FACES 0
#if FACULTY175_ENABLE_ALMANAC_FACES
#include "faculty175_almanac.h"
#endif

#define LVGL_DRAW_BUF_ROWS FACULTY175_LCD_H

static const char *TAG = "faculty175_lvgl";

static lv_display_t *s_display;
static lv_indev_t *s_touch_indev;
static uint8_t *s_draw_buf;
static uint8_t *s_draw_buf_2;
static bool s_ready;
static bool s_watch_created;
static uint32_t s_last_anim_ms;
static uint32_t s_last_service_ms;

static lv_obj_t *s_idle_screen;
static lv_obj_t *s_nav_screen;
static lv_obj_t *s_nav_center;
static lv_obj_t *s_nav_center_slug;
static lv_obj_t *s_nav_out_center;
static lv_obj_t *s_nav_out_slug;
static lv_obj_t *s_nav_left;
static lv_obj_t *s_nav_right;
static lv_obj_t *s_nav_up;
static lv_obj_t *s_nav_down;
static lv_obj_t *s_nav_ring;
static lv_obj_t *s_native_screen;
static lv_obj_t *s_native_title;
static lv_obj_t *s_native_subtitle;
static lv_obj_t *s_native_category;
static lv_obj_t *s_native_primary;
static lv_obj_t *s_native_line_a;
static lv_obj_t *s_native_line_b;
static lv_obj_t *s_native_line_c;
static lv_obj_t *s_native_arc;
static lv_obj_t *s_native_orbit[8];
static lv_obj_t *s_native_bars[7];
static lv_obj_t *s_native_cards[5];
static lv_obj_t *s_moon_screen;
static lv_obj_t *s_moon_image;
static lv_obj_t *s_moon_fallback_disc;
static lv_obj_t *s_moon_hour_line;
static lv_obj_t *s_moon_minute_line;
static lv_obj_t *s_moon_second_line;
static lv_obj_t *s_moon_hour_tail;
static lv_obj_t *s_moon_minute_tail;
static lv_obj_t *s_moon_hour_accent;
static lv_obj_t *s_moon_minute_accent;
static uint8_t *s_moon_pixels;
static uint8_t *s_moon_render_pixels;
static uint16_t *s_moon_bg565_pixels;
static uint16_t *s_moon_bg565_render_pixels;
static bool s_moon_bg565_loaded;
static bool s_moon_texture_loaded;
static bool s_moon_storage_checked;
static bool s_moon_storage_ready;
static int s_moon_render_key = INT_MIN;
static lv_point_precise_t s_moon_second_points[2];
static lv_point_precise_t s_moon_minute_points[2];
static lv_point_precise_t s_moon_hour_points[2];
static lv_point_precise_t s_moon_minute_tail_points[2];
static lv_point_precise_t s_moon_hour_tail_points[2];
static lv_point_precise_t s_moon_minute_accent_points[2];
static lv_point_precise_t s_moon_hour_accent_points[2];
static uint32_t s_moon_rendered_day_s = UINT32_MAX;
static lv_obj_t *s_tarot_screen;
static lv_obj_t *s_tarot_image;
static lv_obj_t *s_tarot_title;
static lv_obj_t *s_tarot_keyword;
static lv_obj_t *s_tarot_roman;
static lv_obj_t *s_tarot_fallback;
static lv_obj_t *s_tarot_arc;
static lv_obj_t *s_tarot_card_backs[5];
static int s_tarot_loaded_idx = -1;
static lv_obj_t *s_runes_screen;
static lv_obj_t *s_runes_ring;
static lv_obj_t *s_runes_tokens[3];
static lv_obj_t *s_runes_slot_labels[3];
static lv_obj_t *s_runes_name_labels[3];
static lv_obj_t *s_runes_keyword;
static lv_obj_t *s_runes_glyph_lines[3][6];
static lv_point_precise_t s_runes_glyph_points[3][6][2];
static int s_runes_loaded[3] = {-1, -1, -1};
static lv_obj_t *s_aleth_screen;
static lv_obj_t *s_aleth_outer;
static lv_obj_t *s_aleth_inner;
static lv_obj_t *s_aleth_center_rings[4];
static lv_obj_t *s_aleth_center_spokes[12];
static lv_point_precise_t s_aleth_spoke_points[12][2];
static lv_obj_t *s_aleth_needles[4];
static lv_point_precise_t s_aleth_needle_points[4][2];
static lv_obj_t *s_aleth_answer;
static lv_obj_t *s_aleth_question;
static lv_obj_t *s_aleth_glyph_images[36];
static uint8_t *s_aleth_glyph_pixels;
static lv_image_dsc_t s_aleth_glyph_textures[36];
static uint32_t s_aleth_glyph_colors[36];
static float s_aleth_angles[4] = {-1.5708f, -0.7f, 1.1f, 2.2f};
static float s_aleth_velocity[4];
static int s_aleth_last_targets[4] = {-1, -1, -1, -1};
static uint32_t s_aleth_last_anim_ms;
static bool s_aleth_angles_valid;
static lv_obj_t *s_sky_screen;
static lv_obj_t *s_sky_grid[5];
static lv_obj_t *s_sky_stars[24];
static lv_obj_t *s_sky_segments[16];
static lv_point_precise_t s_sky_segment_points[16][2];
static lv_obj_t *s_sky_label;
#if FACULTY175_ENABLE_ALMANAC_FACES
static lv_obj_t *s_almanac_screen;
static lv_obj_t *s_almanac_outer;
static lv_obj_t *s_almanac_moon;
static lv_obj_t *s_almanac_moon_shadow;
static lv_obj_t *s_almanac_date;
static lv_obj_t *s_almanac_season;
static lv_obj_t *s_almanac_sky;
static lv_obj_t *s_almanac_event;
static lv_obj_t *s_almanac_planting;
static lv_obj_t *s_almanac_status;
static lv_obj_t *s_phenology_screen;
static lv_obj_t *s_phenology_image;
static lv_obj_t *s_phenology_orbs[8];
static lv_obj_t *s_phenology_panel;
static lv_obj_t *s_phenology_subject;
static lv_obj_t *s_phenology_action;
static lv_obj_t *s_phenology_status;
static uint16_t *s_phenology_pixels;
static lv_image_dsc_t s_phenology_texture;
static char s_phenology_loaded_path[112];
#endif
static lv_obj_t *s_scale_screen;
static lv_obj_t *s_scale_outer;
static lv_obj_t *s_scale_inner;
static lv_obj_t *s_scale_earth;
static lv_obj_t *s_scale_moon;
static lv_obj_t *s_scale_sun;
static lv_obj_t *s_scale_gates[6];
static lv_obj_t *s_scale_gate_label;
static lv_obj_t *s_scale_value_label;
static lv_obj_t *s_scale_note_label;
static lv_obj_t *s_scale_time_label;
static lv_obj_t *s_solar_screen;
static lv_obj_t *s_solar_corona[4];
static lv_obj_t *s_solar_disk;
static lv_obj_t *s_solar_limb;
static lv_obj_t *s_solar_regions[7];
static lv_obj_t *s_solar_region_labels[4];
static lv_obj_t *s_solar_flare[3];
static lv_obj_t *s_solar_cme[3];
static lv_point_precise_t s_solar_cme_points[3][2];
static lv_obj_t *s_solar_title;
static lv_obj_t *s_solar_status;
static lv_obj_t *s_solar_source;
static lv_obj_t *s_magnet_screen;
static lv_obj_t *s_magnet_map_image;
static uint16_t *s_magnet_map_pixels;
static lv_image_dsc_t s_magnet_map_texture;
static bool s_magnet_map_loaded;
static lv_obj_t *s_magnet_sun;
static lv_obj_t *s_magnet_earth;
static lv_obj_t *s_magnet_earth_limb;
static lv_obj_t *s_magnet_earth_shadow;
static lv_obj_t *s_magnet_bow;
static lv_obj_t *s_magnet_tail[4];
static lv_point_precise_t s_magnet_tail_points[4][5];
static lv_obj_t *s_magnet_field[8];
static lv_point_precise_t s_magnet_field_points[8][11];
static lv_obj_t *s_magnet_surface[7];
static lv_obj_t *s_magnet_grid[6];
static lv_point_precise_t s_magnet_grid_points[6][9];
static lv_obj_t *s_magnet_particles[9];
static lv_obj_t *s_magnet_title;
static lv_obj_t *s_magnet_status;
static lv_obj_t *s_magnet_source;
static float s_magnet_data_pressure = 0.62f;
static float s_magnet_data_energy = 0.50f;
static lv_obj_t *s_astrology_screen;
static lv_obj_t *s_astrology_rings[4];
static lv_obj_t *s_astrology_spokes[12];
static lv_point_precise_t s_astrology_spoke_points[12][2];
static lv_obj_t *s_astrology_signs[12];
static lv_obj_t *s_astrology_glyph_lines[12][7];
static lv_point_precise_t s_astrology_glyph_points[12][7][2];
static lv_obj_t *s_astrology_bodies[7];
static lv_obj_t *s_astrology_body_labels[7];
static lv_obj_t *s_astrology_natal[7];
static lv_obj_t *s_astrology_title;
static lv_obj_t *s_astrology_line;
static lv_obj_t *s_astrology_source;
static lv_obj_t *s_synastry_screen;
static lv_obj_t *s_synastry_rings[5];
static lv_obj_t *s_synastry_spokes[12];
static lv_point_precise_t s_synastry_spoke_points[12][2];
static lv_obj_t *s_synastry_aspects[8];
static lv_point_precise_t s_synastry_aspect_points[8][2];
static lv_obj_t *s_synastry_user_bodies[7];
static lv_obj_t *s_synastry_target_bodies[7];
static lv_obj_t *s_synastry_user_labels[7];
static lv_obj_t *s_synastry_target_labels[7];
static lv_obj_t *s_synastry_title;
static lv_obj_t *s_synastry_names;
static lv_obj_t *s_synastry_line;
static lv_obj_t *s_transits_screen;
static lv_obj_t *s_transits_rings[4];
static lv_obj_t *s_transits_spokes[12];
static lv_point_precise_t s_transits_spoke_points[12][2];
static lv_obj_t *s_transits_motion[7];
static lv_point_precise_t s_transits_motion_points[7][2];
static lv_obj_t *s_transits_now[7];
static lv_obj_t *s_transits_next[7];
static lv_obj_t *s_transits_labels[7];
static lv_obj_t *s_transits_title;
static lv_obj_t *s_transits_line;
static lv_obj_t *s_transits_clock;
static lv_obj_t *s_transits_year_arcs[4];
static lv_obj_t *s_transits_year_hand;
static lv_obj_t *s_transits_year_today;
static lv_point_precise_t s_transits_year_hand_points[2];
static lv_obj_t *s_transits_month_labels[12];
static lv_obj_t *s_lenormand_screen;
static lv_obj_t *s_lenormand_ticks[36];
static lv_point_precise_t s_lenormand_tick_points[36][2];
static lv_obj_t *s_lenormand_number;
static lv_obj_t *s_lenormand_title;
static lv_obj_t *s_lenormand_keyword;
static lv_obj_t *s_lenormand_status;
static lv_obj_t *s_lenormand_glyph_image;
static uint8_t *s_lenormand_glyph_pixels;
static uint8_t *s_lenormand_glyph_bits;
static const uint8_t *s_lenormand_glyph_current_bits;
static lv_image_dsc_t s_lenormand_glyph_texture;
static int s_lenormand_rendered_glyph = -1;
static uint32_t s_lenormand_rendered_color = UINT32_MAX;
static FILE *s_lenormand_glyph_file;
static bool s_lenormand_glyph_checked;
static bool s_lenormand_glyph_ready;
static lv_obj_t *s_piano_screen;
static lv_obj_t *s_piano_white[7];
static lv_obj_t *s_piano_black[5];
static lv_obj_t *s_piano_white_labels[7];
static lv_obj_t *s_piano_black_labels[5];
static lv_obj_t *s_piano_pulse;
static lv_obj_t *s_piano_title;
static lv_obj_t *s_piano_status;
static lv_obj_t *s_instrument_screen;
static lv_obj_t *s_instrument_title;
static lv_obj_t *s_instrument_status;
static lv_obj_t *s_instrument_orbs[9];
static lv_obj_t *s_instrument_bars[14];
static lv_obj_t *s_instrument_lines[12];
static lv_point_precise_t s_instrument_line_points[12][2];
static lv_obj_t *s_instrument_labels[4];
static lv_obj_t *s_oracle_screen;
static lv_obj_t *s_oracle_title;
static lv_obj_t *s_oracle_status;
static lv_obj_t *s_oracle_orbs[16];
static lv_obj_t *s_oracle_bars[16];
static lv_obj_t *s_oracle_lines[24];
static lv_point_precise_t s_oracle_line_points[24][2];
static lv_obj_t *s_oracle_labels[12];
static lv_obj_t *s_utility_screen;
static lv_obj_t *s_utility_title;
static lv_obj_t *s_utility_status;
static lv_obj_t *s_utility_orbs[14];
static lv_obj_t *s_utility_bars[16];
static lv_obj_t *s_utility_lines[28];
static lv_point_precise_t s_utility_line_points[28][2];
static lv_obj_t *s_utility_labels[14];
static lv_obj_t *s_utility_qr;
static lv_obj_t *s_rocket_image;
static uint16_t *s_rocket_image_pixels;
static lv_image_dsc_t s_rocket_image_texture;
static bool s_rocket_image_checked;
static lv_obj_t *s_faculty_face_image;
static lv_obj_t *s_deathstar_image;
static uint16_t *s_deathstar_pixels;
static lv_image_dsc_t s_deathstar_textures[2];
static FILE *s_deathstar_file;
static uint32_t *s_deathstar_offsets;
static uint8_t *s_deathstar_comp;
static size_t s_deathstar_comp_cap;
static size_t s_deathstar_comp_len;
static uint32_t s_deathstar_file_size;
static uint32_t s_deathstar_frame_count;
static uint16_t s_deathstar_fps;
static uint32_t s_deathstar_rendered_frame = UINT32_MAX;
static uint8_t s_deathstar_texture_slot;
static bool s_deathstar_checked;
static bool s_deathstar_ready;
static uint8_t *s_faculty_face_pixels;
static lv_image_dsc_t s_faculty_face_texture;
static char s_faculty_face_loaded_slug[64];
static lv_obj_t *s_watch_screen;
static lv_obj_t *s_watch_tint;
static lv_obj_t *s_watch_second_line;
static lv_obj_t *s_watch_minute_line;
static lv_obj_t *s_watch_hour_line;
static lv_obj_t *s_watch_minute_tail;
static lv_obj_t *s_watch_hour_tail;
static lv_obj_t *s_watch_minute_accent;
static lv_obj_t *s_watch_hour_accent;
static lv_point_precise_t s_second_points[2];
static lv_point_precise_t s_minute_points[2];
static lv_point_precise_t s_hour_points[2];
static lv_point_precise_t s_minute_tail_points[2];
static lv_point_precise_t s_hour_tail_points[2];
static lv_point_precise_t s_minute_accent_points[2];
static lv_point_precise_t s_hour_accent_points[2];
static bool s_watch_time_base_valid;
static bool s_watch_time_base_wall_valid;
static uint32_t s_watch_time_base_day_s;
static int64_t s_watch_time_base_ms;
static uint32_t s_watch_rendered_day_s = UINT32_MAX;
static uint32_t s_watch_rendered_minute = UINT32_MAX;
static uint32_t s_watch_tick_metrics_count;
static uint32_t s_watch_rendered_tint_color = UINT32_MAX;
static lv_opa_t s_watch_rendered_tint_opa = 0xff;

static void lvgl_tick(uint32_t elapsed_ms);
static uint32_t watch_seconds_of_day(uint32_t anim_ms, bool *time_valid);
static bool moon_storage_ready(void);
static uint8_t descriptor_hue_for_face(faculty175_face_id_t id);
static void native_obj_hidden(lv_obj_t *obj, bool hidden);
static void nav_anim_metric_start(int64_t *last_us, int64_t *start_us, uint32_t *frames, uint32_t *max_gap_ms);
static void nav_anim_metric_frame(int64_t *last_us, uint32_t *frames, uint32_t *max_gap_ms);
static void nav_anim_metric_log(const char *kind,
                                const char *axis,
                                int delta,
                                uint32_t expected_ms,
                                int64_t start_us,
                                uint32_t frames,
                                uint32_t max_gap_ms);

static lv_image_dsc_t s_watch_bg;
static bool s_watch_bg_ready;

static lv_image_dsc_t s_moon_texture;
static lv_image_dsc_t s_tarot_texture;
static const lv_image_dsc_t s_scale_earth_texture = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = FACULTY175_EARTH_TEX_W,
        .h = FACULTY175_EARTH_TEX_H,
        .stride = FACULTY175_EARTH_TEX_W * sizeof(uint16_t),
        .reserved_2 = 0,
    },
    .data_size = FACULTY175_EARTH_TEX_W * FACULTY175_EARTH_TEX_H * sizeof(uint16_t),
    .data = (const uint8_t *)k_faculty175_earth_tex_rgb565,
    .reserved = NULL,
    .reserved_2 = NULL,
};

static const char *category_name(uint32_t categories)
{
    if ((categories & FACULTY175_FACE_CAT_HOME) != 0) {
        return "HOME";
    }
    if ((categories & FACULTY175_FACE_CAT_COMMONPLACE) != 0) {
        return "COMMON";
    }
    if ((categories & FACULTY175_FACE_CAT_ORACLE) != 0) {
        return "ORACLE";
    }
    if ((categories & FACULTY175_FACE_CAT_INSTRUMENT) != 0) {
        return "INSTRUMENT";
    }
    if ((categories & FACULTY175_FACE_CAT_SYSTEM) != 0) {
        return "SYSTEM";
    }
    return "FACE";
}

static const char *face_label(const faculty175_face_desc_t *face)
{
    return face != NULL && face->label != NULL ? face->label : "-";
}

static const char *face_slug(const faculty175_face_desc_t *face)
{
    return face != NULL && face->slug != NULL ? face->slug : "-";
}

static bool ensure_watch_bg(void)
{
    if (s_watch_bg_ready) {
        return s_watch_bg.data != NULL;
    }
    s_watch_bg_ready = true;
    const uint16_t *pixels = faculty175_pocketwatch_background_pixels();
    if (pixels == NULL) {
        return false;
    }
    s_watch_bg.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_watch_bg.header.cf = LV_COLOR_FORMAT_RGB565;
    s_watch_bg.header.flags = 0;
    s_watch_bg.header.w = FACULTY175_LCD_W;
    s_watch_bg.header.h = FACULTY175_LCD_H;
    s_watch_bg.header.stride = FACULTY175_LCD_W * sizeof(uint16_t);
    s_watch_bg.header.reserved_2 = 0;
    s_watch_bg.data_size = FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t);
    s_watch_bg.data = (const uint8_t *)pixels;
    s_watch_bg.reserved = NULL;
    s_watch_bg.reserved_2 = NULL;
    return true;
}

static void display_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t w = lv_area_get_width(area);
    const int32_t h = lv_area_get_height(area);
    const uint16_t *src = (const uint16_t *)px_map;

    faculty175_display_draw_rgb565(src, area->x1, area->y1, w, h);
    faculty175_display_flush_rect(area->x1, area->y1, w, h);
    lv_display_flush_ready(display);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    const faculty175_touch_state_t state = faculty175_touch_state_get();
    data->point.x = state.x;
    data->point.y = state.y;
    data->state = state.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static lv_obj_t *make_nav_label(lv_obj_t *parent,
                                const char *text,
                                int32_t w,
                                int32_t h,
                                int32_t x,
                                int32_t y,
                                uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_size(label, w, h);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, x, y);
    return label;
}

static void nav_label_set_if_changed(lv_obj_t *label, const char *text)
{
    if (label == NULL) {
        return;
    }
    const char *current = lv_label_get_text(label);
    const char *next = text != NULL ? text : "";
    if (current == NULL || strcmp(current, next) != 0) {
        lv_label_set_text(label, next);
    }
}

static void nav_update_center_labels(const faculty175_face_desc_t *center)
{
    char current_slug[48];
    snprintf(current_slug, sizeof(current_slug), "%s / %s", category_name(center != NULL ? center->categories : 0),
             face_slug(center));
    nav_label_set_if_changed(s_nav_center, face_label(center));
    nav_label_set_if_changed(s_nav_center_slug, current_slug);
}

static void nav_align_pair(lv_obj_t *center, lv_obj_t *slug, int32_t major_offset, bool vertical)
{
    if (vertical) {
        lv_obj_align(center, LV_ALIGN_CENTER, 0, -8 + major_offset);
        lv_obj_align(slug, LV_ALIGN_CENTER, 0, 34 + major_offset);
    } else {
        lv_obj_align(center, LV_ALIGN_CENTER, major_offset, -8);
        lv_obj_align(slug, LV_ALIGN_CENTER, major_offset, 34);
    }
}

static void nav_align_center(int32_t major_offset, bool vertical)
{
    nav_align_pair(s_nav_center, s_nav_center_slug, major_offset, vertical);
}

static void create_nav_screen(void)
{
    s_nav_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_nav_screen);
    lv_obj_set_size(s_nav_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_nav_screen, lv_color_hex(0x030609), 0);
    lv_obj_set_style_bg_opa(s_nav_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_nav_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_nav_ring = lv_arc_create(s_nav_screen);
    lv_obj_remove_style(s_nav_ring, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_nav_ring, 398, 398);
    lv_obj_center(s_nav_ring);
    lv_arc_set_range(s_nav_ring, 0, 100);
    lv_arc_set_value(s_nav_ring, 100);
    lv_arc_set_bg_angles(s_nav_ring, 0, 360);
    lv_arc_set_rotation(s_nav_ring, 270);
    lv_obj_clear_flag(s_nav_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_nav_ring, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_nav_ring, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_nav_ring, lv_color_hex(0x24404a), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_nav_ring, 86, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_nav_ring, 0, LV_PART_INDICATOR);

    s_nav_center = make_nav_label(s_nav_screen, "-", 318, 38, 0, -8, 0xfff2c4);
    s_nav_center_slug = make_nav_label(s_nav_screen, "-", 246, 22, 0, 34, 0x80d8e8);
    s_nav_out_center = make_nav_label(s_nav_screen, "-", 318, 38, 0, -8, 0xfff2c4);
    s_nav_out_slug = make_nav_label(s_nav_screen, "-", 246, 22, 0, 34, 0x80d8e8);
    native_obj_hidden(s_nav_out_center, true);
    native_obj_hidden(s_nav_out_slug, true);
    s_nav_left = make_nav_label(s_nav_screen, "<", 24, 24, -194, 0, 0x7fcde0);
    s_nav_right = make_nav_label(s_nav_screen, ">", 24, 24, 194, 0, 0x7fcde0);
    s_nav_up = make_nav_label(s_nav_screen, "^", 24, 24, 0, -194, 0xffcf66);
    s_nav_down = make_nav_label(s_nav_screen, "v", 24, 24, 0, 194, 0xffcf66);

    lv_obj_set_style_text_opa(s_nav_left, 106, 0);
    lv_obj_set_style_text_opa(s_nav_right, 106, 0);
    lv_obj_set_style_text_opa(s_nav_up, 90, 0);
    lv_obj_set_style_text_opa(s_nav_down, 90, 0);
}

static void invalidate_watch_line_area(lv_obj_t *line,
                                       const lv_point_precise_t *points,
                                       int32_t width,
                                       int32_t pad)
{
    if (line == NULL || points == NULL) {
        return;
    }
    int32_t x0 = (int32_t)lrintf((float)points[0].x);
    int32_t y0 = (int32_t)lrintf((float)points[0].y);
    int32_t x1 = (int32_t)lrintf((float)points[1].x);
    int32_t y1 = (int32_t)lrintf((float)points[1].y);
    const int32_t grow = (width + 1) / 2 + pad;
    lv_area_t area = {
        .x1 = LV_MIN(x0, x1) - grow,
        .y1 = LV_MIN(y0, y1) - grow,
        .x2 = LV_MAX(x0, x1) + grow,
        .y2 = LV_MAX(y0, y1) + grow,
    };
    if (area.x1 < 0) {
        area.x1 = 0;
    }
    if (area.y1 < 0) {
        area.y1 = 0;
    }
    if (area.x2 >= FACULTY175_LCD_W) {
        area.x2 = FACULTY175_LCD_W - 1;
    }
    if (area.y2 >= FACULTY175_LCD_H) {
        area.y2 = FACULTY175_LCD_H - 1;
    }
    lv_obj_invalidate_area(line, &area);
}

static void point_line_at_unit(lv_point_precise_t *points, float unit, int32_t length)
{
    const float a = unit * 6.28318530718f - 1.57079632679f;
    points[0].x = FACULTY175_LCD_W / 2;
    points[0].y = FACULTY175_LCD_H / 2;
    points[1].x = (lv_value_precise_t)((FACULTY175_LCD_W / 2) + cosf(a) * (float)length);
    points[1].y = (lv_value_precise_t)((FACULTY175_LCD_H / 2) + sinf(a) * (float)length);
}

static void set_line_points(lv_obj_t *line, lv_point_precise_t *points, float unit, int32_t length)
{
    point_line_at_unit(points, unit, length);
    lv_line_set_points_mutable(line, points, 2);
}

static void set_second_line_points_radial(lv_obj_t *line, lv_point_precise_t *points, float unit, int32_t length)
{
    if (line == NULL || points == NULL) {
        return;
    }
    invalidate_watch_line_area(line, points, 2, 8);
    point_line_at_unit(points, unit, length);
    invalidate_watch_line_area(line, points, 2, 8);
}

static void set_hand_segment(lv_obj_t *line,
                             lv_point_precise_t *points,
                             float unit,
                             int32_t start_length,
                             int32_t end_length)
{
    lv_obj_invalidate(line);
    const float a = unit * 6.28318530718f - 1.57079632679f;
    points[0].x = (lv_value_precise_t)((FACULTY175_LCD_W / 2) + cosf(a) * (float)start_length);
    points[0].y = (lv_value_precise_t)((FACULTY175_LCD_H / 2) + sinf(a) * (float)start_length);
    points[1].x = (lv_value_precise_t)((FACULTY175_LCD_W / 2) + cosf(a) * (float)end_length);
    points[1].y = (lv_value_precise_t)((FACULTY175_LCD_H / 2) + sinf(a) * (float)end_length);
    lv_line_set_points_mutable(line, points, 2);
    lv_obj_invalidate(line);
}

static void configure_watch_line(lv_obj_t *line, int32_t width, uint32_t color)
{
    lv_obj_set_size(line, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
}

static void nav_anim_metric_start(int64_t *last_us, int64_t *start_us, uint32_t *frames, uint32_t *max_gap_ms)
{
    const int64_t now_us = esp_timer_get_time();
    if (start_us != NULL) {
        *start_us = now_us;
    }
    if (last_us != NULL) {
        *last_us = now_us;
    }
    if (frames != NULL) {
        *frames = 0;
    }
    if (max_gap_ms != NULL) {
        *max_gap_ms = 0;
    }
}

static void nav_anim_metric_frame(int64_t *last_us, uint32_t *frames, uint32_t *max_gap_ms)
{
    const int64_t now_us = esp_timer_get_time();
    if (last_us != NULL && *last_us != 0 && max_gap_ms != NULL) {
        const uint32_t gap_ms = (uint32_t)((now_us - *last_us + 999) / 1000);
        if (gap_ms > *max_gap_ms) {
            *max_gap_ms = gap_ms;
        }
    }
    if (last_us != NULL) {
        *last_us = now_us;
    }
    if (frames != NULL) {
        ++(*frames);
    }
}

static void nav_anim_metric_log(const char *kind,
                                const char *axis,
                                int delta,
                                uint32_t expected_ms,
                                int64_t start_us,
                                uint32_t frames,
                                uint32_t max_gap_ms)
{
    const uint32_t actual_ms = (uint32_t)((esp_timer_get_time() - start_us + 999) / 1000);
    const uint32_t avg_gap_ms = frames > 1 ? actual_ms / (frames - 1u) : actual_ms;
    const int32_t overrun_ms = (int32_t)actual_ms - (int32_t)expected_ms;
    ESP_LOGI(TAG,
             "nav-anim-metrics kind=%s axis=%s delta=%d expected_ms=%u actual_ms=%u frames=%u avg_gap_ms=%u max_gap_ms=%u overrun_ms=%d",
             kind != NULL ? kind : "-",
             axis != NULL ? axis : "-",
             delta,
             (unsigned)expected_ms,
             (unsigned)actual_ms,
             (unsigned)frames,
             (unsigned)avg_gap_ms,
             (unsigned)max_gap_ms,
             (int)overrun_ms);
}

static uint32_t native_hue_color(uint8_t hue, uint8_t lift)
{
    static const uint32_t palette[] = {
        0xffc66d,
        0xf0dd6a,
        0x6ee0aa,
        0x7fcde0,
        0xa58cff,
        0xf090c8,
    };
    uint32_t c = palette[hue % (sizeof(palette) / sizeof(palette[0]))];
    if (lift == 0) {
        return c;
    }
    uint8_t r = (uint8_t)((c >> 16) & 0xff);
    uint8_t g = (uint8_t)((c >> 8) & 0xff);
    uint8_t b = (uint8_t)(c & 0xff);
    r = (uint8_t)(r + (((uint16_t)(255u - r) * lift) / 255u));
    g = (uint8_t)(g + (((uint16_t)(255u - g) * lift) / 255u));
    b = (uint8_t)(b + (((uint16_t)(255u - b) * lift) / 255u));
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static lv_obj_t *make_native_label(lv_obj_t *parent, int32_t y, int32_t w, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static void native_obj_hidden(lv_obj_t *obj, bool hidden)
{
    if (obj == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_circle(lv_obj_t *parent, int32_t size, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (obj == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *make_arc_ring(lv_obj_t *parent, int32_t size, uint32_t color, int32_t width, lv_opa_t opa)
{
    lv_obj_t *arc = lv_arc_create(parent);
    if (arc == NULL) {
        return NULL;
    }
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 100);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 270);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, opa, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    return arc;
}

/* A value arc used for the annual transit lanes.  The background is kept
 * transparent so each lane reads as a discrete timing window, rather than
 * another complete bezel ring. */
static lv_obj_t *make_transit_year_arc(lv_obj_t *parent, int32_t size, uint32_t color, int32_t width)
{
    lv_obj_t *arc = lv_arc_create(parent);
    if (arc == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, size, size);
    lv_arc_set_range(arc, 0, 360);
    lv_arc_set_value(arc, 0);
    lv_arc_set_rotation(arc, 270);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_width(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, 0, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_center(arc);
    return arc;
}

static double rev360(double x)
{
    x = fmod(x, 360.0);
    if (x < 0.0) {
        x += 360.0;
    }
    return x;
}

static double deg_to_rad(double deg)
{
    return deg * (M_PI / 180.0);
}

static double rad_to_deg(double rad)
{
    return rad * (180.0 / M_PI);
}

static void ecliptic_to_equatorial(double lon_deg, double lat_deg, double *ra_deg, double *dec_deg)
{
    const double eps = deg_to_rad(23.4393);
    const double lon = deg_to_rad(lon_deg);
    const double lat = deg_to_rad(lat_deg);
    const double sin_dec = sin(lat) * cos(eps) + cos(lat) * sin(eps) * sin(lon);
    const double y = sin(lon) * cos(eps) - tan(lat) * sin(eps);
    const double x = cos(lon);
    if (ra_deg != NULL) {
        *ra_deg = rev360(rad_to_deg(atan2(y, x)));
    }
    if (dec_deg != NULL) {
        *dec_deg = rad_to_deg(asin(sin_dec));
    }
}

static double local_sidereal_deg(double jd, double lon_deg)
{
    const double t = (jd - 2451545.0) / 36525.0;
    return rev360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * t * t -
                  t * t * t / 38710000.0 + lon_deg);
}

static void sun_rect_and_mean(double d, double *lon_deg, double *ls_deg, double *ms_deg)
{
    const double w = 282.9404 + 4.70935e-5 * d;
    const double e = 0.016709 - 1.151e-9 * d;
    double m = rev360(356.0470 + 0.9856002585 * d);
    const double ls = rev360(w + m);
    const double mr = deg_to_rad(m);
    double e_anom = rev360(m + rad_to_deg(e * sin(mr) * (1.0 + e * cos(mr))));
    const double er = deg_to_rad(e_anom);
    const double xv = cos(er) - e;
    const double yv = sin(er) * sqrt(1.0 - e * e);
    const double v = rad_to_deg(atan2(yv, xv));
    if (lon_deg != NULL) {
        *lon_deg = rev360(v + w);
    }
    if (ls_deg != NULL) {
        *ls_deg = ls;
    }
    if (ms_deg != NULL) {
        *ms_deg = m;
    }
}

static void moon_lon_lat(double d, double ls_deg, double ms_deg, double *lon_deg, double *lat_deg)
{
    double n = rev360(125.1228 - 0.0529538083 * d);
    const double i = 5.1454;
    double w_m = rev360(318.0634 + 0.1643573223 * d);
    const double a = 60.2666;
    const double e = 0.054900;
    double m = rev360(115.3654 + 13.0649929509 * d);
    double e_anom = m + rad_to_deg(e * sin(deg_to_rad(m)) * (1.0 + e * cos(deg_to_rad(m))));
    for (int iter = 0; iter < 6; ++iter) {
        const double er = deg_to_rad(e_anom);
        const double delta = (e_anom - rad_to_deg(e * sin(er)) - m) / (1.0 - e * cos(er));
        e_anom -= delta;
        if (fabs(delta) < 1e-6) {
            break;
        }
    }
    const double er = deg_to_rad(rev360(e_anom));
    const double xv = a * (cos(er) - e);
    const double yv = a * sqrt(1.0 - e * e) * sin(er);
    const double v = rad_to_deg(atan2(yv, xv));
    const double r = sqrt(xv * xv + yv * yv);
    const double nr = deg_to_rad(n);
    const double ir = deg_to_rad(i);
    const double vw = deg_to_rad(v + w_m);
    const double xe = r * (cos(nr) * cos(vw) - sin(nr) * sin(vw) * cos(ir));
    const double ye = r * (sin(nr) * cos(vw) + cos(nr) * sin(vw) * cos(ir));
    const double ze = r * sin(vw) * sin(ir);
    double lon = rev360(rad_to_deg(atan2(ye, xe)));
    const double lm = rev360(n + w_m + m);
    const double d_moon = rev360(lm - rev360(ls_deg));
    const double f = rev360(lm - n);
    lon = rev360(lon - 1.274 * sin(deg_to_rad(m - 2.0 * d_moon)) +
                 0.658 * sin(deg_to_rad(2.0 * d_moon)) - 0.186 * sin(deg_to_rad(ms_deg)));
    if (lon_deg != NULL) {
        *lon_deg = lon;
    }
    if (lat_deg != NULL) {
        *lat_deg = rad_to_deg(atan2(ze, sqrt(xe * xe + ye * ye))) +
                   0.173 * sin(deg_to_rad(f - 2.0 * d_moon));
    }
}

static float moon_phase_fraction(uint32_t anim_ms)
{
    time_t epoch = astrolabe_time_valid() ? astrolabe_time_now() : (time_t)(947182440 + (anim_ms % 2551443u));
    const double jd = ((double)epoch / 86400.0) + 2440587.5;
    const double d = jd - 2451543.5;
    double sun_lon = 0.0;
    double ls = 0.0;
    double ms = 0.0;
    sun_rect_and_mean(d, &sun_lon, &ls, &ms);
    double moon_lon = 0.0;
    double moon_lat = 0.0;
    moon_lon_lat(d, ls, ms, &moon_lon, &moon_lat);

    faculty175_location_settings_t loc = {};
    if (faculty175_location_settings_load(&loc) == ESP_OK && loc.valid) {
        double moon_ra = 0.0;
        double moon_dec = 0.0;
        ecliptic_to_equatorial(moon_lon, moon_lat, &moon_ra, &moon_dec);
        const double lst = local_sidereal_deg(jd, loc.lon_deg);
        const double ha = deg_to_rad(rev360(lst - moon_ra));
        const double lat = deg_to_rad(loc.lat_deg);
        const double dec = deg_to_rad(moon_dec);
        const double altitude = asin(sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(ha));
        const double parallax_deg = 0.9507 * cos(altitude);
        moon_lon = rev360(moon_lon - parallax_deg * sin(ha) * cos(lat));
    }

    return (float)(rev360(moon_lon - sun_lon) / 360.0);
}

static uint16_t rgb565_dim(uint16_t px, uint8_t dim)
{
    const uint16_t r = (uint16_t)((px >> 11) & 0x1f);
    const uint16_t g = (uint16_t)((px >> 5) & 0x3f);
    const uint16_t b = (uint16_t)(px & 0x1f);
    return (uint16_t)((((r * dim) / 255u) << 11) | (((g * dim) / 255u) << 5) | ((b * dim) / 255u));
}

static inline uint8_t dim_u8(uint8_t v, uint8_t dim)
{
    return (uint8_t)(((uint16_t)v * dim) / 255u);
}

static void moon_render_shadow(float phase)
{
    if (s_moon_pixels == NULL || s_moon_render_pixels == NULL) {
        return;
    }
    const int render_key = (int)lrintf(phase * 720.0f);
    if (render_key == s_moon_render_key) {
        return;
    }
    s_moon_render_key = render_key;

    const float cx = ((float)FACULTY175_LCD_W - 1.0f) * 0.5f;
    const float cy = ((float)FACULTY175_LCD_H - 1.0f) * 0.5f;
    const float radius = 226.0f;
    const float inv_r = 1.0f / radius;
    const float terminator = cosf(phase * 6.28318530718f);
    const bool waxing = phase < 0.5f;
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const float ny = ((float)y - cy) * inv_r;
        const float yy = ny * ny;
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const size_t idx = ((size_t)y * FACULTY175_LCD_W + (size_t)x) * 4u;
            const float nx = ((float)x - cx) * inv_r;
            const float rr = nx * nx + yy;
            if (rr > 1.03f) {
                s_moon_render_pixels[idx + 0u] = s_moon_pixels[idx + 0u];
                s_moon_render_pixels[idx + 1u] = s_moon_pixels[idx + 1u];
                s_moon_render_pixels[idx + 2u] = s_moon_pixels[idx + 2u];
                s_moon_render_pixels[idx + 3u] = s_moon_pixels[idx + 3u];
                continue;
            }
            const float limb = sqrtf(fmaxf(0.0f, 1.0f - yy));
            const float edge = terminator * limb;
            const float lit_side = waxing ? (nx - edge) : (-nx - edge);
            float shadow = 1.0f - (lit_side + 0.035f) / 0.12f;
            if (shadow < 0.0f) {
                shadow = 0.0f;
            } else if (shadow > 1.0f) {
                shadow = 1.0f;
            }
            const float radial = fminf(1.0f, sqrtf(fmaxf(0.0f, rr)));
            const float limb_shadow = radial > 0.86f ? (radial - 0.86f) / 0.14f : 0.0f;
            const uint8_t dim = (uint8_t)lrintf(255.0f - 168.0f * shadow - 34.0f * limb_shadow);
            const uint8_t final_dim = dim < 36 ? 36 : dim;
            s_moon_render_pixels[idx + 0u] = dim_u8(s_moon_pixels[idx + 0u], final_dim);
            s_moon_render_pixels[idx + 1u] = dim_u8(s_moon_pixels[idx + 1u], final_dim);
            s_moon_render_pixels[idx + 2u] = dim_u8(s_moon_pixels[idx + 2u], final_dim);
            s_moon_render_pixels[idx + 3u] = s_moon_pixels[idx + 3u];
        }
    }
    s_moon_texture.data = (const uint8_t *)s_moon_render_pixels;
    if (s_moon_image != NULL) {
        lv_image_set_src(s_moon_image, &s_moon_texture);
        lv_obj_invalidate(s_moon_image);
    }
}

static bool moon_background_load565(void)
{
    if (s_moon_bg565_loaded) {
        return s_moon_bg565_pixels != NULL;
    }
    s_moon_bg565_loaded = true;

    if (!moon_storage_ready()) {
        return false;
    }

    char path[160];
    if (!faculty175_usb_resolve_asset_path("bust_cache/moon/moon_360_gray.png",
                                           "/bust_cache/moon/moon_466_gray.png",
                                           path,
                                           sizeof(path))) {
        ESP_LOGW(TAG, "moon texture missing");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "moon texture missing: %s", path);
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return false;
    }
    const long size = ftell(f);
    if (size <= 0 || size > INT_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return false;
    }
    uint8_t *png = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png == NULL) {
        png = heap_caps_malloc((size_t)size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (png == NULL) {
        fclose(f);
        return false;
    }
    const size_t rd = fread(png, 1, (size_t)size, f);
    fclose(f);
    if (rd != (size_t)size) {
        heap_caps_free(png);
        return false;
    }

    int decoded_w = 0;
    int decoded_h = 0;
    int channels = 0;
    uint8_t *decoded = stbi_load_from_memory(png, (int)size, &decoded_w, &decoded_h, &channels, 3);
    heap_caps_free(png);
    if (decoded == NULL || decoded_w <= 0 || decoded_h <= 0) {
        if (decoded != NULL) {
            stbi_image_free(decoded);
        }
        ESP_LOGW(TAG, "moon PNG decode failed len=%u err=%s", (unsigned)size, stbi_failure_reason());
        return false;
    }

    const size_t pixel_count = (size_t)FACULTY175_LCD_W * FACULTY175_LCD_H;
    s_moon_bg565_pixels = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_moon_bg565_pixels == NULL) {
        s_moon_bg565_pixels = heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_moon_bg565_render_pixels =
        heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_moon_bg565_render_pixels == NULL) {
        s_moon_bg565_render_pixels =
            heap_caps_malloc(pixel_count * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_moon_bg565_pixels == NULL || s_moon_bg565_render_pixels == NULL) {
        heap_caps_free(s_moon_bg565_pixels);
        heap_caps_free(s_moon_bg565_render_pixels);
        s_moon_bg565_pixels = NULL;
        s_moon_bg565_render_pixels = NULL;
        stbi_image_free(decoded);
        return false;
    }

    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const int src_y = (int)((int64_t)y * decoded_h / FACULTY175_LCD_H);
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const int src_x = (int)((int64_t)x * decoded_w / FACULTY175_LCD_W);
            const uint8_t *px = decoded + ((size_t)src_y * (size_t)decoded_w + (size_t)src_x) * 3u;
            s_moon_bg565_pixels[(size_t)y * FACULTY175_LCD_W + (size_t)x] =
                faculty175_display_rgb888(px[0], px[1], px[2]);
        }
    }
    stbi_image_free(decoded);
    memcpy(s_moon_bg565_render_pixels, s_moon_bg565_pixels, pixel_count * sizeof(uint16_t));
    ESP_LOGI(TAG, "moon direct texture loaded %dx%d -> %dx%d", decoded_w, decoded_h, FACULTY175_LCD_W, FACULTY175_LCD_H);
    return true;
}

static void moon_render_shadow565(float phase)
{
    if (s_moon_bg565_pixels == NULL || s_moon_bg565_render_pixels == NULL) {
        return;
    }
    const int render_key = (int)lrintf(phase * 720.0f);
    if (render_key == s_moon_render_key) {
        return;
    }
    s_moon_render_key = render_key;

    const float cx = ((float)FACULTY175_LCD_W - 1.0f) * 0.5f;
    const float cy = ((float)FACULTY175_LCD_H - 1.0f) * 0.5f;
    const float radius = 174.0f;
    const float inv_r = 1.0f / radius;
    const float terminator = cosf(phase * 6.28318530718f);
    const bool waxing = phase < 0.5f;
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const float ny = ((float)y - cy) * inv_r;
        const float yy = ny * ny;
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const size_t idx = (size_t)y * FACULTY175_LCD_W + (size_t)x;
            const float nx = ((float)x - cx) * inv_r;
            const float rr = nx * nx + yy;
            if (rr > 1.03f) {
                s_moon_bg565_render_pixels[idx] = s_moon_bg565_pixels[idx];
                continue;
            }
            const float limb = sqrtf(fmaxf(0.0f, 1.0f - yy));
            const float edge = terminator * limb;
            const float lit_side = waxing ? (nx - edge) : (-nx - edge);
            float shadow = 1.0f - (lit_side + 0.035f) / 0.12f;
            if (shadow < 0.0f) {
                shadow = 0.0f;
            } else if (shadow > 1.0f) {
                shadow = 1.0f;
            }
            const float radial = fminf(1.0f, sqrtf(fmaxf(0.0f, rr)));
            const float limb_shadow = radial > 0.86f ? (radial - 0.86f) / 0.14f : 0.0f;
            const uint8_t dim = (uint8_t)lrintf(255.0f - 168.0f * shadow - 34.0f * limb_shadow);
            s_moon_bg565_render_pixels[idx] = rgb565_dim(s_moon_bg565_pixels[idx], dim < 36 ? 36 : dim);
        }
    }
}

static void draw_direct_hand(float unit, int32_t start, int32_t end, uint16_t color, int width)
{
    const float a = unit * 6.28318530718f - 1.57079632679f;
    const float ca = cosf(a);
    const float sa = sinf(a);
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const int x0 = (int)lrintf((float)cx + ca * (float)start);
    const int y0 = (int)lrintf((float)cy + sa * (float)start);
    const int x1 = (int)lrintf((float)cx + ca * (float)end);
    const int y1 = (int)lrintf((float)cy + sa * (float)end);
    faculty175_display_draw_line(x0, y0, x1, y1, color);
    if (width > 1) {
        const float px = -sa;
        const float py = ca;
        for (int off = 1; off <= width / 2; ++off) {
            const int dx = (int)lrintf(px * (float)off);
            const int dy = (int)lrintf(py * (float)off);
            faculty175_display_draw_line(x0 + dx, y0 + dy, x1 + dx, y1 + dy, color);
            faculty175_display_draw_line(x0 - dx, y0 - dy, x1 - dx, y1 - dy, color);
        }
    }
}

static bool moon_storage_ready(void)
{
    if (s_moon_storage_checked) {
        return s_moon_storage_ready;
    }
    s_moon_storage_checked = true;

    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/bust_cache",
        .partition_label = "storage",
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_moon_storage_ready = true;
        return true;
    }
    ESP_LOGW(TAG, "moon SPIFFS mount failed: %s", esp_err_to_name(err));
    return false;
}

static bool moon_texture_load(void)
{
    if (s_moon_texture_loaded) {
        return s_moon_pixels != NULL;
    }
    s_moon_texture_loaded = true;

    if (!moon_storage_ready()) {
        return false;
    }

    char path[160];
    if (!faculty175_usb_resolve_asset_path("bust_cache/moon/moon_360_gray.png",
                                           "/bust_cache/moon/moon_466_gray.png",
                                           path,
                                           sizeof(path))) {
        ESP_LOGW(TAG, "moon texture missing");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "moon texture missing: %s", path);
        return false;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ESP_LOGW(TAG, "moon PNG seek failed: %s", path);
        return false;
    }
    const long size = ftell(f);
    if (size <= 0 || size > INT_MAX || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        ESP_LOGW(TAG, "moon PNG size invalid: %ld", size);
        return false;
    }
    const size_t expected = (size_t)FACULTY175_LCD_W * FACULTY175_LCD_H * 4u;

    uint8_t *png = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (png == NULL) {
        png = heap_caps_malloc((size_t)size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (png == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "moon PNG alloc failed");
        return false;
    }
    const size_t rd = fread(png, 1, (size_t)size, f);
    fclose(f);
    if (rd != (size_t)size) {
        heap_caps_free(png);
        ESP_LOGW(TAG, "moon PNG short read %u/%u", (unsigned)rd, (unsigned)size);
        return false;
    }

    int decoded_w = 0;
    int decoded_h = 0;
    int channels = 0;
    uint8_t *decoded = stbi_load_from_memory(png, (int)size, &decoded_w, &decoded_h, &channels, 3);
    heap_caps_free(png);
    if (decoded == NULL || decoded_w <= 0 || decoded_h <= 0) {
        if (decoded != NULL) {
            stbi_image_free(decoded);
        }
        ESP_LOGW(TAG, "moon PNG decode failed len=%u err=%s", (unsigned)size, stbi_failure_reason());
        return false;
    }

    s_moon_pixels = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_moon_pixels == NULL) {
        s_moon_pixels = heap_caps_malloc(expected, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_moon_pixels == NULL) {
        ESP_LOGW(TAG, "moon texture pixel alloc failed");
        fclose(f);
        return false;
    }
    s_moon_render_pixels = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_moon_render_pixels == NULL) {
        ESP_LOGW(TAG, "moon render pixel alloc failed");
        heap_caps_free(s_moon_pixels);
        s_moon_pixels = NULL;
        stbi_image_free(decoded);
        return false;
    }

    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
            const int src_y = (int)((int64_t)y * decoded_h / FACULTY175_LCD_H);
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const int src_x = (int)((int64_t)x * decoded_w / FACULTY175_LCD_W);
            const uint8_t *px = decoded + ((size_t)src_y * (size_t)decoded_w + (size_t)src_x) * 3u;
            const size_t idx = ((size_t)y * FACULTY175_LCD_W + (size_t)x) * 4u;
            s_moon_pixels[idx + 0u] = px[2];
            s_moon_pixels[idx + 1u] = px[1];
            s_moon_pixels[idx + 2u] = px[0];
            s_moon_pixels[idx + 3u] = 0xff;
        }
    }
    stbi_image_free(decoded);
    memcpy(s_moon_render_pixels, s_moon_pixels, expected);

    s_moon_texture = (lv_image_dsc_t) {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = FACULTY175_LCD_W,
                .h = FACULTY175_LCD_H,
                .stride = FACULTY175_LCD_W * 4u,
                .reserved_2 = 0,
            },
        .data_size = expected,
        .data = (const uint8_t *)s_moon_render_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    ESP_LOGI(TAG, "moon texture loaded from SPIFFS PNG %dx%d -> %dx%d",
             decoded_w,
             decoded_h,
             FACULTY175_LCD_W,
             FACULTY175_LCD_H);
    return true;
}

static void create_moon_screen(void)
{
    s_moon_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_moon_screen);
    lv_obj_set_size(s_moon_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_moon_screen, lv_color_hex(0x02050a), 0);
    lv_obj_set_style_bg_opa(s_moon_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_moon_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (moon_texture_load()) {
        s_moon_image = lv_image_create(s_moon_screen);
        lv_image_set_src(s_moon_image, &s_moon_texture);
        lv_obj_align(s_moon_image, LV_ALIGN_CENTER, 0, 0);
    } else {
        s_moon_fallback_disc = make_circle(s_moon_screen, FACULTY175_LCD_W - 52, 0xc9c4b6, LV_OPA_COVER);
        lv_obj_center(s_moon_fallback_disc);
        lv_obj_set_style_shadow_width(s_moon_fallback_disc, 32, 0);
        lv_obj_set_style_shadow_color(s_moon_fallback_disc, lv_color_hex(0x141a22), 0);
        lv_obj_set_style_shadow_opa(s_moon_fallback_disc, 170, 0);
        lv_obj_set_style_border_width(s_moon_fallback_disc, 2, 0);
        lv_obj_set_style_border_color(s_moon_fallback_disc, lv_color_hex(0xeff1f4), 0);
        lv_obj_set_style_border_opa(s_moon_fallback_disc, 160, 0);
    }

    s_moon_hour_line = lv_line_create(s_moon_screen);
    s_moon_minute_line = lv_line_create(s_moon_screen);
    s_moon_second_line = lv_line_create(s_moon_screen);
    s_moon_hour_tail = lv_line_create(s_moon_screen);
    s_moon_minute_tail = lv_line_create(s_moon_screen);
    s_moon_hour_accent = lv_line_create(s_moon_screen);
    s_moon_minute_accent = lv_line_create(s_moon_screen);
    configure_watch_line(s_moon_hour_line, 14, 0x293244);
    configure_watch_line(s_moon_minute_line, 10, 0x314158);
    configure_watch_line(s_moon_second_line, 2, 0xa7d8ff);
    configure_watch_line(s_moon_hour_tail, 7, 0x293244);
    configure_watch_line(s_moon_minute_tail, 5, 0x314158);
    configure_watch_line(s_moon_hour_accent, 4, 0xf4f2eb);
    configure_watch_line(s_moon_minute_accent, 3, 0xd9e8f8);

    lv_obj_t *cap_shadow = lv_obj_create(s_moon_screen);
    lv_obj_remove_style_all(cap_shadow);
    lv_obj_set_size(cap_shadow, 28, 28);
    lv_obj_center(cap_shadow);
    lv_obj_set_style_radius(cap_shadow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap_shadow, lv_color_hex(0x1a2230), 0);
    lv_obj_set_style_bg_opa(cap_shadow, LV_OPA_COVER, 0);

    lv_obj_t *cap = lv_obj_create(s_moon_screen);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 18, 18);
    lv_obj_center(cap);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xf4f2eb), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap, 2, 0);
    lv_obj_set_style_border_color(cap, lv_color_hex(0x415067), 0);
    lv_obj_set_style_border_opa(cap, 220, 0);
    s_moon_rendered_day_s = UINT32_MAX;
}

static bool draw_moon(uint32_t anim_ms)
{
    if (moon_background_load565()) {
        const float phase = moon_phase_fraction(anim_ms);
        moon_render_shadow565(phase);
        faculty175_display_draw_rgb565(s_moon_bg565_render_pixels, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);

        bool time_valid = false;
        const uint32_t day_s = watch_seconds_of_day(anim_ms, &time_valid);
        const float second_u = (float)(day_s % 60u) / 60.0f;
        const float minute_u = (float)(day_s % 3600u) / 3600.0f;
        const float hour_u = (float)(day_s % 43200u) / 43200.0f;
        const uint16_t hour_col = faculty175_display_rgb888(0x29, 0x32, 0x44);
        const uint16_t minute_col = faculty175_display_rgb888(0x31, 0x41, 0x58);
        const uint16_t second_col = faculty175_display_rgb888(0xa7, 0xd8, 0xff);
        const uint16_t cap_shadow = faculty175_display_rgb888(0x1a, 0x22, 0x30);
        const uint16_t cap = faculty175_display_rgb888(0xf4, 0xf2, 0xeb);
        const uint16_t cap_ring = faculty175_display_rgb888(0x41, 0x50, 0x67);

        draw_direct_hand(hour_u, -28, 78, hour_col, 13);
        draw_direct_hand(minute_u, -38, 122, minute_col, 9);
        draw_direct_hand(second_u, -18, 146, second_col, 1);
        faculty175_display_fill_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 14, cap_shadow);
        faculty175_display_fill_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 9, cap);
        faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 9, cap_ring);
        faculty175_display_flush();
        return true;
    }

    if (s_moon_screen == NULL) {
        create_moon_screen();
    }
    if (s_moon_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_moon_screen) {
        lv_screen_load(s_moon_screen);
    }

    const float phase = moon_phase_fraction(anim_ms);
    moon_render_shadow(phase);

    bool time_valid = false;
    const uint32_t day_s = watch_seconds_of_day(anim_ms, &time_valid);
    if (day_s != s_moon_rendered_day_s) {
        const float second_u = (float)(day_s % 60u) / 60.0f;
        const float minute_u = (float)(day_s % 3600u) / 3600.0f;
        const float hour_u = (float)(day_s % 43200u) / 43200.0f;
        set_hand_segment(s_moon_hour_line, s_moon_hour_points, hour_u, 10, 78);
        set_hand_segment(s_moon_minute_line, s_moon_minute_points, minute_u, 8, 122);
        set_hand_segment(s_moon_hour_tail, s_moon_hour_tail_points, hour_u, -28, -6);
        set_hand_segment(s_moon_minute_tail, s_moon_minute_tail_points, minute_u, -38, -7);
        set_hand_segment(s_moon_hour_accent, s_moon_hour_accent_points, hour_u, 16, 64);
        set_hand_segment(s_moon_minute_accent, s_moon_minute_accent_points, minute_u, 18, 104);
        set_line_points(s_moon_second_line, s_moon_second_points, second_u, 146);
        s_moon_rendered_day_s = day_s;
    }

    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static lv_obj_t *make_tarot_label(lv_obj_t *parent, int32_t y, int32_t w, uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
    return label;
}

static void create_tarot_screen(void)
{
    s_tarot_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_tarot_screen);
    lv_obj_set_size(s_tarot_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_tarot_screen, lv_color_hex(0x05040a), 0);
    lv_obj_set_style_bg_opa(s_tarot_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_tarot_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_tarot_arc = lv_arc_create(s_tarot_screen);
    lv_obj_remove_style(s_tarot_arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_tarot_arc, 424, 424);
    lv_obj_center(s_tarot_arc);
    lv_arc_set_range(s_tarot_arc, 0, 100);
    lv_arc_set_value(s_tarot_arc, 72);
    lv_arc_set_bg_angles(s_tarot_arc, 210, 510);
    lv_arc_set_rotation(s_tarot_arc, 270);
    lv_obj_clear_flag(s_tarot_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_tarot_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_tarot_arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_tarot_arc, lv_color_hex(0x332238), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_tarot_arc, 160, LV_PART_MAIN);

    for (size_t i = 0; i < sizeof(s_tarot_card_backs) / sizeof(s_tarot_card_backs[0]); ++i) {
        s_tarot_card_backs[i] = lv_obj_create(s_tarot_screen);
        lv_obj_remove_style_all(s_tarot_card_backs[i]);
        lv_obj_set_size(s_tarot_card_backs[i], 58, 88);
        lv_obj_set_style_radius(s_tarot_card_backs[i], 4, 0);
        lv_obj_set_style_bg_color(s_tarot_card_backs[i], lv_color_hex(0x14101d), 0);
        lv_obj_set_style_bg_opa(s_tarot_card_backs[i], 220, 0);
        lv_obj_set_style_border_width(s_tarot_card_backs[i], 1, 0);
        lv_obj_set_style_border_color(s_tarot_card_backs[i], lv_color_hex(0x6a4b72), 0);
        lv_obj_set_style_border_opa(s_tarot_card_backs[i], 170, 0);
        lv_obj_align(s_tarot_card_backs[i], LV_ALIGN_CENTER, (int32_t)i * 46 - 92, 118 + (i % 2 ? 8 : 0));
    }

    lv_obj_t *frame = lv_obj_create(s_tarot_screen);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 263, 287);
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_set_style_radius(frame, 7, 0);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x100b16), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(0xd6a955), 0);
    lv_obj_set_style_border_opa(frame, 220, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    s_tarot_image = lv_image_create(s_tarot_screen);
    lv_obj_set_size(s_tarot_image, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_align(s_tarot_image, LV_ALIGN_TOP_LEFT, 0, 0);

    s_tarot_fallback = lv_obj_create(s_tarot_screen);
    lv_obj_remove_style_all(s_tarot_fallback);
    lv_obj_set_size(s_tarot_fallback, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_align(s_tarot_fallback, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(s_tarot_fallback, 5, 0);
    lv_obj_set_style_bg_color(s_tarot_fallback, lv_color_hex(0x191222), 0);
    lv_obj_set_style_bg_opa(s_tarot_fallback, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_tarot_fallback, 1, 0);
    lv_obj_set_style_border_color(s_tarot_fallback, lv_color_hex(0x806048), 0);
    lv_obj_set_style_border_opa(s_tarot_fallback, 180, 0);

    s_tarot_roman = make_tarot_label(s_tarot_screen, 143, 140, 0xffe2a0);
    s_tarot_title = make_tarot_label(s_tarot_screen, 362, 340, 0xf2e8d0);
    s_tarot_keyword = make_tarot_label(s_tarot_screen, 392, 280, 0xd6a955);
}

static bool draw_tarot(uint32_t anim_ms)
{
    if (s_tarot_screen == NULL) {
        create_tarot_screen();
    }
    if (s_tarot_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_tarot_screen) {
        faculty175_face_tarot_draw_card(anim_ms);
        lv_screen_load(s_tarot_screen);
    }

    const int idx = faculty175_face_tarot_current_card();
    const faculty175_tarot_card_t *card = faculty175_tarot_card_get(idx);
    if (card == NULL) {
        return false;
    }

    const uint32_t accent = ((uint32_t)card->r << 16) | ((uint32_t)card->g << 8) | card->b;
    lv_obj_set_style_arc_color(s_tarot_arc, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_label_set_text(s_tarot_title, card->title);
    lv_label_set_text(s_tarot_keyword, card->keyword);
    lv_label_set_text(s_tarot_roman, card->roman);
    lv_obj_set_style_text_color(s_tarot_keyword, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(s_tarot_roman, lv_color_hex(accent), 0);

    const uint16_t *pixels = NULL;
    int w = 0;
    int h = 0;
    const bool image_ok = faculty175_tarot_spiffs_image_get(idx, card, &pixels, &w, &h) && pixels != NULL &&
                          w == FACULTY175_LCD_W && h == FACULTY175_LCD_H;
    if (image_ok) {
        if (s_tarot_loaded_idx != idx) {
            s_tarot_texture = (lv_image_dsc_t) {
                .header = {
                    .magic = LV_IMAGE_HEADER_MAGIC,
                    .cf = LV_COLOR_FORMAT_RGB565,
                    .flags = 0,
                    .w = (uint32_t)w,
                    .h = (uint32_t)h,
                    .stride = (uint32_t)w * sizeof(uint16_t),
                    .reserved_2 = 0,
                },
                .data_size = (uint32_t)w * (uint32_t)h * sizeof(uint16_t),
                .data = (const uint8_t *)pixels,
                .reserved = NULL,
                .reserved_2 = NULL,
            };
            lv_image_set_src(s_tarot_image, &s_tarot_texture);
            s_tarot_loaded_idx = idx;
        }
        native_obj_hidden(s_tarot_image, false);
        native_obj_hidden(s_tarot_fallback, true);
    } else {
        native_obj_hidden(s_tarot_image, true);
        native_obj_hidden(s_tarot_fallback, false);
    }
    native_obj_hidden(s_tarot_title, image_ok);
    native_obj_hidden(s_tarot_keyword, image_ok);
    native_obj_hidden(s_tarot_roman, image_ok);

    lv_arc_set_value(s_tarot_arc, 55 + (int32_t)((anim_ms / 80u) % 30u));
    lv_obj_invalidate(s_tarot_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void configure_rune_line(lv_obj_t *line)
{
    lv_obj_set_size(line, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(0xf0c66f), 0);
}

static void set_rune_segment(int token, int seg, int x0, int y0, int x1, int y1)
{
    lv_obj_t *line = s_runes_glyph_lines[token][seg];
    if (line == NULL) {
        return;
    }
    s_runes_glyph_points[token][seg][0].x = x0;
    s_runes_glyph_points[token][seg][0].y = y0;
    s_runes_glyph_points[token][seg][1].x = x1;
    s_runes_glyph_points[token][seg][1].y = y1;
    lv_line_set_points(line, s_runes_glyph_points[token][seg], 2);
    native_obj_hidden(line, false);
}

static void clear_rune_segments(int token)
{
    for (int i = 0; i < 6; ++i) {
        native_obj_hidden(s_runes_glyph_lines[token][i], true);
    }
}

static void update_rune_glyph(int token, int rune_idx, int cx, int cy)
{
    clear_rune_segments(token);
    const int y0 = cy - 34;
    const int y1 = cy + 34;
    int seg = 0;
    set_rune_segment(token, seg++, cx, y0, cx, y1);
    switch (rune_idx % 12) {
        case 0:
            set_rune_segment(token, seg++, cx, y0 + 6, cx + 28, cy - 12);
            set_rune_segment(token, seg++, cx, cy - 2, cx + 24, cy + 14);
            break;
        case 1:
            set_rune_segment(token, seg++, cx, y0, cx - 26, cy);
            set_rune_segment(token, seg++, cx - 26, cy, cx, y1);
            set_rune_segment(token, seg++, cx, y0, cx + 26, cy);
            set_rune_segment(token, seg++, cx + 26, cy, cx, y1);
            break;
        case 2:
            set_rune_segment(token, seg++, cx - 24, y0, cx + 24, y1);
            set_rune_segment(token, seg++, cx + 24, y0, cx - 24, y1);
            break;
        case 3:
            set_rune_segment(token, seg++, cx, y0 + 8, cx + 28, cy - 10);
            set_rune_segment(token, seg++, cx, cy - 1, cx + 28, cy + 18);
            break;
        case 4:
            set_rune_segment(token, seg++, cx - 26, y0 + 8, cx + 24, y1 - 8);
            set_rune_segment(token, seg++, cx - 24, cy + 8, cx + 26, cy - 8);
            break;
        case 5:
            set_rune_segment(token, seg++, cx - 24, cy + 6, cx + 24, cy - 24);
            set_rune_segment(token, seg++, cx - 24, cy + 24, cx + 24, cy - 6);
            break;
        case 6:
            set_rune_segment(token, seg++, cx - 28, y0 + 6, cx + 28, y1 - 6);
            set_rune_segment(token, seg++, cx + 28, y0 + 6, cx - 28, y1 - 6);
            break;
        case 7:
            set_rune_segment(token, seg++, cx, cy, cx - 28, y0 + 6);
            set_rune_segment(token, seg++, cx, cy, cx + 28, y0 + 6);
            break;
        case 8:
            set_rune_segment(token, seg++, cx - 24, y0 + 10, cx + 24, y1 - 10);
            break;
        case 9:
            set_rune_segment(token, seg++, cx, cy - 6, cx + 26, y0 + 12);
            set_rune_segment(token, seg++, cx, cy + 8, cx - 26, y1 - 12);
            break;
        case 10:
            set_rune_segment(token, seg++, cx - 30, cy, cx + 30, cy);
            break;
        default:
            set_rune_segment(token, seg++, cx, cy - 4, cx + 26, y0 + 12);
            set_rune_segment(token, seg++, cx, cy + 4, cx - 26, y1 - 12);
            break;
    }
}

static void create_runes_screen(void)
{
    s_runes_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_runes_screen);
    lv_obj_set_size(s_runes_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_runes_screen, lv_color_hex(0x06080e), 0);
    lv_obj_set_style_bg_opa(s_runes_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_runes_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_runes_ring = lv_arc_create(s_runes_screen);
    lv_obj_remove_style(s_runes_ring, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_runes_ring, 426, 426);
    lv_obj_center(s_runes_ring);
    lv_arc_set_range(s_runes_ring, 0, 100);
    lv_arc_set_value(s_runes_ring, 84);
    lv_arc_set_bg_angles(s_runes_ring, 30, 330);
    lv_arc_set_rotation(s_runes_ring, 270);
    lv_obj_clear_flag(s_runes_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_runes_ring, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_runes_ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_runes_ring, lv_color_hex(0x44301f), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_runes_ring, lv_color_hex(0xeabf6b), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_runes_ring, 150, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_runes_ring, 220, LV_PART_INDICATOR);

    const int xs[3] = {104, 233, 362};
    const int ys[3] = {220, 196, 220};
    for (int i = 0; i < 3; ++i) {
        s_runes_tokens[i] = make_circle(s_runes_screen, 118, 0x251b16, LV_OPA_COVER);
        lv_obj_align(s_runes_tokens[i], LV_ALIGN_TOP_LEFT, xs[i] - 59, ys[i] - 59);
        lv_obj_set_style_border_width(s_runes_tokens[i], 2, 0);
        lv_obj_set_style_border_color(s_runes_tokens[i], lv_color_hex(0xb37a3d), 0);
        lv_obj_set_style_border_opa(s_runes_tokens[i], 220, 0);

        s_runes_slot_labels[i] = make_tarot_label(s_runes_screen, ys[i] + 67, 88, 0x8d8174);
        lv_obj_align(s_runes_slot_labels[i], LV_ALIGN_TOP_LEFT, xs[i] - 44, ys[i] + 67);
        s_runes_name_labels[i] = make_tarot_label(s_runes_screen, 332 + i * 22, 360, 0xe8dcc4);

        for (int seg = 0; seg < 6; ++seg) {
            s_runes_glyph_lines[i][seg] = lv_line_create(s_runes_screen);
            configure_rune_line(s_runes_glyph_lines[i][seg]);
            native_obj_hidden(s_runes_glyph_lines[i][seg], true);
        }
    }
    s_runes_keyword = make_tarot_label(s_runes_screen, 402, 360, 0x78d7c3);
}

static bool draw_runes(uint32_t anim_ms)
{
    if (s_runes_screen == NULL) {
        create_runes_screen();
    }
    if (s_runes_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_runes_screen) {
        lv_screen_load(s_runes_screen);
    }

    int spread[3] = {};
    if (!faculty175_face_runes_current(spread)) {
        return false;
    }
    const int xs[3] = {104, 233, 362};
    const int ys[3] = {216, 192, 216};
    char line[96];
    for (int i = 0; i < 3; ++i) {
        lv_label_set_text(s_runes_slot_labels[i], faculty175_face_rune_slot(i));
        if (s_runes_loaded[i] != spread[i]) {
            update_rune_glyph(i, spread[i], xs[i], ys[i]);
            s_runes_loaded[i] = spread[i];
        }
        snprintf(line, sizeof(line), "%s  %s", faculty175_face_rune_slot(i), faculty175_face_rune_name(spread[i]));
        lv_label_set_text(s_runes_name_labels[i], line);
    }
    snprintf(line,
             sizeof(line),
             "%s / %s / %s",
             faculty175_face_rune_keyword(spread[0]),
             faculty175_face_rune_keyword(spread[1]),
             faculty175_face_rune_keyword(spread[2]));
    lv_label_set_text(s_runes_keyword, line);
    lv_arc_set_value(s_runes_ring, 72 + (int32_t)((anim_ms / 95u) % 20u));

    lv_obj_invalidate(s_runes_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

typedef struct {
    const char *title;
    const char *keyword;
    uint32_t color;
} lvgl_lenormand_card_t;

static const lvgl_lenormand_card_t k_lvgl_lenormand_cards[36] = {
    {"RIDER", "message", 0xe6b060},      {"CLOVER", "chance", 0x74d686},
    {"SHIP", "passage", 0x6ebedc},       {"HOUSE", "home", 0xe6be76},
    {"TREE", "roots", 0x6ac278},         {"CLOUDS", "unclear", 0xa0b2be},
    {"SNAKE", "turning", 0xb0d260},      {"COFFIN", "ending", 0xa08e76},
    {"BOUQUET", "gift", 0xe696bc},       {"SCYTHE", "cut", 0xe6705c},
    {"WHIP", "friction", 0xd07e60},      {"BIRDS", "talk", 0xeebe56},
    {"CHILD", "new", 0x9cd2ee},          {"FOX", "strategy", 0xe08450},
    {"BEAR", "power", 0xdab05a},         {"STARS", "guidance", 0xacc4ff},
    {"STORK", "change", 0xd2dcee},       {"DOG", "loyalty", 0xee7c92},
    {"TOWER", "structure", 0xbaa0dc},    {"GARDEN", "public", 0x70cea0},
    {"MOUNTAIN", "block", 0x9cb0b8},     {"CROSSROADS", "choice", 0xd6b460},
    {"MICE", "loss", 0xa89684},          {"HEART", "love", 0xee6884},
    {"RING", "bond", 0xe8c658},          {"BOOK", "hidden", 0x8eb8e4},
    {"LETTER", "news", 0xdece96},        {"MAN", "querent", 0x9eceee},
    {"WOMAN", "querent", 0xee9ed2},      {"LILY", "peace", 0xe8dc98},
    {"SUN", "success", 0xf8ce54},        {"MOON", "recognition", 0xb0b2ee},
    {"KEY", "answer", 0xe8c456},         {"FISH", "flow", 0x66c2dc},
    {"ANCHOR", "stability", 0x76b8d6},   {"CROSS", "burden", 0xceb07a},
};

static int lenormand_daily_index(uint32_t anim_ms)
{
    if (!astrolabe_time_valid()) {
        return (int)((anim_ms / 60000u) % 36u);
    }
    struct tm local = {};
    astrolabe_time_local(&local);
    const int yday = local.tm_yday < 0 ? 0 : local.tm_yday;
    const int year = local.tm_year + 1900;
    return (yday + year * 11) % 36;
}

static void configure_lenormand_line(lv_obj_t *line, uint32_t color, int32_t width, lv_opa_t opa)
{
    lv_obj_set_size(line, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_opa(line, opa, 0);
}

static uint16_t lenormand_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t lenormand_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool lenormand_glyph_pack_open(void)
{
    if (s_lenormand_glyph_checked) {
        return s_lenormand_glyph_ready;
    }
    s_lenormand_glyph_checked = true;
    if (!moon_storage_ready()) {
        ESP_LOGW(TAG, "lenormand glyph storage unavailable");
        return false;
    }
    s_lenormand_glyph_file = fopen(FACULTY175_LENORMAND_GLYPH_PATH, "rb");
    if (s_lenormand_glyph_file == NULL) {
        ESP_LOGW(TAG, "missing %s", FACULTY175_LENORMAND_GLYPH_PATH);
        return false;
    }

    uint8_t hdr[20] = {};
    if (fread(hdr, 1, sizeof(hdr), s_lenormand_glyph_file) != sizeof(hdr) ||
        memcmp(hdr, "LENOCLR1", 8) != 0 ||
        lenormand_read_le16(&hdr[8]) != FACULTY175_LENORMAND_GLYPH_W ||
        lenormand_read_le16(&hdr[10]) != FACULTY175_LENORMAND_GLYPH_H ||
        lenormand_read_le16(&hdr[12]) != FACULTY175_LENORMAND_GLYPH_ROW_BYTES ||
        lenormand_read_le16(&hdr[14]) != FACULTY175_LENORMAND_GLYPH_COUNT ||
        lenormand_read_le32(&hdr[16]) != FACULTY175_LENORMAND_GLYPH_BYTES) {
        ESP_LOGW(TAG, "invalid lenormand glyph pack");
        fclose(s_lenormand_glyph_file);
        s_lenormand_glyph_file = NULL;
        return false;
    }

    const size_t pack_bytes = (size_t)FACULTY175_LENORMAND_GLYPH_BYTES * FACULTY175_LENORMAND_GLYPH_COUNT;
    s_lenormand_glyph_bits = heap_caps_malloc(pack_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_lenormand_glyph_bits == NULL) {
        s_lenormand_glyph_bits = heap_caps_malloc(pack_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_lenormand_glyph_bits == NULL) {
        ESP_LOGW(TAG, "lenormand glyph bit alloc failed");
        fclose(s_lenormand_glyph_file);
        s_lenormand_glyph_file = NULL;
        return false;
    }

    for (int i = 0; i < FACULTY175_LENORMAND_GLYPH_COUNT; ++i) {
        uint8_t *dst = s_lenormand_glyph_bits + (size_t)i * FACULTY175_LENORMAND_GLYPH_BYTES;
        if (fread(dst, 1, FACULTY175_LENORMAND_GLYPH_BYTES, s_lenormand_glyph_file) !=
            FACULTY175_LENORMAND_GLYPH_BYTES) {
            ESP_LOGW(TAG, "lenormand glyph pack read failed idx=%d", i);
            fclose(s_lenormand_glyph_file);
            s_lenormand_glyph_file = NULL;
            free(s_lenormand_glyph_bits);
            s_lenormand_glyph_bits = NULL;
            return false;
        }
        if ((i & 0x03) == 0x03) {
            vTaskDelay(1);
        }
    }
    fclose(s_lenormand_glyph_file);
    s_lenormand_glyph_file = NULL;
    s_lenormand_glyph_current_bits = s_lenormand_glyph_bits;
    s_lenormand_glyph_ready = true;
    ESP_LOGI(TAG, "lenormand glyph pack ready bytes=%u", (unsigned)pack_bytes);
    return true;
}

static bool lenormand_glyph_pack_load(int idx)
{
    if (!lenormand_glyph_pack_open()) {
        return false;
    }
    if (idx < 0 || idx >= FACULTY175_LENORMAND_GLYPH_COUNT) {
        return false;
    }
    s_lenormand_glyph_current_bits = s_lenormand_glyph_bits + (size_t)idx * FACULTY175_LENORMAND_GLYPH_BYTES;
    return true;
}

static bool update_lenormand_emoji_glyph(int idx, uint32_t color)
{
    if (idx < 0 || idx >= FACULTY175_LENORMAND_GLYPH_COUNT) {
        return false;
    }
    if (s_lenormand_glyph_pixels == NULL) {
        const size_t pixel_count = FACULTY175_LENORMAND_GLYPH_W * FACULTY175_LENORMAND_GLYPH_H;
        s_lenormand_glyph_pixels = heap_caps_malloc(pixel_count * 4u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_lenormand_glyph_pixels == NULL) {
            s_lenormand_glyph_pixels = heap_caps_malloc(pixel_count * 4u, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_lenormand_glyph_pixels == NULL) {
            return false;
        }
        s_lenormand_glyph_texture = (lv_image_dsc_t) {
            .header = {
                .magic = LV_IMAGE_HEADER_MAGIC,
                .cf = LV_COLOR_FORMAT_ARGB8888,
                .flags = 0,
                .w = FACULTY175_LENORMAND_GLYPH_W,
                .h = FACULTY175_LENORMAND_GLYPH_H,
                .stride = FACULTY175_LENORMAND_GLYPH_ROW_BYTES,
                .reserved_2 = 0,
            },
            .data_size = FACULTY175_LENORMAND_GLYPH_BYTES,
            .data = s_lenormand_glyph_pixels,
            .reserved = NULL,
            .reserved_2 = NULL,
        };
    }

    if (s_lenormand_rendered_glyph == idx && s_lenormand_rendered_color == color) {
        return true;
    }
    if (!lenormand_glyph_pack_load(idx)) {
        return false;
    }

    memcpy(s_lenormand_glyph_pixels, s_lenormand_glyph_current_bits, FACULTY175_LENORMAND_GLYPH_BYTES);
    s_lenormand_rendered_glyph = idx;
    s_lenormand_rendered_color = color;
    return true;
}

static void create_lenormand_screen(void)
{
    s_lenormand_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_lenormand_screen);
    lv_obj_set_size(s_lenormand_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_lenormand_screen, lv_color_hex(0x0a0910), 0);
    lv_obj_set_style_bg_opa(s_lenormand_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_lenormand_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    for (int i = 0; i < 36; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 36.0f;
        s_lenormand_tick_points[i][0].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 205.0f);
        s_lenormand_tick_points[i][0].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 205.0f);
        s_lenormand_tick_points[i][1].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 220.0f);
        s_lenormand_tick_points[i][1].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 220.0f);
        s_lenormand_ticks[i] = lv_line_create(s_lenormand_screen);
        configure_lenormand_line(s_lenormand_ticks[i], 0x3a3044, 2, 165);
        lv_line_set_points(s_lenormand_ticks[i], s_lenormand_tick_points[i], 2);
    }

    s_lenormand_glyph_image = lv_image_create(s_lenormand_screen);
    lv_obj_set_size(s_lenormand_glyph_image, FACULTY175_LENORMAND_GLYPH_W, FACULTY175_LENORMAND_GLYPH_H);
    lv_obj_align(s_lenormand_glyph_image, LV_ALIGN_CENTER, 0, -18);
    native_obj_hidden(s_lenormand_glyph_image, true);

    s_lenormand_number = make_tarot_label(s_lenormand_screen, 82, 80, 0xe6b060);
    s_lenormand_title = make_tarot_label(s_lenormand_screen, 304, 180, 0xf6ebd2);
    s_lenormand_keyword = make_tarot_label(s_lenormand_screen, 332, 180, 0xa08050);
    s_lenormand_status = make_tarot_label(s_lenormand_screen, 404, 300, 0xa69aae);

}

static bool draw_lenormand(uint32_t anim_ms)
{
    if (s_lenormand_screen == NULL) {
        create_lenormand_screen();
    }
    if (s_lenormand_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_lenormand_screen) {
        lv_screen_load(s_lenormand_screen);
    }

    const int idx = lenormand_daily_index(anim_ms);
    const lvgl_lenormand_card_t *card = &k_lvgl_lenormand_cards[idx];
    for (int i = 0; i < 36; ++i) {
        const bool active = i == idx;
        lv_obj_set_style_line_color(s_lenormand_ticks[i], lv_color_hex(active ? card->color : 0x3a3044), 0);
        lv_obj_set_style_line_width(s_lenormand_ticks[i], active ? 5 : 2, 0);
        lv_obj_set_style_line_opa(s_lenormand_ticks[i], active ? LV_OPA_COVER : 165, 0);
    }

    char num[8];
    snprintf(num, sizeof(num), "%02d", idx + 1);
    lv_label_set_text(s_lenormand_number, num);
    lv_label_set_text(s_lenormand_title, card->title);
    lv_label_set_text(s_lenormand_keyword, card->keyword);
    lv_label_set_text(s_lenormand_status, astrolabe_time_valid() ? "LENORMAND  DAILY CARD" : "LENORMAND  WAITING FOR TIME");
    lv_obj_set_style_text_color(s_lenormand_number, lv_color_hex(card->color), 0);
    lv_obj_set_style_text_color(s_lenormand_keyword, lv_color_hex(card->color), 0);
    if (update_lenormand_emoji_glyph(idx, card->color) && s_lenormand_glyph_image != NULL) {
        lv_image_set_src(s_lenormand_glyph_image, &s_lenormand_glyph_texture);
        native_obj_hidden(s_lenormand_glyph_image, false);
        lv_obj_invalidate(s_lenormand_glyph_image);
    }

    lv_obj_invalidate(s_lenormand_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void create_piano_screen(void)
{
    s_piano_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_piano_screen);
    lv_obj_set_size(s_piano_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_piano_screen, lv_color_hex(0x05070c), 0);
    lv_obj_set_style_bg_opa(s_piano_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_piano_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_piano_title = make_tarot_label(s_piano_screen, 54, 300, 0xe8e6dc);
    lv_label_set_text(s_piano_title, "PIANO");
    s_piano_status = make_tarot_label(s_piano_screen, 392, 320, 0x9ca8ba);

    const int white_w = 52;
    const int white_h = 218;
    const int left = 50;
    const int top = 136;
    static const char *const white_notes[7] = {"C", "D", "E", "F", "G", "A", "B"};
    for (int i = 0; i < 7; ++i) {
        s_piano_white[i] = lv_obj_create(s_piano_screen);
        lv_obj_remove_style_all(s_piano_white[i]);
        lv_obj_set_size(s_piano_white[i], white_w, white_h);
        lv_obj_align(s_piano_white[i], LV_ALIGN_TOP_LEFT, left + i * white_w, top);
        lv_obj_set_style_bg_color(s_piano_white[i], lv_color_hex(0xe8e2d4), 0);
        lv_obj_set_style_bg_opa(s_piano_white[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_piano_white[i], 2, 0);
        lv_obj_set_style_border_color(s_piano_white[i], lv_color_hex(0x202530), 0);
        lv_obj_set_style_radius(s_piano_white[i], 4, 0);
        lv_obj_clear_flag(s_piano_white[i], LV_OBJ_FLAG_SCROLLABLE);

        s_piano_white_labels[i] = make_tarot_label(s_piano_screen, top + white_h - 34, 32, 0x1c2230);
        lv_label_set_text(s_piano_white_labels[i], white_notes[i]);
        lv_obj_align(s_piano_white_labels[i], LV_ALIGN_TOP_LEFT, left + i * white_w + 10, top + white_h - 34);
    }

    const int black_w = 34;
    const int black_h = 132;
    static const int black_after_white[5] = {0, 1, 3, 4, 5};
    static const char *const black_notes[5] = {"C#", "D#", "F#", "G#", "A#"};
    for (int i = 0; i < 5; ++i) {
        const int x = left + (black_after_white[i] + 1) * white_w - black_w / 2;
        s_piano_black[i] = lv_obj_create(s_piano_screen);
        lv_obj_remove_style_all(s_piano_black[i]);
        lv_obj_set_size(s_piano_black[i], black_w, black_h);
        lv_obj_align(s_piano_black[i], LV_ALIGN_TOP_LEFT, x, top);
        lv_obj_set_style_bg_color(s_piano_black[i], lv_color_hex(0x121620), 0);
        lv_obj_set_style_bg_opa(s_piano_black[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(s_piano_black[i], 1, 0);
        lv_obj_set_style_border_color(s_piano_black[i], lv_color_hex(0x6f7788), 0);
        lv_obj_set_style_radius(s_piano_black[i], 4, 0);
        lv_obj_clear_flag(s_piano_black[i], LV_OBJ_FLAG_SCROLLABLE);

        s_piano_black_labels[i] = make_tarot_label(s_piano_screen, top + 98, 34, 0xd0d6e2);
        lv_label_set_text(s_piano_black_labels[i], black_notes[i]);
        lv_obj_align(s_piano_black_labels[i], LV_ALIGN_TOP_LEFT, x - 1, top + 98);
    }

    s_piano_pulse = make_circle(s_piano_screen, 18, 0xffd06a, LV_OPA_COVER);
}

static bool draw_piano(uint32_t anim_ms)
{
    if (s_piano_screen == NULL) {
        create_piano_screen();
    }
    if (s_piano_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_piano_screen) {
        lv_screen_load(s_piano_screen);
    }

    const int active = (int)((anim_ms / 420u) % 7u);
    const int white_w = 52;
    const int left = 50;
    const int top = 136;
    for (int i = 0; i < 7; ++i) {
        const bool on = i == active;
        lv_obj_set_style_bg_color(s_piano_white[i], lv_color_hex(on ? 0xffd06a : 0xe8e2d4), 0);
        lv_obj_set_style_border_color(s_piano_white[i], lv_color_hex(on ? 0xfff0b0 : 0x202530), 0);
        lv_obj_set_style_text_color(s_piano_white_labels[i], lv_color_hex(on ? 0x291d06 : 0x1c2230), 0);
    }
    const int black_active = (int)((anim_ms / 610u) % 5u);
    for (int i = 0; i < 5; ++i) {
        const bool on = i == black_active;
        lv_obj_set_style_bg_color(s_piano_black[i], lv_color_hex(on ? 0x6fc8ff : 0x121620), 0);
        lv_obj_set_style_bg_opa(s_piano_black[i], on ? 230 : LV_OPA_COVER, 0);
    }

    const int pulse_x = left + active * white_w + white_w / 2 - 9;
    const int pulse_y = top + 174 + (int)((anim_ms / 80u) % 9u);
    lv_obj_align(s_piano_pulse, LV_ALIGN_TOP_LEFT, pulse_x, pulse_y);
    char status[48];
    static const char *const white_notes[7] = {"C", "D", "E", "F", "G", "A", "B"};
    snprintf(status, sizeof(status), "ONE OCTAVE  %s", white_notes[active]);
    lv_label_set_text(s_piano_status, status);

    lv_obj_invalidate(s_piano_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void configure_solar_line(lv_obj_t *line, uint32_t color, int32_t width, lv_opa_t opa)
{
    lv_obj_set_size(line, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_opa(line, opa, 0);
}

static void create_solar_screen(void)
{
    s_solar_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_solar_screen);
    lv_obj_set_size(s_solar_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_solar_screen, lv_color_hex(0x030406), 0);
    lv_obj_set_style_bg_opa(s_solar_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_solar_screen, LV_OBJ_FLAG_SCROLLABLE);

    static const int corona_sizes[4] = {430, 384, 336, 292};
    static const uint32_t corona_colors[4] = {0x402018, 0x74321a, 0xb95b20, 0xff9e36};
    for (int i = 0; i < 4; ++i) {
        s_solar_corona[i] = make_circle(s_solar_screen, corona_sizes[i], corona_colors[i], (lv_opa_t)(34 + i * 18));
        lv_obj_center(s_solar_corona[i]);
    }

    s_solar_disk = make_circle(s_solar_screen, 282, 0xffb238, LV_OPA_COVER);
    lv_obj_center(s_solar_disk);
    lv_obj_set_style_bg_grad_dir(s_solar_disk, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(s_solar_disk, lv_color_hex(0xd95016), 0);

    s_solar_limb = lv_arc_create(s_solar_screen);
    lv_obj_remove_style(s_solar_limb, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_solar_limb, 294, 294);
    lv_obj_center(s_solar_limb);
    lv_arc_set_bg_angles(s_solar_limb, 0, 360);
    lv_arc_set_range(s_solar_limb, 0, 100);
    lv_arc_set_value(s_solar_limb, 100);
    lv_obj_clear_flag(s_solar_limb, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_solar_limb, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_solar_limb, lv_color_hex(0xffd16a), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_solar_limb, 180, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_solar_limb, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_solar_limb, 0, LV_PART_INDICATOR);

    for (int i = 0; i < 7; ++i) {
        s_solar_regions[i] = make_circle(s_solar_screen, i < 3 ? 28 : 18, i < 3 ? 0x321308 : 0x4a1b0a, 176);
    }
    for (int i = 0; i < 3; ++i) {
        s_solar_flare[i] = make_circle(s_solar_screen, 18 + i * 10, 0xfff2a0, (lv_opa_t)(210 - i * 56));
    }
    for (int i = 0; i < 3; ++i) {
        s_solar_cme[i] = lv_line_create(s_solar_screen);
        configure_solar_line(s_solar_cme[i], i == 0 ? 0xffe8a4 : 0xff8e48, i == 0 ? 4 : 3, (lv_opa_t)(190 - i * 34));
    }
    for (int i = 0; i < 4; ++i) {
        s_solar_region_labels[i] = make_tarot_label(s_solar_screen, 0, 54, 0x2a1408);
    }

    s_solar_title = make_tarot_label(s_solar_screen, 30, 280, 0xffe2a4);
    lv_label_set_text(s_solar_title, "SOLAR");
    s_solar_status = make_tarot_label(s_solar_screen, 392, 350, 0xffbd68);
    s_solar_source = make_tarot_label(s_solar_screen, 414, 360, 0x8fd6ff);
    lv_label_set_text(s_solar_source, "SDO NRT  DONKI CACHE");
}

static bool draw_solar(uint32_t anim_ms)
{
    if (s_solar_screen == NULL) {
        create_solar_screen();
    }
    if (s_solar_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_solar_screen) {
        lv_screen_load(s_solar_screen);
    }

    const int32_t cx = FACULTY175_LCD_W / 2;
    const int32_t cy = FACULTY175_LCD_H / 2;
    for (int i = 0; i < 4; ++i) {
        const lv_opa_t opa = (lv_opa_t)(26 + i * 16 + (int)((anim_ms / 180u + (uint32_t)i * 7u) % 14u));
        lv_obj_set_style_bg_opa(s_solar_corona[i], opa, 0);
    }

    static const float region_pos[7][2] = {
        {-0.48f, -0.18f}, {-0.18f, 0.28f}, {0.34f, -0.34f}, {0.52f, 0.18f},
        {-0.32f, -0.48f}, {0.05f, -0.08f}, {0.12f, 0.52f},
    };
    for (int i = 0; i < 7; ++i) {
        const float drift = sinf(((float)anim_ms * 0.00045f) + (float)i * 0.7f) * 7.0f;
        const int32_t x = cx + (int32_t)lrintf(region_pos[i][0] * 132.0f + drift);
        const int32_t y = cy + (int32_t)lrintf(region_pos[i][1] * 132.0f);
        lv_obj_align(s_solar_regions[i], LV_ALIGN_TOP_LEFT, x - lv_obj_get_width(s_solar_regions[i]) / 2, y - lv_obj_get_height(s_solar_regions[i]) / 2);
        lv_obj_set_style_bg_opa(s_solar_regions[i], i < 3 ? 210 : 144, 0);
        if (i < 4) {
            char ar[8];
            snprintf(ar, sizeof(ar), "AR%d", 4 + i);
            lv_label_set_text(s_solar_region_labels[i], ar);
            lv_obj_align(s_solar_region_labels[i], LV_ALIGN_TOP_LEFT, x - 26, y - 8);
        }
    }

    const int flare_idx = (int)((anim_ms / 3600u) % 3u);
    const int32_t fx = cx + 64 + (int32_t)lrintf(sinf((float)anim_ms * 0.0011f) * 16.0f);
    const int32_t fy = cy - 72 + (int32_t)lrintf(cosf((float)anim_ms * 0.0009f) * 10.0f);
    for (int i = 0; i < 3; ++i) {
        const int size = 18 + i * 10 + (flare_idx == i ? 8 : 0);
        lv_obj_set_size(s_solar_flare[i], size, size);
        lv_obj_set_style_radius(s_solar_flare[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_align(s_solar_flare[i], LV_ALIGN_TOP_LEFT, fx - size / 2, fy - size / 2);
    }

    for (int i = 0; i < 3; ++i) {
        const float a = -1.05f + (float)i * 0.16f + sinf((float)anim_ms * 0.0005f) * 0.06f;
        s_solar_cme_points[i][0].x = (lv_value_precise_t)(cx + (int32_t)lrintf(cosf(a) * 118.0f));
        s_solar_cme_points[i][0].y = (lv_value_precise_t)(cy + (int32_t)lrintf(sinf(a) * 118.0f));
        s_solar_cme_points[i][1].x = (lv_value_precise_t)(cx + (int32_t)lrintf(cosf(a) * 212.0f));
        s_solar_cme_points[i][1].y = (lv_value_precise_t)(cy + (int32_t)lrintf(sinf(a) * 212.0f));
        lv_line_set_points(s_solar_cme[i], s_solar_cme_points[i], 2);
    }

    lv_label_set_text(s_solar_status, "AIA 193  HMI  FLR/CME");
    lv_obj_invalidate(s_solar_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static bool magnet_map_load(void)
{
    if (s_magnet_map_loaded) {
        return s_magnet_map_pixels != NULL;
    }
    s_magnet_map_loaded = true;

    if (!moon_storage_ready()) {
        return false;
    }

    const char *path = "/bust_cache/space/magnetosphere_466.rgb565";
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        ESP_LOGW(TAG, "magnetosphere map missing: %s", path);
        return false;
    }

    const size_t expected = (size_t)FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t);
    s_magnet_map_pixels = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_magnet_map_pixels == NULL) {
        s_magnet_map_pixels = heap_caps_malloc(expected, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_magnet_map_pixels == NULL) {
        ESP_LOGW(TAG, "magnetosphere map pixel alloc failed");
        fclose(f);
        return false;
    }

    const size_t got = fread(s_magnet_map_pixels, 1, expected, f);
    fclose(f);
    if (got != expected) {
        heap_caps_free(s_magnet_map_pixels);
        s_magnet_map_pixels = NULL;
        ESP_LOGW(TAG, "magnetosphere map short read %u/%u", (unsigned)got, (unsigned)expected);
        return false;
    }

    s_magnet_map_texture = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = FACULTY175_LCD_W,
            .h = FACULTY175_LCD_H,
            .stride = FACULTY175_LCD_W * sizeof(uint16_t),
            .reserved_2 = 0,
        },
        .data_size = FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t),
        .data = (const uint8_t *)s_magnet_map_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    ESP_LOGI(TAG, "magnetosphere map loaded from SPIFFS RGB565 (%u bytes)", (unsigned)expected);
    return true;
}

static void magnet_map_derive_field_model(void)
{
    if (s_magnet_map_pixels == NULL) {
        s_magnet_data_pressure = 0.62f;
        s_magnet_data_energy = 0.50f;
        return;
    }

    uint32_t dayside = 0;
    uint32_t nightside = 0;
    uint32_t hot = 0;
    uint32_t cyan = 0;
    uint32_t dayside_count = 0;
    uint32_t nightside_count = 0;
    for (int y = 0; y < FACULTY175_LCD_H; y += 4) {
        for (int x = 0; x < FACULTY175_LCD_W; x += 4) {
            const uint16_t px = s_magnet_map_pixels[y * FACULTY175_LCD_W + x];
            const uint8_t r = (uint8_t)(((px >> 11) & 0x1f) << 3);
            const uint8_t g = (uint8_t)(((px >> 5) & 0x3f) << 2);
            const uint8_t b = (uint8_t)((px & 0x1f) << 3);
            const uint32_t lum = (uint32_t)r + (uint32_t)g + (uint32_t)b;
            if (x > FACULTY175_LCD_W * 2 / 3) {
                dayside += lum;
                dayside_count++;
                if (r > 180 && g > 130) {
                    hot++;
                }
            } else if (x < FACULTY175_LCD_W / 3) {
                nightside += lum;
                nightside_count++;
                if (g > 150 && b > 160) {
                    cyan++;
                }
            }
        }
    }

    const float day = dayside_count > 0 ? (float)dayside / ((float)dayside_count * 765.0f) : 0.50f;
    const float night = nightside_count > 0 ? (float)nightside / ((float)nightside_count * 765.0f) : 0.35f;
    const float heat = dayside_count > 0 ? (float)hot / (float)dayside_count : 0.0f;
    const float flow = nightside_count > 0 ? (float)cyan / (float)nightside_count : 0.0f;
    s_magnet_data_pressure = fminf(1.0f, fmaxf(0.20f, 0.42f + (day - night) * 1.5f + heat * 1.7f));
    s_magnet_data_energy = fminf(1.0f, fmaxf(0.15f, 0.28f + heat * 2.2f + flow * 1.1f));
    ESP_LOGI(TAG,
             "magnetosphere SWMF model pressure=%.2f energy=%.2f",
             (double)s_magnet_data_pressure,
             (double)s_magnet_data_energy);
}

static void create_magnet_screen(void)
{
    s_magnet_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_magnet_screen);
    lv_obj_set_size(s_magnet_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_magnet_screen, lv_color_hex(0x020712), 0);
    lv_obj_set_style_bg_opa(s_magnet_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_magnet_screen, LV_OBJ_FLAG_SCROLLABLE);

    if (magnet_map_load()) {
        magnet_map_derive_field_model();
        s_magnet_map_image = lv_image_create(s_magnet_screen);
        lv_image_set_src(s_magnet_map_image, &s_magnet_map_texture);
        lv_obj_align(s_magnet_map_image, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_opa(s_magnet_map_image, 34, 0);
    }

    s_magnet_bow = lv_arc_create(s_magnet_screen);
    lv_obj_remove_style(s_magnet_bow, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_magnet_bow, 360, 360);
    lv_obj_align(s_magnet_bow, LV_ALIGN_CENTER, -76, 0);
    lv_arc_set_bg_angles(s_magnet_bow, 82, 278);
    lv_arc_set_range(s_magnet_bow, 0, 100);
    lv_arc_set_value(s_magnet_bow, 100);
    lv_obj_clear_flag(s_magnet_bow, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_magnet_bow, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_magnet_bow, lv_color_hex(0x79d8ff), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_magnet_bow, 116, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_magnet_bow, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_magnet_bow, 0, LV_PART_INDICATOR);

    for (int i = 0; i < 4; ++i) {
        s_magnet_tail[i] = lv_line_create(s_magnet_screen);
        configure_solar_line(s_magnet_tail[i], 0x5cbcff, 2, (lv_opa_t)(128 - i * 16));
    }
    for (int i = 0; i < 8; ++i) {
        s_magnet_field[i] = lv_line_create(s_magnet_screen);
        configure_solar_line(s_magnet_field[i], i < 4 ? 0x5ff7d8 : 0xa27bff, i % 4 == 0 ? 3 : 2, (lv_opa_t)(166 - i * 8));
    }
    for (int i = 0; i < 9; ++i) {
        s_magnet_particles[i] = make_circle(s_magnet_screen, i % 3 == 0 ? 8 : 6, i % 2 == 0 ? 0xffcc66 : 0x72d8ff, LV_OPA_COVER);
    }

    s_magnet_sun = make_circle(s_magnet_screen, 58, 0xffb64a, 154);
    lv_obj_align(s_magnet_sun, LV_ALIGN_LEFT_MID, -22, -88);

    s_magnet_earth_limb = make_circle(s_magnet_screen, 212, 0x79e0ff, 92);
    lv_obj_center(s_magnet_earth_limb);

    s_magnet_earth = make_circle(s_magnet_screen, 184, 0x1d83d8, LV_OPA_COVER);
    lv_obj_center(s_magnet_earth);
    lv_obj_set_style_bg_grad_dir(s_magnet_earth, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_grad_color(s_magnet_earth, lv_color_hex(0x08245e), 0);
    lv_obj_set_style_border_width(s_magnet_earth, 2, 0);
    lv_obj_set_style_border_color(s_magnet_earth, lv_color_hex(0x9feaff), 0);
    lv_obj_set_style_border_opa(s_magnet_earth, 154, 0);

    s_magnet_earth_shadow = make_circle(s_magnet_screen, 184, 0x000000, 82);
    lv_obj_center(s_magnet_earth_shadow);
    lv_obj_set_style_bg_grad_dir(s_magnet_earth_shadow, LV_GRAD_DIR_HOR, 0);
    lv_obj_set_style_bg_grad_color(s_magnet_earth_shadow, lv_color_hex(0x00132e), 0);

    for (int i = 0; i < 7; ++i) {
        s_magnet_surface[i] = make_circle(s_magnet_screen, i % 3 == 0 ? 28 : 20, i % 2 == 0 ? 0x4fdc96 : 0xb1e9ff, (lv_opa_t)(116 + i * 10));
    }

    for (int i = 0; i < 6; ++i) {
        s_magnet_grid[i] = lv_line_create(s_magnet_screen);
        configure_solar_line(s_magnet_grid[i], i < 3 ? 0x9ad7ff : 0x6ef0c8, 1, 84);
    }

    s_magnet_title = NULL;
    s_magnet_status = NULL;
    s_magnet_source = NULL;
}

static bool draw_magnetosphere(uint32_t anim_ms)
{
    if (s_magnet_screen == NULL) {
        create_magnet_screen();
    }
    if (s_magnet_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_magnet_screen) {
        lv_screen_load(s_magnet_screen);
    }

    const int32_t cx = FACULTY175_LCD_W / 2;
    const int32_t cy = FACULTY175_LCD_H / 2;
    const float pressure_wave = 0.5f + 0.5f * sinf((float)anim_ms * 0.00042f);
    const float pressure = fminf(1.0f, fmaxf(0.0f, s_magnet_data_pressure * 0.72f + pressure_wave * 0.28f));
    const float energy = fminf(1.0f, fmaxf(0.0f, s_magnet_data_energy));
    const float spin = (float)(anim_ms % 18000u) / 18000.0f * 6.2831853f;
    const float pulse = (0.5f + 0.5f * sinf((float)anim_ms * 0.0011f)) * (0.65f + energy * 0.45f);

    for (int i = 0; i < 4; ++i) {
        const float sign = i < 2 ? -1.0f : 1.0f;
        const float lane = 0.35f + (float)(i % 2) * 0.42f;
        for (int p = 0; p < 5; ++p) {
            const float t = (float)p / 4.0f;
            const float wave = sinf((float)anim_ms * 0.0011f + (float)i * 1.7f + t * 4.2f) * (10.0f + pressure * 8.0f);
            s_magnet_tail_points[i][p].x = (lv_value_precise_t)(cx + 42 + (int32_t)lrintf(t * (186.0f + energy * 40.0f)));
            s_magnet_tail_points[i][p].y = (lv_value_precise_t)(cy + (int32_t)lrintf(sign * (48.0f + lane * (58.0f + energy * 24.0f)) + wave));
        }
        lv_line_set_points(s_magnet_tail[i], s_magnet_tail_points[i], 5);
    }

    for (int i = 0; i < 8; ++i) {
        const float sign = i < 4 ? -1.0f : 1.0f;
        const float lane = (float)(i % 4 + 1) / 4.8f;
        const float rx = 82.0f + lane * (88.0f + pressure * 38.0f);
        const float ry = 32.0f + lane * (68.0f + energy * 24.0f);
        const float tilt = sinf(spin * 0.5f + (float)i * 0.31f) * 10.0f;
        for (int p = 0; p < 11; ++p) {
            const float t = (float)p / 10.0f;
            const float a = 3.14159265f * t;
            const float x = cosf(a) * rx + 18.0f * t;
            const float y = sign * sinf(a) * ry + tilt * (t - 0.5f);
            s_magnet_field_points[i][p].x = (lv_value_precise_t)(cx + (int32_t)lrintf(x));
            s_magnet_field_points[i][p].y = (lv_value_precise_t)(cy + (int32_t)lrintf(y));
        }
        lv_line_set_points(s_magnet_field[i], s_magnet_field_points[i], 11);
        lv_obj_set_style_line_opa(s_magnet_field[i], (lv_opa_t)(104 + (int)(pulse * 72.0f) - i * 5), LV_PART_MAIN);
    }

    for (int i = 0; i < 7; ++i) {
        static const float k_lon[7] = { -2.3f, -1.2f, -0.35f, 0.72f, 1.45f, 2.2f, 2.9f };
        static const float k_lat[7] = { -0.52f, 0.26f, -0.10f, 0.52f, -0.32f, 0.08f, 0.38f };
        const int32_t size = i % 3 == 0 ? 28 : 20;
        const float lon = k_lon[i] + spin;
        const float facing = cosf(lon);
        const int32_t x = cx + (int32_t)lrintf(sinf(lon) * 72.0f) - size / 2;
        const int32_t y = cy + (int32_t)lrintf(k_lat[i] * 74.0f) - size / 2;
        const bool hidden = facing < -0.30f;
        native_obj_hidden(s_magnet_surface[i], hidden);
        if (!hidden) {
            lv_obj_set_size(s_magnet_surface[i], size, (int32_t)lrintf((float)size * (0.54f + 0.46f * fabsf(facing))));
            lv_obj_set_style_radius(s_magnet_surface[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_align(s_magnet_surface[i], LV_ALIGN_TOP_LEFT, x, y);
            lv_obj_set_style_bg_opa(s_magnet_surface[i], (lv_opa_t)(82 + (int)(facing * 92.0f)), 0);
        }
    }

    for (int i = 0; i < 3; ++i) {
        const float lat = ((float)i - 1.0f) * 0.46f;
        const float ry = 82.0f * cosf(lat);
        for (int p = 0; p < 9; ++p) {
            const float a = -1.25f + (float)p / 8.0f * 2.50f;
            s_magnet_grid_points[i][p].x = (lv_value_precise_t)(cx + (int32_t)lrintf(sinf(a) * ry));
            s_magnet_grid_points[i][p].y = (lv_value_precise_t)(cy + (int32_t)lrintf(lat * 78.0f + cosf(a) * 11.0f));
        }
        lv_line_set_points(s_magnet_grid[i], s_magnet_grid_points[i], 9);
    }
    for (int i = 3; i < 6; ++i) {
        const float phase = spin + ((float)(i - 4) * 0.82f);
        const float rx = fabsf(cosf(phase)) * 72.0f;
        const int32_t xoff = (int32_t)lrintf(sinf(phase) * 30.0f);
        for (int p = 0; p < 9; ++p) {
            const float a = -1.15f + (float)p / 8.0f * 2.30f;
            s_magnet_grid_points[i][p].x = (lv_value_precise_t)(cx + xoff + (int32_t)lrintf(sinf(a) * rx));
            s_magnet_grid_points[i][p].y = (lv_value_precise_t)(cy + (int32_t)lrintf(cosf(a) * 82.0f));
        }
        lv_line_set_points(s_magnet_grid[i], s_magnet_grid_points[i], 9);
    }

    for (int i = 0; i < 9; ++i) {
        const int32_t x = 8 + (int32_t)((anim_ms / (28u + (uint32_t)i * 5u) + (uint32_t)i * 43u) % 236u);
        const int32_t y = 52 + i * 44 + (int32_t)lrintf(sinf((float)anim_ms * 0.001f + (float)i) * 14.0f);
        lv_obj_align(s_magnet_particles[i], LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_bg_opa(s_magnet_particles[i], (lv_opa_t)(72 + (int)((anim_ms / 90u + (uint32_t)i * 11u) % 82u)), 0);
    }

    lv_obj_set_style_arc_opa(s_magnet_bow, (lv_opa_t)(78 + (int)(pressure * 98.0f)), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_magnet_earth_limb, (lv_opa_t)(68 + (int)(pulse * 58.0f)), 0);
    lv_obj_invalidate(s_magnet_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static bool instrument_face_id(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_SPECTRUM:
        case FACULTY175_FACE_CHAKRA:
        case FACULTY175_FACE_BOWL:
        case FACULTY175_FACE_OCARINA:
        case FACULTY175_FACE_PITCH:
        case FACULTY175_FACE_BONGO:
        case FACULTY175_FACE_KALIMBA:
        case FACULTY175_FACE_DRONE:
        case FACULTY175_FACE_CHORD:
        case FACULTY175_FACE_LEVEL:
        case FACULTY175_FACE_TUNING:
        case FACULTY175_FACE_PANDRUM:
        case FACULTY175_FACE_ORIENT:
            return true;
        default:
            return false;
    }
}

static const char *instrument_title(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_SPECTRUM: return "SPECTRUM";
        case FACULTY175_FACE_CHAKRA: return "CHAKRA";
        case FACULTY175_FACE_BOWL: return "BOWL";
        case FACULTY175_FACE_OCARINA: return "OCARINA";
        case FACULTY175_FACE_PITCH: return "PITCH";
        case FACULTY175_FACE_BONGO: return "BONGO";
        case FACULTY175_FACE_KALIMBA: return "KALIMBA";
        case FACULTY175_FACE_DRONE: return "DRONE";
        case FACULTY175_FACE_CHORD: return "CHORD";
        case FACULTY175_FACE_LEVEL: return "LEVEL";
        case FACULTY175_FACE_TUNING: return "TUNING";
        case FACULTY175_FACE_PANDRUM: return "PAN DRUM";
        case FACULTY175_FACE_ORIENT: return "ORIENT";
        default: return "INSTRUMENT";
    }
}

static void instrument_set_line(int idx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, int32_t width, lv_opa_t opa)
{
    if (idx < 0 || idx >= 12 || s_instrument_lines[idx] == NULL) {
        return;
    }
    s_instrument_line_points[idx][0].x = (lv_value_precise_t)x0;
    s_instrument_line_points[idx][0].y = (lv_value_precise_t)y0;
    s_instrument_line_points[idx][1].x = (lv_value_precise_t)x1;
    s_instrument_line_points[idx][1].y = (lv_value_precise_t)y1;
    lv_line_set_points(s_instrument_lines[idx], s_instrument_line_points[idx], 2);
    lv_obj_set_style_line_color(s_instrument_lines[idx], lv_color_hex(color), 0);
    lv_obj_set_style_line_width(s_instrument_lines[idx], width, 0);
    lv_obj_set_style_line_opa(s_instrument_lines[idx], opa, 0);
    native_obj_hidden(s_instrument_lines[idx], false);
}

static void instrument_set_orb(int idx, int32_t x, int32_t y, int32_t size, uint32_t color, lv_opa_t opa)
{
    if (idx < 0 || idx >= 9 || s_instrument_orbs[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_instrument_orbs[idx], size, size);
    lv_obj_set_style_radius(s_instrument_orbs[idx], size <= 24 ? LV_RADIUS_CIRCLE : 0, 0);
    lv_obj_set_style_bg_color(s_instrument_orbs[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_instrument_orbs[idx], opa, 0);
    lv_obj_set_style_border_width(s_instrument_orbs[idx], 0, 0);
    lv_obj_align(s_instrument_orbs[idx], LV_ALIGN_TOP_LEFT, x - size / 2, y - size / 2);
    native_obj_hidden(s_instrument_orbs[idx], false);
}

static void instrument_set_bar(int idx, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, lv_opa_t opa, int32_t radius)
{
    (void)radius;
    if (idx < 0 || idx >= 14 || s_instrument_bars[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_instrument_bars[idx], w, h);
    lv_obj_set_style_radius(s_instrument_bars[idx], 0, 0);
    lv_obj_set_style_bg_color(s_instrument_bars[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_instrument_bars[idx], opa, 0);
    lv_obj_set_style_border_width(s_instrument_bars[idx], 0, 0);
    lv_obj_align(s_instrument_bars[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_instrument_bars[idx], false);
}

static void instrument_clear_objects(void)
{
    for (int i = 0; i < 9; ++i) {
        native_obj_hidden(s_instrument_orbs[i], true);
    }
    for (int i = 0; i < 14; ++i) {
        native_obj_hidden(s_instrument_bars[i], true);
    }
    for (int i = 0; i < 12; ++i) {
        native_obj_hidden(s_instrument_lines[i], true);
    }
    for (int i = 0; i < 4; ++i) {
        native_obj_hidden(s_instrument_labels[i], true);
    }
}

static void instrument_set_label(int idx, const char *text, int32_t x, int32_t y, int32_t w, uint32_t color)
{
    if (idx < 0 || idx >= 4 || s_instrument_labels[idx] == NULL) {
        return;
    }
    lv_label_set_text(s_instrument_labels[idx], text != NULL ? text : "");
    lv_obj_set_width(s_instrument_labels[idx], w);
    lv_obj_set_style_text_color(s_instrument_labels[idx], lv_color_hex(color), 0);
    lv_obj_align(s_instrument_labels[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_instrument_labels[idx], false);
}

static void create_instrument_screen(void)
{
    s_instrument_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_instrument_screen);
    lv_obj_set_size(s_instrument_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_instrument_screen, lv_color_hex(0x05070c), 0);
    lv_obj_set_style_bg_opa(s_instrument_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_instrument_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_instrument_title = make_tarot_label(s_instrument_screen, 34, 300, 0xe8edf8);
    s_instrument_status = make_tarot_label(s_instrument_screen, 394, 340, 0x9ca8ba);

    for (int i = 0; i < 9; ++i) {
        s_instrument_orbs[i] = make_circle(s_instrument_screen, 12, 0xffffff, LV_OPA_COVER);
        native_obj_hidden(s_instrument_orbs[i], true);
    }
    for (int i = 0; i < 14; ++i) {
        s_instrument_bars[i] = lv_obj_create(s_instrument_screen);
        lv_obj_remove_style_all(s_instrument_bars[i]);
        lv_obj_clear_flag(s_instrument_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        native_obj_hidden(s_instrument_bars[i], true);
    }
    for (int i = 0; i < 12; ++i) {
        s_instrument_lines[i] = lv_line_create(s_instrument_screen);
        configure_solar_line(s_instrument_lines[i], 0x9fd8ff, 3, 170);
        native_obj_hidden(s_instrument_lines[i], true);
    }
    for (int i = 0; i < 4; ++i) {
        s_instrument_labels[i] = make_tarot_label(s_instrument_screen, 0, 80, 0xcfd7e8);
        native_obj_hidden(s_instrument_labels[i], true);
    }
}

static void draw_instrument_spectrum(uint32_t anim_ms)
{
    for (int i = 0; i < 14; ++i) {
        const float wave = 0.5f + 0.5f * sinf((float)anim_ms * 0.004f + (float)i * 0.72f);
        const int32_t h = 28 + (int32_t)lrintf(wave * 176.0f);
        const uint32_t color = i < 5 ? 0x70d6ff : (i < 10 ? 0x9ef08a : 0xffcf66);
        instrument_set_bar(i, 54 + i * 29, 330 - h, 18, h, color, 220, 5);
    }
    lv_label_set_text(s_instrument_status, "LIVE FFT");
}

static void draw_instrument_chakra(uint32_t anim_ms)
{
    static const uint32_t colors[7] = {0xd84a5f, 0xf18f38, 0xf5d84a, 0x62d48d, 0x58c4f6, 0x6e7df0, 0xc875ff};
    for (int i = 0; i < 7; ++i) {
        const float pulse = 0.5f + 0.5f * sinf((float)anim_ms * 0.0017f + (float)i * 0.9f);
        const int32_t y = 338 - i * 43;
        instrument_set_orb(i, 233, y, 28 + (int32_t)lrintf(pulse * 12.0f), colors[i], 220);
    }
    instrument_set_line(0, 233, 88, 233, 354, 0xd8f2ff, 2, 110);
    lv_label_set_text(s_instrument_status, "TONAL CENTERS");
}

static void draw_instrument_bowl(uint32_t anim_ms)
{
    for (int i = 0; i < 5; ++i) {
        const int size = 116 + i * 42 + (int)((anim_ms / 180u + (uint32_t)i * 3u) % 8u);
        instrument_set_orb(i, 233, 232, size, i == 0 ? 0xd8b46a : 0x6d5230, (lv_opa_t)(210 - i * 30));
    }
    instrument_set_line(0, 306, 164, 368, 112, 0xf4df9c, 7, 220);
    instrument_set_orb(6, 372, 108, 22, 0xf6e1a4, LV_OPA_COVER);
    lv_label_set_text(s_instrument_status, "RESONANCE");
}

static void draw_instrument_ocarina(uint32_t anim_ms)
{
    instrument_set_orb(0, 230, 232, 226, 0x5ca6d8, LV_OPA_COVER);
    instrument_set_orb(1, 162, 208, 62, 0x74c1e8, LV_OPA_COVER);
    instrument_set_bar(0, 318, 218, 92, 30, 0x5ca6d8, LV_OPA_COVER, 15);
    static const int holes[6][2] = {{194, 208}, {236, 196}, {272, 220}, {214, 260}, {258, 272}, {300, 252}};
    for (int i = 0; i < 6; ++i) {
        const bool active = ((anim_ms / 520u) % 6u) == (uint32_t)i;
        instrument_set_orb(i + 2, holes[i][0], holes[i][1], active ? 25 : 18, active ? 0xffe28a : 0x0a2438, LV_OPA_COVER);
    }
    lv_label_set_text(s_instrument_status, "BREATH MAP");
}

static void draw_instrument_pitch(uint32_t anim_ms)
{
    const int32_t sway = (int32_t)lrintf(sinf((float)anim_ms * 0.002f) * 8.0f);
    instrument_set_line(0, 207 + sway, 132, 207 - sway, 308, 0x8fe4ff, 9, 230);
    instrument_set_line(1, 265 - sway, 132, 265 + sway, 308, 0x8fe4ff, 9, 230);
    instrument_set_line(2, 207, 308, 265, 308, 0x8fe4ff, 9, 230);
    instrument_set_line(3, 236, 308, 236, 370, 0xdff8ff, 7, 220);
    instrument_set_orb(0, 236, 384, 22, 0xffd26a, LV_OPA_COVER);
    lv_label_set_text(s_instrument_status, "REFERENCE TONE");
}

static void draw_instrument_bongo(uint32_t anim_ms)
{
    const bool left = ((anim_ms / 360u) % 2u) == 0;
    instrument_set_orb(0, 168, 238, left ? 132 : 118, left ? 0xffc56a : 0x8a4a2a, LV_OPA_COVER);
    instrument_set_orb(1, 292, 238, left ? 118 : 132, left ? 0x8a4a2a : 0xffc56a, LV_OPA_COVER);
    instrument_set_orb(2, 168, 238, 84, 0x2c1710, 185);
    instrument_set_orb(3, 292, 238, 84, 0x2c1710, 185);
    instrument_set_line(0, 126, 318, 112, 382, 0xe2d0b8, 5, 190);
    instrument_set_line(1, 334, 318, 350, 382, 0xe2d0b8, 5, 190);
    lv_label_set_text(s_instrument_status, "TOUCH DRUMS");
}

static void draw_instrument_kalimba(uint32_t anim_ms)
{
    instrument_set_bar(0, 126, 154, 214, 248, 0x8e5a34, LV_OPA_COVER, 18);
    instrument_set_orb(0, 233, 160, 90, 0x4a2b1a, 150);
    for (int i = 0; i < 7; ++i) {
        const int32_t h = 150 - abs(i - 3) * 16;
        const int32_t x = 162 + i * 22;
        const bool active = ((anim_ms / 430u) % 7u) == (uint32_t)i;
        instrument_set_bar(i + 1, x, 178, 12, h, active ? 0xffdc82 : 0xd8d6c8, LV_OPA_COVER, 5);
    }
    lv_label_set_text(s_instrument_status, "TINES");
}

static void draw_instrument_drone(uint32_t anim_ms)
{
    for (int i = 0; i < 7; ++i) {
        const int size = 74 + i * 36 + (int)((anim_ms / 220u + (uint32_t)i * 5u) % 10u);
        instrument_set_orb(i, 233, 232, size, i % 2 == 0 ? 0x654dff : 0x48d8c0, (lv_opa_t)(210 - i * 22));
    }
    instrument_set_line(0, 80, 232, 386, 232, 0xe8f4ff, 2, 130);
    lv_label_set_text(s_instrument_status, "SUSTAIN");
}

static void draw_instrument_chord(uint32_t anim_ms)
{
    static const int pts[4][2] = {{156, 270}, {226, 154}, {314, 258}, {232, 316}};
    static const uint32_t colors[4] = {0xffc96a, 0x8fe4ff, 0xc996ff, 0x98f0a2};
    for (int i = 0; i < 4; ++i) {
        instrument_set_orb(i, pts[i][0], pts[i][1], i == 1 ? 44 : 36, colors[i], LV_OPA_COVER);
    }
    instrument_set_line(0, pts[0][0], pts[0][1], pts[1][0], pts[1][1], 0xe8f4ff, 3, 160);
    instrument_set_line(1, pts[1][0], pts[1][1], pts[2][0], pts[2][1], 0xe8f4ff, 3, 160);
    instrument_set_line(2, pts[2][0], pts[2][1], pts[3][0], pts[3][1], 0xe8f4ff, 3, 160);
    instrument_set_line(3, pts[3][0], pts[3][1], pts[0][0], pts[0][1], 0xe8f4ff, 3, 160);
    instrument_set_label(0, "I", 146, 286, 32, 0x10131a);
    instrument_set_label(1, "III", 214, 141, 48, 0x10131a);
    instrument_set_label(2, "V", 304, 274, 32, 0x10131a);
    lv_label_set_text(s_instrument_status, "HARMONY");
}

static void draw_instrument_tuning(uint32_t anim_ms)
{
    instrument_set_orb(0, 233, 244, 224, 0x111a26, LV_OPA_COVER);
    instrument_set_orb(1, 233, 244, 178, 0x071016, LV_OPA_COVER);
    for (int i = 0; i < 9; ++i) {
        const float a = -2.25f + (float)i * 0.5625f;
        instrument_set_line(i, 233 + (int32_t)lrintf(cosf(a) * 78.0f), 244 + (int32_t)lrintf(sinf(a) * 78.0f),
                            233 + (int32_t)lrintf(cosf(a) * 104.0f), 244 + (int32_t)lrintf(sinf(a) * 104.0f),
                            0x8fa8be, 3, 170);
    }
    const float needle = -1.5708f + sinf((float)anim_ms * 0.0014f) * 0.5f;
    instrument_set_line(10, 233, 244, 233 + (int32_t)lrintf(cosf(needle) * 98.0f),
                        244 + (int32_t)lrintf(sinf(needle) * 98.0f), 0xffcf66, 5, 230);
    instrument_set_orb(2, 233, 244, 28, 0xffcf66, LV_OPA_COVER);
    lv_label_set_text(s_instrument_status, "A4 440");
}

static void draw_instrument_level(uint32_t anim_ms)
{
    const float tilt_x = sinf((float)anim_ms * 0.0011f);
    const float tilt_y = cosf((float)anim_ms * 0.0008f);
    instrument_set_orb(0, 233, 238, 246, 0x101720, LV_OPA_COVER);
    instrument_set_orb(1, 233, 238, 184, 0x071016, LV_OPA_COVER);
    instrument_set_line(0, 86, 238, 380, 238, 0x48657a, 3, 150);
    instrument_set_line(1, 233, 91, 233, 385, 0x48657a, 3, 150);
    instrument_set_orb(2, 233 + (int32_t)lrintf(tilt_x * 72.0f), 238 + (int32_t)lrintf(tilt_y * 72.0f), 42, 0x92e8ff, LV_OPA_COVER);
    instrument_set_orb(3, 233, 238, 18, 0xffcf66, LV_OPA_COVER);
    lv_label_set_text(s_instrument_status, "TILT BUBBLE");
}

static void draw_instrument_orient(uint32_t anim_ms)
{
    instrument_set_orb(0, 233, 238, 248, 0x111522, LV_OPA_COVER);
    instrument_set_orb(1, 233, 238, 190, 0x070a10, LV_OPA_COVER);
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5708f + (float)i * 0.523599f;
        instrument_set_line(i, 233 + (int32_t)lrintf(cosf(a) * 86.0f), 238 + (int32_t)lrintf(sinf(a) * 86.0f),
                            233 + (int32_t)lrintf(cosf(a) * 118.0f), 238 + (int32_t)lrintf(sinf(a) * 118.0f),
                            i % 3 == 0 ? 0xffcf66 : 0x7594aa, i % 3 == 0 ? 4 : 2, 180);
    }
    const float heading = -1.5708f + (float)(anim_ms % 9000u) / 9000.0f * 6.28318f;
    instrument_set_line(0, 233, 238, 233 + (int32_t)lrintf(cosf(heading) * 104.0f),
                        238 + (int32_t)lrintf(sinf(heading) * 104.0f), 0xff625f, 6, LV_OPA_COVER);
    instrument_set_line(1, 233, 238, 233 - (int32_t)lrintf(cosf(heading) * 74.0f),
                        238 - (int32_t)lrintf(sinf(heading) * 74.0f), 0x92e8ff, 5, LV_OPA_COVER);
    instrument_set_label(0, "N", 223, 98, 32, 0xffcf66);
    lv_label_set_text(s_instrument_status, "HEADING");
}

static void draw_instrument_pandrum(uint32_t anim_ms)
{
    instrument_set_orb(0, 233, 238, 256, 0x59616f, LV_OPA_COVER);
    instrument_set_orb(1, 233, 238, 206, 0x252c36, 170);
    instrument_set_orb(2, 233, 238, 62, 0xd2a862, LV_OPA_COVER);
    for (int i = 0; i < 8; ++i) {
        const float a = -1.5708f + (float)i * 0.785398f;
        const bool active = ((anim_ms / 500u) % 8u) == (uint32_t)i;
        instrument_set_orb(i + 1, 233 + (int32_t)lrintf(cosf(a) * 82.0f),
                           238 + (int32_t)lrintf(sinf(a) * 82.0f),
                           active ? 56 : 44, active ? 0xffd784 : 0x8c96aa, LV_OPA_COVER);
    }
    lv_label_set_text(s_instrument_status, "HANDPAN");
}

static bool draw_instrument(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (s_instrument_screen == NULL) {
        create_instrument_screen();
    }
    if (s_instrument_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_instrument_screen) {
        lv_screen_load(s_instrument_screen);
    }

    instrument_clear_objects();
    lv_label_set_text(s_instrument_title, instrument_title(id));
    switch (id) {
        case FACULTY175_FACE_SPECTRUM: draw_instrument_spectrum(anim_ms); break;
        case FACULTY175_FACE_CHAKRA: draw_instrument_chakra(anim_ms); break;
        case FACULTY175_FACE_BOWL: draw_instrument_bowl(anim_ms); break;
        case FACULTY175_FACE_OCARINA: draw_instrument_ocarina(anim_ms); break;
        case FACULTY175_FACE_PITCH: draw_instrument_pitch(anim_ms); break;
        case FACULTY175_FACE_BONGO: draw_instrument_bongo(anim_ms); break;
        case FACULTY175_FACE_KALIMBA: draw_instrument_kalimba(anim_ms); break;
        case FACULTY175_FACE_DRONE: draw_instrument_drone(anim_ms); break;
        case FACULTY175_FACE_CHORD: draw_instrument_chord(anim_ms); break;
        case FACULTY175_FACE_LEVEL: draw_instrument_level(anim_ms); break;
        case FACULTY175_FACE_TUNING: draw_instrument_tuning(anim_ms); break;
        case FACULTY175_FACE_PANDRUM: draw_instrument_pandrum(anim_ms); break;
        case FACULTY175_FACE_ORIENT: draw_instrument_orient(anim_ms); break;
        default: return false;
    }

    lv_obj_invalidate(s_instrument_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static bool oracle_face_id(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_INQ:
        case FACULTY175_FACE_GEOMANCY:
        case FACULTY175_FACE_ENOCHIAN:
            return true;
        default:
            return false;
    }
}

static const char *oracle_title(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_INQ: return "iNQ";
        case FACULTY175_FACE_GEOMANCY: return "GEOMANCY";
        case FACULTY175_FACE_ENOCHIAN: return "ENOCHIAN";
        default: return "ORACLE";
    }
}

static void oracle_set_line(int idx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, int32_t width, lv_opa_t opa)
{
    if (idx < 0 || idx >= 24 || s_oracle_lines[idx] == NULL) {
        return;
    }
    s_oracle_line_points[idx][0].x = (lv_value_precise_t)x0;
    s_oracle_line_points[idx][0].y = (lv_value_precise_t)y0;
    s_oracle_line_points[idx][1].x = (lv_value_precise_t)x1;
    s_oracle_line_points[idx][1].y = (lv_value_precise_t)y1;
    lv_line_set_points(s_oracle_lines[idx], s_oracle_line_points[idx], 2);
    lv_obj_set_style_line_color(s_oracle_lines[idx], lv_color_hex(color), 0);
    lv_obj_set_style_line_width(s_oracle_lines[idx], width, 0);
    lv_obj_set_style_line_opa(s_oracle_lines[idx], opa, 0);
    native_obj_hidden(s_oracle_lines[idx], false);
}

static void oracle_set_orb(int idx, int32_t x, int32_t y, int32_t size, uint32_t color, lv_opa_t opa)
{
    if (idx < 0 || idx >= 16 || s_oracle_orbs[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_oracle_orbs[idx], size, size);
    lv_obj_set_style_radius(s_oracle_orbs[idx], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_oracle_orbs[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_oracle_orbs[idx], opa, 0);
    lv_obj_set_style_border_width(s_oracle_orbs[idx], 0, 0);
    lv_obj_align(s_oracle_orbs[idx], LV_ALIGN_TOP_LEFT, x - size / 2, y - size / 2);
    native_obj_hidden(s_oracle_orbs[idx], false);
}

static void oracle_set_bar(int idx, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, lv_opa_t opa, int32_t radius)
{
    if (idx < 0 || idx >= 16 || s_oracle_bars[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_oracle_bars[idx], w, h);
    lv_obj_set_style_radius(s_oracle_bars[idx], radius, 0);
    lv_obj_set_style_bg_color(s_oracle_bars[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_oracle_bars[idx], opa, 0);
    lv_obj_set_style_border_width(s_oracle_bars[idx], 0, 0);
    lv_obj_align(s_oracle_bars[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_oracle_bars[idx], false);
}

static void oracle_set_label(int idx, const char *text, int32_t x, int32_t y, int32_t w, uint32_t color)
{
    if (idx < 0 || idx >= 12 || s_oracle_labels[idx] == NULL) {
        return;
    }
    lv_label_set_text(s_oracle_labels[idx], text != NULL ? text : "");
    lv_obj_set_width(s_oracle_labels[idx], w);
    lv_obj_set_style_text_color(s_oracle_labels[idx], lv_color_hex(color), 0);
    lv_obj_align(s_oracle_labels[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_oracle_labels[idx], false);
}

static void oracle_clear_objects(void)
{
    for (int i = 0; i < 16; ++i) {
        native_obj_hidden(s_oracle_orbs[i], true);
        native_obj_hidden(s_oracle_bars[i], true);
    }
    for (int i = 0; i < 24; ++i) {
        native_obj_hidden(s_oracle_lines[i], true);
    }
    for (int i = 0; i < 12; ++i) {
        native_obj_hidden(s_oracle_labels[i], true);
    }
}

static void create_oracle_screen(void)
{
    s_oracle_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_oracle_screen);
    lv_obj_set_size(s_oracle_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_oracle_screen, lv_color_hex(0x06040a), 0);
    lv_obj_set_style_bg_opa(s_oracle_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_oracle_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *outer = make_circle(s_oracle_screen, 432, 0x000000, 0);
    lv_obj_center(outer);
    lv_obj_set_style_border_width(outer, 2, 0);
    lv_obj_set_style_border_color(outer, lv_color_hex(0x3b3048), 0);
    lv_obj_set_style_border_opa(outer, 160, 0);

    s_oracle_title = make_tarot_label(s_oracle_screen, 34, 320, 0xf1e3ff);
    s_oracle_status = make_tarot_label(s_oracle_screen, 394, 340, 0xa994bd);
    for (int i = 0; i < 16; ++i) {
        s_oracle_orbs[i] = make_circle(s_oracle_screen, 12, 0xffffff, LV_OPA_COVER);
        native_obj_hidden(s_oracle_orbs[i], true);
        s_oracle_bars[i] = lv_obj_create(s_oracle_screen);
        lv_obj_remove_style_all(s_oracle_bars[i]);
        lv_obj_clear_flag(s_oracle_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        native_obj_hidden(s_oracle_bars[i], true);
    }
    for (int i = 0; i < 24; ++i) {
        s_oracle_lines[i] = lv_line_create(s_oracle_screen);
        configure_solar_line(s_oracle_lines[i], 0xd8c8ff, 2, 160);
        native_obj_hidden(s_oracle_lines[i], true);
    }
    for (int i = 0; i < 12; ++i) {
        s_oracle_labels[i] = make_tarot_label(s_oracle_screen, 0, 80, 0xd8c8ff);
        native_obj_hidden(s_oracle_labels[i], true);
    }
}

static void draw_oracle_inq(uint32_t anim_ms)
{
    oracle_set_bar(0, 128, 112, 210, 282, 0xf4ead6, LV_OPA_COVER, 5);
    oracle_set_bar(1, 142, 128, 182, 254, 0x221629, LV_OPA_COVER, 4);
    oracle_set_bar(2, 160, 146, 146, 58, 0x7acfe4, 210, 3);
    oracle_set_orb(0, 233, 246, 92, 0xf6ce76, LV_OPA_COVER);
    oracle_set_orb(1, 233, 246, 58, 0x221629, 190);
    for (int i = 0; i < 6; ++i) {
        const float a = -1.5708f + (float)i * 1.0472f + (float)anim_ms * 0.00035f;
        oracle_set_line(i, 233, 246, 233 + (int32_t)lrintf(cosf(a) * 106.0f),
                        246 + (int32_t)lrintf(sinf(a) * 106.0f), 0xf6ce76, 3, 150);
    }
    oracle_set_label(0, "?", 224, 228, 36, 0x1a101f);
    lv_label_set_text(s_oracle_status, "QUESTION CARD");
}

static void draw_oracle_geomancy(uint32_t anim_ms)
{
    static const uint8_t figures[4] = {0x9, 0x6, 0xb, 0xd};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            const bool single = ((figures[col] >> row) & 1u) != 0;
            const int32_t x = 116 + col * 72;
            const int32_t y = 148 + row * 48;
            const uint32_t color = row % 2 == 0 ? 0xecc46e : 0x83d7e8;
            if (single) {
                oracle_set_orb(col * 4 + row, x, y, 18, color, LV_OPA_COVER);
            } else {
                oracle_set_bar(col * 4 + row, x - 20, y - 7, 40, 14, color, LV_OPA_COVER, 7);
            }
        }
        oracle_set_label(col, col == 0 ? "M" : (col == 1 ? "D" : (col == 2 ? "N" : "J")), 104 + col * 72, 342, 32, 0x8a799e);
    }
    const int active = (int)((anim_ms / 680u) % 4u);
    oracle_set_bar(15, 90 + active * 72, 118, 52, 4, 0xf6e7a8, LV_OPA_COVER, 2);
    lv_label_set_text(s_oracle_status, "FOUR FIGURES");
}

static void draw_oracle_enochian(uint32_t anim_ms)
{
    const int left = 88;
    const int top = 116;
    const int cell = 38;
    for (int i = 0; i <= 7; ++i) {
        oracle_set_line(i, left, top + i * cell, left + 7 * cell, top + i * cell, 0x8c74b4, 2, 150);
        oracle_set_line(i + 8, left + i * cell, top, left + i * cell, top + 7 * cell, 0x8c74b4, 2, 150);
    }
    static const char *const letters[9] = {"A", "L", "D", "O", "N", "A", "I", "E", "X"};
    for (int i = 0; i < 9; ++i) {
        const int gx = (i * 2 + (int)(anim_ms / 900u)) % 7;
        const int gy = (i * 3 + 1) % 7;
        oracle_set_label(i, letters[i], left + gx * cell + 11, top + gy * cell + 9, 28, i % 2 == 0 ? 0xf1d78a : 0xb4e8ff);
    }
    oracle_set_orb(0, 233, 249, 26, 0xf1d78a, 210);
    lv_label_set_text(s_oracle_status, "WATCHTOWER");
}

static bool draw_oracle(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (s_oracle_screen == NULL) {
        create_oracle_screen();
    }
    if (s_oracle_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_oracle_screen) {
        lv_screen_load(s_oracle_screen);
    }

    oracle_clear_objects();
    lv_label_set_text(s_oracle_title, oracle_title(id));
    switch (id) {
        case FACULTY175_FACE_INQ: draw_oracle_inq(anim_ms); break;
        case FACULTY175_FACE_GEOMANCY: draw_oracle_geomancy(anim_ms); break;
        case FACULTY175_FACE_ENOCHIAN: draw_oracle_enochian(anim_ms); break;
        default: return false;
    }

    lv_obj_invalidate(s_oracle_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static bool utility_face_id(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_FACULTY:
        case FACULTY175_FACE_APOCALYPSO:
        case FACULTY175_FACE_DIGITAL:
        case FACULTY175_FACE_SPOTIFY:
        case FACULTY175_FACE_NOTES:
        case FACULTY175_FACE_CALCIFER:
        case FACULTY175_FACE_CASTALIA:
        case FACULTY175_FACE_ROCKET:
        case FACULTY175_FACE_RADAR:
        case FACULTY175_FACE_WEATHER:
        case FACULTY175_FACE_GLOBE:
        case FACULTY175_FACE_QUOTES:
        case FACULTY175_FACE_QDAY:
        case FACULTY175_FACE_FOCUS:
        case FACULTY175_FACE_BIOMETRICS:
        case FACULTY175_FACE_WATCHER:
        case FACULTY175_FACE_HID:
        case FACULTY175_FACE_BABEL:
        case FACULTY175_FACE_WSCAN:
        case FACULTY175_FACE_DEAUTH:
        case FACULTY175_FACE_EVILTWIN:
        case FACULTY175_FACE_HANDSHAKE:
        case FACULTY175_FACE_INCIDENTS:
        case FACULTY175_FACE_SETTINGS:
            return true;
        default:
            return false;
    }
}

static const char *utility_title(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_FACULTY: return "FACULTY";
        case FACULTY175_FACE_APOCALYPSO: return "APOCALYPSO";
        case FACULTY175_FACE_DIGITAL: return "DIGITAL";
        case FACULTY175_FACE_SPOTIFY: return "SPOTIFY";
        case FACULTY175_FACE_NOTES: return "NOTES";
        case FACULTY175_FACE_CALCIFER: return "CALCIFER";
        case FACULTY175_FACE_CASTALIA: return "CASTALIA";
        case FACULTY175_FACE_ROCKET: return "ROCKET";
        case FACULTY175_FACE_RADAR: return "RADAR";
        case FACULTY175_FACE_WEATHER: return "WEATHER";
        case FACULTY175_FACE_GLOBE: return "GLOBE";
        case FACULTY175_FACE_QUOTES: return "QUOTES";
        case FACULTY175_FACE_QDAY: return "QUESTION";
        case FACULTY175_FACE_FOCUS: return "FOCUS";
        case FACULTY175_FACE_BIOMETRICS: return "BIOMETRICS";
        case FACULTY175_FACE_WATCHER: return "WATCHER";
        case FACULTY175_FACE_HID: return "HID";
        case FACULTY175_FACE_WSCAN: return "WIFI SCAN";
        case FACULTY175_FACE_DEAUTH: return "DEAUTH";
        case FACULTY175_FACE_EVILTWIN: return "EVIL TWIN";
        case FACULTY175_FACE_HANDSHAKE: return "HANDSHAKE";
        case FACULTY175_FACE_INCIDENTS: return "INCIDENTS";
        case FACULTY175_FACE_BABEL: return "BABEL";
        case FACULTY175_FACE_DEATHSTAR: return "DEATH STAR";
        case FACULTY175_FACE_SETTINGS: return "SETTINGS";
        default: return "STATUS";
    }
}

static void utility_set_line(int idx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color, int32_t width, lv_opa_t opa)
{
    if (idx < 0 || idx >= 28 || s_utility_lines[idx] == NULL) {
        return;
    }
    s_utility_line_points[idx][0].x = (lv_value_precise_t)x0;
    s_utility_line_points[idx][0].y = (lv_value_precise_t)y0;
    s_utility_line_points[idx][1].x = (lv_value_precise_t)x1;
    s_utility_line_points[idx][1].y = (lv_value_precise_t)y1;
    lv_line_set_points(s_utility_lines[idx], s_utility_line_points[idx], 2);
    lv_obj_set_style_line_color(s_utility_lines[idx], lv_color_hex(color), 0);
    lv_obj_set_style_line_width(s_utility_lines[idx], width, 0);
    lv_obj_set_style_line_opa(s_utility_lines[idx], opa, 0);
    native_obj_hidden(s_utility_lines[idx], false);
}

static void utility_set_orb(int idx, int32_t x, int32_t y, int32_t size, uint32_t color, lv_opa_t opa)
{
    if (idx < 0 || idx >= 14 || s_utility_orbs[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_utility_orbs[idx], size, size);
    lv_obj_set_style_radius(s_utility_orbs[idx], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_utility_orbs[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_utility_orbs[idx], opa, 0);
    lv_obj_set_style_border_width(s_utility_orbs[idx], 0, 0);
    lv_obj_align(s_utility_orbs[idx], LV_ALIGN_TOP_LEFT, x - size / 2, y - size / 2);
    native_obj_hidden(s_utility_orbs[idx], false);
}

static void utility_set_bar(int idx, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color, lv_opa_t opa, int32_t radius)
{
    if (idx < 0 || idx >= 16 || s_utility_bars[idx] == NULL) {
        return;
    }
    lv_obj_set_size(s_utility_bars[idx], w, h);
    lv_obj_set_style_radius(s_utility_bars[idx], radius, 0);
    lv_obj_set_style_bg_color(s_utility_bars[idx], lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(s_utility_bars[idx], opa, 0);
    lv_obj_set_style_border_width(s_utility_bars[idx], 0, 0);
    lv_obj_align(s_utility_bars[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_utility_bars[idx], false);
}

static void utility_set_label(int idx, const char *text, int32_t x, int32_t y, int32_t w, uint32_t color)
{
    if (idx < 0 || idx >= 14 || s_utility_labels[idx] == NULL) {
        return;
    }
    lv_label_set_text(s_utility_labels[idx], text != NULL ? text : "");
    lv_obj_set_width(s_utility_labels[idx], w);
    lv_obj_set_style_text_color(s_utility_labels[idx], lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(s_utility_labels[idx], LV_OPA_COVER, 0);
    lv_obj_align(s_utility_labels[idx], LV_ALIGN_TOP_LEFT, x, y);
    native_obj_hidden(s_utility_labels[idx], false);
}

static void utility_clear_objects(void)
{
    if (s_rocket_image != NULL) {
        native_obj_hidden(s_rocket_image, true);
    }
    if (s_faculty_face_image != NULL) {
        native_obj_hidden(s_faculty_face_image, true);
    }
    if (s_deathstar_image != NULL) {
        native_obj_hidden(s_deathstar_image, true);
    }
    for (int i = 0; i < 14; ++i) {
        native_obj_hidden(s_utility_orbs[i], true);
    }
    for (int i = 0; i < 16; ++i) {
        native_obj_hidden(s_utility_bars[i], true);
    }
    for (int i = 0; i < 28; ++i) {
        native_obj_hidden(s_utility_lines[i], true);
    }
    for (int i = 0; i < 14; ++i) {
        native_obj_hidden(s_utility_labels[i], true);
    }
    native_obj_hidden(s_utility_qr, true);
}

static bool faculty_face_texture_load(void)
{
    const char *loaded_slug = faculty175_faculty_loaded_slug();
    if (s_faculty_face_pixels != NULL && loaded_slug != NULL && loaded_slug[0] != '\0' &&
        strcmp(s_faculty_face_loaded_slug, loaded_slug) == 0) {
        return true;
    }

    const size_t cap = (size_t)FACULTY175_FACULTY_BUST_W * (size_t)FACULTY175_FACULTY_BUST_H * 4u;
    if (s_faculty_face_pixels == NULL) {
        s_faculty_face_pixels = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_faculty_face_pixels == NULL) {
            s_faculty_face_pixels = heap_caps_malloc(cap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    }
    if (s_faculty_face_pixels == NULL) {
        ESP_LOGW(TAG, "faculty transparent bust alloc failed");
        return false;
    }

    int w = 0;
    int h = 0;
    char slug[64] = {};
    if (!faculty175_faculty_copy_bust_argb8888(s_faculty_face_pixels, cap, &w, &h, slug, sizeof(slug))) {
        s_faculty_face_loaded_slug[0] = '\0';
        return false;
    }
    if (w <= 0 || h <= 0) {
        s_faculty_face_loaded_slug[0] = '\0';
        return false;
    }

    s_faculty_face_texture = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_ARGB8888,
            .flags = 0,
            .w = w,
            .h = h,
            .stride = w * 4,
            .reserved_2 = 0,
        },
        .data_size = (uint32_t)((size_t)w * (size_t)h * 4u),
        .data = (const uint8_t *)s_faculty_face_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    strncpy(s_faculty_face_loaded_slug, slug, sizeof(s_faculty_face_loaded_slug) - 1u);
    s_faculty_face_loaded_slug[sizeof(s_faculty_face_loaded_slug) - 1u] = '\0';
    ESP_LOGI(TAG, "faculty transparent bust loaded slug=%s %dx%d", s_faculty_face_loaded_slug, w, h);
    return true;
}

static bool utility_show_faculty_bust_for_slug(const char *slug, int32_t y, lv_opa_t opa)
{
    if (slug == NULL || slug[0] == '\0') {
        return false;
    }
    const char *loaded_slug = faculty175_faculty_loaded_slug();
    if (loaded_slug == NULL || strcmp(loaded_slug, slug) != 0) {
        static char s_last_quote_bust_req[64];
        static int64_t s_last_quote_bust_req_us;
        const faculty175_faculty_bust_status_t status = faculty175_faculty_bust_status();
        const int64_t now_us = esp_timer_get_time();
        if (status != FACULTY175_FACULTY_BUST_LOADING &&
            (strcmp(s_last_quote_bust_req, slug) != 0 || now_us - s_last_quote_bust_req_us >= 15000000LL)) {
            faculty175_faculty_request_bust(slug);
            strncpy(s_last_quote_bust_req, slug, sizeof(s_last_quote_bust_req) - 1u);
            s_last_quote_bust_req[sizeof(s_last_quote_bust_req) - 1u] = '\0';
            s_last_quote_bust_req_us = now_us;
        }
        return false;
    }
    if (!faculty_face_texture_load() || strcmp(s_faculty_face_loaded_slug, slug) != 0) {
        return false;
    }
    if (s_faculty_face_image == NULL) {
        s_faculty_face_image = lv_image_create(s_utility_screen);
    }
    if (s_faculty_face_image == NULL) {
        return false;
    }
    lv_image_set_src(s_faculty_face_image, &s_faculty_face_texture);
    const int32_t w = (int32_t)s_faculty_face_texture.header.w;
    lv_obj_align(s_faculty_face_image, LV_ALIGN_TOP_LEFT, (FACULTY175_LCD_W - w) / 2, y);
    lv_obj_set_style_opa(s_faculty_face_image, opa, 0);
    lv_obj_move_to_index(s_faculty_face_image, 5);
    native_obj_hidden(s_faculty_face_image, false);
    return true;
}

static bool rocket_image_load(void)
{
    if (s_rocket_image_checked) {
        return s_rocket_image_pixels != NULL;
    }
    s_rocket_image_checked = true;
    if (!moon_storage_ready()) {
        return false;
    }
    const char *path = "/bust_cache/rocket/launch_466.rgb565";
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    const size_t expected = (size_t)FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t);
    s_rocket_image_pixels = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_rocket_image_pixels == NULL) {
        s_rocket_image_pixels = heap_caps_malloc(expected, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_rocket_image_pixels == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "rocket image pixel alloc failed");
        return false;
    }
    const size_t got = fread(s_rocket_image_pixels, 1, expected, f);
    fclose(f);
    if (got != expected) {
        heap_caps_free(s_rocket_image_pixels);
        s_rocket_image_pixels = NULL;
        ESP_LOGW(TAG, "rocket image short read %u/%u", (unsigned)got, (unsigned)expected);
        return false;
    }
    s_rocket_image_texture = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = FACULTY175_LCD_W,
            .h = FACULTY175_LCD_H,
            .stride = FACULTY175_LCD_W * sizeof(uint16_t),
            .reserved_2 = 0,
        },
        .data_size = expected,
        .data = (const uint8_t *)s_rocket_image_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    ESP_LOGI(TAG, "rocket image loaded from %s", path);
    return true;
}

static uint16_t a1v_read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t a1v_read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool deathstar_a1v_read_exact(void *dst, size_t len)
{
    return s_deathstar_file != NULL && fread(dst, 1, len, s_deathstar_file) == len;
}

static bool deathstar_a1v_open(void)
{
    if (s_deathstar_checked) {
        return s_deathstar_ready;
    }
    s_deathstar_checked = true;
    if (!moon_storage_ready()) {
        ESP_LOGW(TAG, "deathstar storage unavailable");
        return false;
    }

    const char *path = "/bust_cache/deathstar.a1v";
    s_deathstar_file = fopen(path, "rb");
    if (s_deathstar_file == NULL) {
        ESP_LOGW(TAG, "missing %s", path);
        return false;
    }
    if (fseek(s_deathstar_file, 0, SEEK_END) != 0) {
        return false;
    }
    const long end = ftell(s_deathstar_file);
    if (end <= 32 || fseek(s_deathstar_file, 0, SEEK_SET) != 0) {
        return false;
    }
    s_deathstar_file_size = (uint32_t)end;

    uint8_t hdr[32] = {};
    if (!deathstar_a1v_read_exact(hdr, sizeof(hdr)) || memcmp(hdr, "A1R1", 4) != 0) {
        ESP_LOGW(TAG, "invalid deathstar a1v header");
        return false;
    }
    const uint16_t width = a1v_read_le16(&hdr[4]);
    const uint16_t height = a1v_read_le16(&hdr[6]);
    s_deathstar_fps = a1v_read_le16(&hdr[8]);
    s_deathstar_frame_count = a1v_read_le32(&hdr[12]);
    const uint32_t index_offset = a1v_read_le32(&hdr[16]);
    const uint32_t data_offset = a1v_read_le32(&hdr[20]);
    if (width != FACULTY175_LCD_W || height != FACULTY175_LCD_H || s_deathstar_fps == 0 ||
        s_deathstar_frame_count == 0 || s_deathstar_frame_count > 10000u ||
        index_offset < sizeof(hdr) || data_offset <= index_offset || data_offset > s_deathstar_file_size) {
        ESP_LOGW(TAG,
                 "unsupported deathstar a1v geometry %ux%u fps=%u frames=%u",
                 (unsigned)width,
                 (unsigned)height,
                 (unsigned)s_deathstar_fps,
                 (unsigned)s_deathstar_frame_count);
        return false;
    }

    const size_t pixel_bytes = FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t);
    s_deathstar_pixels = heap_caps_malloc(pixel_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_deathstar_pixels == NULL) {
        s_deathstar_pixels = heap_caps_malloc(pixel_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    s_deathstar_offsets = heap_caps_malloc(sizeof(uint32_t) * s_deathstar_frame_count,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_deathstar_offsets == NULL) {
        s_deathstar_offsets = malloc(sizeof(uint32_t) * s_deathstar_frame_count);
    }
    if (s_deathstar_pixels == NULL || s_deathstar_offsets == NULL) {
        ESP_LOGW(TAG, "deathstar a1v alloc failed");
        return false;
    }
    if (fseek(s_deathstar_file, (long)index_offset, SEEK_SET) != 0) {
        return false;
    }
    for (uint32_t i = 0; i < s_deathstar_frame_count; ++i) {
        uint8_t off[4] = {};
        if (!deathstar_a1v_read_exact(off, sizeof(off))) {
            return false;
        }
        s_deathstar_offsets[i] = a1v_read_le32(off);
        if (s_deathstar_offsets[i] < data_offset || s_deathstar_offsets[i] >= s_deathstar_file_size ||
            (i > 0 && s_deathstar_offsets[i] <= s_deathstar_offsets[i - 1])) {
            ESP_LOGW(TAG, "bad deathstar a1v offset %u", (unsigned)i);
            return false;
        }
    }

    const lv_image_dsc_t texture = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = LV_IMAGE_FLAGS_MODIFIABLE,
            .w = FACULTY175_LCD_W,
            .h = FACULTY175_LCD_H,
            .stride = FACULTY175_LCD_W * sizeof(uint16_t),
            .reserved_2 = 0,
        },
        .data_size = pixel_bytes,
        .data = (const uint8_t *)s_deathstar_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    s_deathstar_textures[0] = texture;
    s_deathstar_textures[1] = texture;
    s_deathstar_ready = true;
    ESP_LOGI(TAG,
             "deathstar a1v ready frames=%u fps=%u size=%u KiB",
             (unsigned)s_deathstar_frame_count,
             (unsigned)s_deathstar_fps,
             (unsigned)(s_deathstar_file_size / 1024u));
    return true;
}

static bool deathstar_a1v_ensure_comp(size_t len)
{
    if (len <= s_deathstar_comp_cap) {
        return true;
    }
    uint8_t *next = heap_caps_realloc(s_deathstar_comp, len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (next == NULL) {
        next = realloc(s_deathstar_comp, len);
    }
    if (next == NULL) {
        return false;
    }
    s_deathstar_comp = next;
    s_deathstar_comp_cap = len;
    return true;
}

static bool deathstar_a1v_read_run(const uint8_t **p, const uint8_t *end, uint32_t *run)
{
    uint32_t value = 0;
    uint32_t shift = 0;
    while (*p < end && shift <= 28u) {
        const uint8_t b = *(*p)++;
        value |= (uint32_t)(b & 0x7fu) << shift;
        if ((b & 0x80u) == 0) {
            *run = value;
            return value > 0;
        }
        shift += 7u;
    }
    return false;
}

static bool deathstar_a1v_decode_frame(uint32_t frame)
{
    if (!deathstar_a1v_open()) {
        return false;
    }
    frame %= s_deathstar_frame_count;
    if (frame == s_deathstar_rendered_frame) {
        return true;
    }
    const uint32_t start = s_deathstar_offsets[frame];
    const uint32_t end = frame + 1u < s_deathstar_frame_count ? s_deathstar_offsets[frame + 1u] : s_deathstar_file_size;
    if (end <= start || !deathstar_a1v_ensure_comp(end - start)) {
        return false;
    }
    if (fseek(s_deathstar_file, (long)start, SEEK_SET) != 0 ||
        fread(s_deathstar_comp, 1, end - start, s_deathstar_file) != end - start) {
        return false;
    }
    s_deathstar_comp_len = end - start;
    if (s_deathstar_comp_len < 2) {
        return false;
    }

    const uint16_t black = faculty175_display_rgb888(0, 0, 0);
    const uint16_t white = faculty175_display_rgb888(245, 248, 244);
    const uint8_t *p = s_deathstar_comp;
    const uint8_t *comp_end = s_deathstar_comp + s_deathstar_comp_len;
    bool color = *p++ != 0;
    uint32_t pos = 0;
    const uint32_t total = FACULTY175_LCD_W * FACULTY175_LCD_H;
    while (p < comp_end && pos < total) {
        uint32_t run = 0;
        if (!deathstar_a1v_read_run(&p, comp_end, &run)) {
            return false;
        }
        const uint16_t px = color ? white : black;
        const uint32_t stop = LV_MIN(total, pos + run);
        while (pos < stop) {
            s_deathstar_pixels[pos++] = px;
            if ((pos & 0x3fffu) == 0u) {
                vTaskDelay(1);
            }
        }
        color = !color;
    }
    if (pos != total) {
        return false;
    }
    s_deathstar_rendered_frame = frame;
    return true;
}

static void create_utility_screen(void)
{
    s_utility_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_utility_screen);
    lv_obj_set_size(s_utility_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_utility_screen, lv_color_hex(0x05070d), 0);
    lv_obj_set_style_bg_opa(s_utility_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_utility_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_utility_title = make_tarot_label(s_utility_screen, 34, 320, 0xe8edf8);
    s_utility_status = make_tarot_label(s_utility_screen, 394, 350, 0x9ca8ba);
    for (int i = 0; i < 14; ++i) {
        s_utility_orbs[i] = make_circle(s_utility_screen, 12, 0xffffff, LV_OPA_COVER);
        native_obj_hidden(s_utility_orbs[i], true);
    }
    for (int i = 0; i < 16; ++i) {
        s_utility_bars[i] = lv_obj_create(s_utility_screen);
        lv_obj_remove_style_all(s_utility_bars[i]);
        lv_obj_clear_flag(s_utility_bars[i], LV_OBJ_FLAG_SCROLLABLE);
        native_obj_hidden(s_utility_bars[i], true);
    }
    for (int i = 0; i < 28; ++i) {
        s_utility_lines[i] = lv_line_create(s_utility_screen);
        configure_solar_line(s_utility_lines[i], 0x9fd8ff, 2, 150);
        native_obj_hidden(s_utility_lines[i], true);
    }
    for (int i = 0; i < 14; ++i) {
        s_utility_labels[i] = make_tarot_label(s_utility_screen, 0, 120, 0xd8e2f0);
        native_obj_hidden(s_utility_labels[i], true);
    }
    s_rocket_image = lv_image_create(s_utility_screen);
    native_obj_hidden(s_rocket_image, true);
    s_utility_qr = lv_qrcode_create(s_utility_screen);
    lv_qrcode_set_size(s_utility_qr, 210);
    lv_qrcode_set_dark_color(s_utility_qr, lv_color_hex(0x111722));
    lv_qrcode_set_light_color(s_utility_qr, lv_color_hex(0xf4f7ff));
    lv_obj_set_style_border_width(s_utility_qr, 0, 0);
    native_obj_hidden(s_utility_qr, true);
}

static void draw_utility_digital(uint32_t anim_ms)
{
    time_t now = astrolabe_time_valid() ? astrolabe_time_now() : (time_t)(anim_ms / 1000u);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char hhmm[16];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    utility_set_label(0, hhmm, 126, 184, 230, 0xf4f7ff);
    lv_obj_set_style_text_font(s_utility_labels[0], LV_FONT_DEFAULT, 0);
    utility_set_label(1, astrolabe_time_valid() ? "LOCAL TIME" : "TIME PENDING", 150, 278, 180, 0x8ecdf0);
    utility_set_orb(0, 233, 238, 260, 0x101927, 180);
    lv_label_set_text(s_utility_status, "SECONDS TICK");
}

static void draw_utility_radarish(faculty175_face_id_t id, uint32_t anim_ms)
{
    const int32_t cx = 233;
    const int32_t cy = 238;
    utility_set_orb(0, cx, cy, id == FACULTY175_FACE_GLOBE ? 230 : 246, 0x0b1420, LV_OPA_COVER);
    for (int i = 0; i < 5; ++i) {
        utility_set_orb(i + 1, cx, cy, 64 + i * 42, 0x000000, 0);
        lv_obj_set_style_border_width(s_utility_orbs[i + 1], 2, 0);
        lv_obj_set_style_border_color(s_utility_orbs[i + 1], lv_color_hex(id == FACULTY175_FACE_RADAR ? 0x62f0a8 : 0x72cfff), 0);
        lv_obj_set_style_border_opa(s_utility_orbs[i + 1], (lv_opa_t)(170 - i * 22), 0);
    }
    const float sweep = -1.5708f + (float)(anim_ms % 4500u) / 4500.0f * 6.28318f;
    utility_set_line(0, cx, cy, cx + (int32_t)lrintf(cosf(sweep) * 132.0f),
                     cy + (int32_t)lrintf(sinf(sweep) * 132.0f), id == FACULTY175_FACE_RADAR ? 0x62f0a8 : 0xffd06a, 5, 220);
    if (id == FACULTY175_FACE_GLOBE) {
        for (int i = 0; i < 5; ++i) {
            utility_set_line(i + 1, 102, 158 + i * 42, 364, 158 + i * 42, 0x72cfff, 2, 100);
        }
        lv_label_set_text(s_utility_status, "WORLD VIEW");
    } else {
        for (int i = 0; i < 5; ++i) {
            const float a = (float)i * 1.256f + (float)anim_ms * 0.0004f;
            utility_set_orb(i + 8, cx + (int32_t)lrintf(cosf(a) * (70.0f + i * 14.0f)),
                            cy + (int32_t)lrintf(sinf(a) * (70.0f + i * 14.0f)), 10, 0x62f0a8, LV_OPA_COVER);
        }
        lv_label_set_text(s_utility_status, "SWEEP SCAN");
    }
}

static void draw_utility_weather(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x071421, LV_OPA_COVER);
    utility_set_orb(1, 178, 152, 148, 0xffc96a, 170);
    utility_set_orb(2, 196, 178, 226, 0x18293a, 210);
    utility_set_orb(3, 262, 192, 196, 0x20364a, 230);
    utility_set_orb(4, 318, 214, 142, 0x2b465c, 220);
    utility_set_orb(5, 154, 238, 128, 0x22384c, 216);
    utility_set_orb(6, 250, 254, 238, 0x314c62, LV_OPA_COVER);
    utility_set_orb(7, 330, 274, 156, 0x38586d, 228);

    for (int i = 0; i < 6; ++i) {
        const int32_t y = 292 + i * 18;
        utility_set_line(i, 76, y, 388, y - 18, 0x86cfff, 2, (lv_opa_t)(54 + i * 13));
    }

    for (int i = 0; i < 8; ++i) {
        const int32_t drift = (int32_t)((anim_ms / 28u + (uint32_t)i * 19u) % 72u);
        const int32_t x = 92 + i * 38;
        utility_set_line(i + 6,
                         x + drift / 3,
                         280 + drift,
                         x - 22 + drift / 3,
                         328 + drift,
                         0x8fd8ff,
                         3,
                         (lv_opa_t)(96 + (i % 3) * 32));
    }

    const float a = -0.4f + sinf((float)anim_ms * 0.0008f) * 0.18f;
    utility_set_line(14, 98, 168, 352, 168 + (int32_t)lrintf(sinf(a) * 34.0f), 0xffe09a, 3, 156);
    utility_set_line(15, 128, 148, 322, 196, 0xfff0b0, 2, 118);
    utility_set_orb(8, 356, 126, 24 + (int32_t)((anim_ms / 160u) % 12u), 0xfff0b0, 176);
}

static void draw_utility_calcifer(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x12070a, LV_OPA_COVER);
    utility_set_orb(1, 233, 284, 270, 0x6d1f16, 210);
    utility_set_orb(2, 233, 250, 220, 0xd84a1b, 230);
    utility_set_orb(3, 232, 220, 158, 0xffb43a, 232);
    utility_set_orb(4, 232, 196, 104, 0xffec8a, 210);

    for (int i = 0; i < 9; ++i) {
        const float phase = (float)anim_ms * 0.0022f + (float)i * 0.74f;
        const int32_t x = 118 + i * 26 + (int32_t)lrintf(sinf(phase) * 10.0f);
        const int32_t y = 326 - (int32_t)lrintf(fabsf(sinf(phase * 0.73f)) * 76.0f);
        const int32_t h = 64 + (int32_t)lrintf((0.5f + 0.5f * sinf(phase)) * 116.0f);
        const uint32_t color = i % 3 == 0 ? 0xffd66a : (i % 3 == 1 ? 0xff6a28 : 0xff2f22);
        utility_set_bar(i, x, y - h, 20, h, color, (lv_opa_t)(170 + (i % 3) * 24), 10);
    }

    for (int i = 0; i < 6; ++i) {
        const float a = -1.1f + (float)i * 0.42f + sinf((float)anim_ms * 0.0011f + (float)i) * 0.12f;
        utility_set_line(i,
                         233,
                         322,
                         233 + (int32_t)lrintf(cosf(a) * 118.0f),
                         322 + (int32_t)lrintf(sinf(a) * 178.0f),
                         i % 2 == 0 ? 0xffe48a : 0xff7738,
                         5,
                         (lv_opa_t)(150 + i * 12));
    }

    utility_set_orb(10, 204, 210, 22, 0x130606, LV_OPA_COVER);
    utility_set_orb(11, 266, 210, 22, 0x130606, LV_OPA_COVER);
    utility_set_orb(12, 204, 206, 9, 0xfff4c0, LV_OPA_COVER);
    utility_set_orb(13, 266, 206, 9, 0xfff4c0, LV_OPA_COVER);
    utility_set_line(12, 212, 264, 252, 264, 0x230a08, 5, 220);
}

static void draw_utility_castalia(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x061019, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 296, 0x0b293a, 220);
    utility_set_orb(2, 233, 238, 218, 0x0e3f55, 190);
    utility_set_orb(3, 233, 238, 126, 0x74e0ff, 92);
    utility_set_orb(4, 233, 238, 46 + (int32_t)((anim_ms / 110u) % 16u), 0xe6fbff, 178);

    for (int i = 0; i < 6; ++i) {
        const int32_t size = 92 + i * 48 + (int32_t)((anim_ms / 90u + (uint32_t)i * 7u) % 24u);
        utility_set_orb(i + 5, 233, 238, size, 0x000000, 0);
        lv_obj_set_style_border_width(s_utility_orbs[i + 5], i < 2 ? 3 : 2, 0);
        lv_obj_set_style_border_color(s_utility_orbs[i + 5], lv_color_hex(i % 2 == 0 ? 0x89ecff : 0x7bc6ff), 0);
        lv_obj_set_style_border_opa(s_utility_orbs[i + 5], (lv_opa_t)(170 - i * 20), 0);
    }

    for (int i = 0; i < 10; ++i) {
        const float t = (float)i / 9.0f;
        const float wave = sinf((float)anim_ms * 0.0012f + (float)i * 0.8f);
        const int32_t x0 = 76 + (int32_t)lrintf(t * 312.0f);
        const int32_t y0 = 334 - (int32_t)lrintf(sinf(t * 3.14159f) * 92.0f) + (int32_t)lrintf(wave * 12.0f);
        const int32_t x1 = 100 + (int32_t)lrintf(t * 264.0f);
        const int32_t y1 = 150 + (int32_t)lrintf(cosf(t * 3.14159f) * 36.0f) - (int32_t)lrintf(wave * 9.0f);
        utility_set_line(i, x0, y0, x1, y1, i % 2 == 0 ? 0x98f0ff : 0xc8fbff, i < 4 ? 3 : 2, (lv_opa_t)(84 + i * 12));
    }

    for (int i = 0; i < 6; ++i) {
        const float a = -1.5708f + (float)i * 1.0472f + (float)anim_ms * 0.00028f;
        utility_set_orb(11 + (i % 3),
                        233 + (int32_t)lrintf(cosf(a) * 118.0f),
                        238 + (int32_t)lrintf(sinf(a) * 118.0f),
                        12 + (i % 2) * 4,
                        i % 2 == 0 ? 0xe6fbff : 0x82dcff,
                        210);
    }
}

static void draw_utility_babel(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x060914, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 286, 0x111a2a, LV_OPA_COVER);
    utility_set_orb(2, 233, 238, 168, 0x24304a, 212);
    utility_set_orb(3, 233, 238, 78, 0x92e8ff, 86);

    for (int i = 0; i < 8; ++i) {
        const int32_t y = 104 + i * 34;
        const int32_t left_wave = (int32_t)lrintf(sinf((float)anim_ms * 0.0016f + (float)i * 0.7f) * 24.0f);
        const int32_t right_wave = (int32_t)lrintf(cosf((float)anim_ms * 0.0013f + (float)i * 0.8f) * 24.0f);
        utility_set_line(i,
                         84 + left_wave,
                         y,
                         222,
                         238 + (i - 4) * 8,
                         i % 2 == 0 ? 0x8fd8ff : 0xdec7ff,
                         3,
                         (lv_opa_t)(116 + i * 11));
        utility_set_line(i + 8,
                         382 + right_wave,
                         y,
                         244,
                         238 - (i - 4) * 8,
                         i % 2 == 0 ? 0xffd36a : 0xff8fca,
                         3,
                         (lv_opa_t)(116 + i * 11));
    }

    static const int16_t marks[10][2] = {
        {132, 124}, {178, 164}, {120, 224}, {174, 286}, {132, 342},
        {334, 126}, {288, 168}, {356, 224}, {292, 288}, {336, 342},
    };
    for (int i = 0; i < 10; ++i) {
        const int32_t bob = (int32_t)lrintf(sinf((float)anim_ms * 0.002f + (float)i) * 8.0f);
        const uint32_t color = i < 5 ? 0x94e8ff : 0xffcf7a;
        utility_set_orb(4 + i, marks[i][0], marks[i][1] + bob, i % 3 == 0 ? 20 : 14, color, 206);
    }

    utility_set_bar(0, 216, 102, 8, 276, 0x405078, 156, 4);
    utility_set_bar(1, 242, 102, 8, 276, 0x705050, 148, 4);
    utility_set_orb(12, 233, 238, 46 + (int32_t)((anim_ms / 120u) % 12u), 0xf8f0c8, 192);
    utility_set_orb(13, 233, 238, 18, 0x071018, LV_OPA_COVER);
}

static void draw_utility_notes(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x0b0d12, LV_OPA_COVER);
    utility_set_orb(1, 232, 238, 292, 0x2b2118, 154);

    for (int i = 0; i < 4; ++i) {
        const int32_t x = 108 + i * 14;
        const int32_t y = 86 + i * 16;
        const int32_t bob = (int32_t)lrintf(sinf((float)anim_ms * 0.0011f + (float)i) * 3.0f);
        utility_set_bar(i, x, y + bob, 234, 302 - i * 18, i == 3 ? 0xf3e8d4 : 0xd8c7a9, (lv_opa_t)(116 + i * 38), 6);
    }

    utility_set_bar(4, 154, 126, 186, 6, 0x7e5f3c, 190, 3);
    utility_set_bar(5, 154, 356, 156, 5, 0x7e5f3c, 132, 3);
    for (int i = 0; i < 9; ++i) {
        const int32_t y = 164 + i * 23;
        const int32_t wave = (int32_t)lrintf(sinf((float)anim_ms * 0.0015f + (float)i * 0.7f) * 7.0f);
        utility_set_line(i,
                         152,
                         y,
                         318 + wave,
                         y + (i % 2 == 0 ? 1 : -1),
                         i % 3 == 0 ? 0x9a7450 : 0x6d5948,
                         2,
                         (lv_opa_t)(92 + i * 10));
    }

    for (int i = 0; i < 6; ++i) {
        const int32_t x = 174 + i * 28;
        const int32_t y = 176 + (i % 3) * 48;
        utility_set_orb(2 + i, x, y, i % 2 == 0 ? 7 : 5, 0x4a3828, (lv_opa_t)(120 + i * 16));
    }

    utility_set_line(12, 306, 118, 342, 88, 0xffc96a, 4, 180);
    utility_set_orb(10, 348, 84, 14, 0xffc96a, 210);
    utility_set_orb(11, 318, 112, 10, 0xfff0b0, 180);
}

static void quote_label_sanitize(const char *in, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (in == NULL) {
        return;
    }
    size_t wr = 0;
    for (size_t rd = 0; in[rd] != '\0' && wr + 1 < cap;) {
        const unsigned char ch = (unsigned char)in[rd];
        if (ch < 0x80) {
            out[wr++] = (char)ch;
            ++rd;
            continue;
        }
        if ((unsigned char)in[rd] == 0xe2 && (unsigned char)in[rd + 1] == 0x80) {
            const unsigned char mark = (unsigned char)in[rd + 2];
            if (mark == 0x93 || mark == 0x94) {
                out[wr++] = '-';
                rd += 3;
                continue;
            }
            if (mark == 0x98 || mark == 0x99) {
                out[wr++] = '\'';
                rd += 3;
                continue;
            }
            if (mark == 0x9c || mark == 0x9d) {
                out[wr++] = '"';
                rd += 3;
                continue;
            }
        }
        ++rd;
    }
    out[wr] = '\0';
}

static void draw_utility_quotes(uint32_t anim_ms)
{
    (void)anim_ms;
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    faculty175_quote_t q = {};
    bool ok = faculty175_quotes_current(&q) && q.ok;
    if (!ok) {
        ok = true;
        q.ok = true;
        q.demo = true;
        snprintf(q.faculty_slug, sizeof(q.faculty_slug), "a.plato");
        snprintf(q.faculty_name, sizeof(q.faculty_name), "Plato");
        snprintf(q.quote, sizeof(q.quote), "The beginning is the most important part of the work.");
        snprintf(q.passage, sizeof(q.passage), "The Republic, Book II");
        snprintf(q.book_title, sizeof(q.book_title), "The Republic");
    }

    lv_obj_set_style_bg_color(s_utility_screen, lv_color_hex(0x060812), 0);
    utility_set_bar(0, 0, 316, FACULTY175_LCD_W, 150, 0x0d1019, LV_OPA_COVER, 0);
    utility_set_bar(1, 0, 314, FACULTY175_LCD_W, 2, 0xb99a62, 170, 0);
    const bool bust_drawn = ok && utility_show_faculty_bust_for_slug(q.faculty_slug, 18, 218);

    char author[64];
    quote_label_sanitize(q.faculty_name[0] != '\0' ? q.faculty_name : q.faculty_slug, author, sizeof(author));
    char initials[4] = "?";
    if (author[0] != '\0') {
        initials[0] = author[0];
        initials[1] = '\0';
        const char *space = strrchr(author, ' ');
        if (space != NULL && space[1] != '\0') {
            initials[1] = space[1];
            initials[2] = '\0';
        }
    }
    if (bust_drawn) {
        native_obj_hidden(s_utility_labels[0], true);
    } else {
        utility_set_label(0, initials, 208, 150, 72, 0xf1dfb8);
    }
    utility_set_label(1, author, 88, bust_drawn ? 278 : 264, 300, 0xf1e6cc);
    lv_obj_set_style_text_align(s_utility_labels[1], LV_TEXT_ALIGN_CENTER, 0);

    char quote_text[sizeof(q.quote)];
    quote_label_sanitize(q.quote, quote_text, sizeof(quote_text));
    const char *p = quote_text;
    for (int line = 0; line < 5; ++line) {
        while (*p == ' ') {
            ++p;
        }
        char buf[54] = {};
        size_t n = 0;
        size_t last_space = 0;
        while (p[n] != '\0' && n < 36) {
            if (p[n] == ' ') {
                last_space = n;
            }
            ++n;
        }
        if (p[n] != '\0' && last_space > 0) {
            n = last_space;
        }
        if (n >= sizeof(buf)) {
            n = sizeof(buf) - 1;
        }
        memcpy(buf, p, n);
        buf[n] = '\0';
        if (p[n] != '\0' && line == 4 && n > 3) {
            buf[n - 3] = '.';
            buf[n - 2] = '.';
            buf[n - 1] = '.';
        }
        utility_set_label(2 + line, buf, 44, 332 + line * 21, 390, 0xeee5d8);
        lv_obj_set_style_text_align(s_utility_labels[2 + line], LV_TEXT_ALIGN_CENTER, 0);
        p += n;
        if (*p == '\0') {
            break;
        }
    }

    char book_title[80] = {};
    char passage[80] = {};
    char source[120] = {};
    quote_label_sanitize(q.book_title, book_title, sizeof(book_title));
    quote_label_sanitize(q.passage, passage, sizeof(passage));
    snprintf(source, sizeof(source), "%s%s%s", book_title, passage[0] != '\0' ? " / " : "", passage);
    if (source[0] != '\0') {
        utility_set_label(7, source, 54, 438, 370, 0x9ca8ba);
        lv_obj_set_style_text_align(s_utility_labels[7], LV_TEXT_ALIGN_CENTER, 0);
    }
}

static void draw_utility_qday(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x071018, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 286, 0x122838, 224);
    utility_set_orb(2, 233, 238, 174, 0x24566a, 170);
    utility_set_orb(3, 233, 238, 88 + (int32_t)((anim_ms / 100u) % 20u), 0xffcf66, 84);

    for (int i = 0; i < 8; ++i) {
        const float a = -1.5708f + (float)i * 0.785398f + sinf((float)anim_ms * 0.0007f) * 0.16f;
        const int32_t r = 116 + (i % 2) * 28;
        utility_set_line(i,
                         233 + (int32_t)lrintf(cosf(a) * 34.0f),
                         238 + (int32_t)lrintf(sinf(a) * 34.0f),
                         233 + (int32_t)lrintf(cosf(a) * (float)r),
                         238 + (int32_t)lrintf(sinf(a) * (float)r),
                         i % 2 == 0 ? 0x86e4ff : 0xffcf66,
                         3,
                         (lv_opa_t)(116 + i * 10));
    }

    utility_set_label(0, "?", 206, 152, 88, 0xffe8a4);
    lv_obj_set_style_text_opa(s_utility_labels[0], LV_OPA_COVER, 0);
    utility_set_orb(4, 233, 296, 22, 0xffe8a4, LV_OPA_COVER);
    utility_set_orb(5, 233, 238, 38, 0x071018, LV_OPA_COVER);
    utility_set_orb(6, 233, 238, 14, 0xffcf66, 210);

    for (int i = 0; i < 5; ++i) {
        const float a = (float)i * 1.2566f + (float)anim_ms * 0.00045f;
        utility_set_orb(7 + i,
                        233 + (int32_t)lrintf(cosf(a) * 156.0f),
                        238 + (int32_t)lrintf(sinf(a) * 156.0f),
                        10,
                        0x86e4ff,
                        170);
    }
}

static void draw_utility_spotify(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x06120b, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 318, 0x0c2a17, LV_OPA_COVER);
    utility_set_orb(2, 233, 238, 226, 0x1ed760, 86);
    utility_set_orb(3, 154, 204, 112, 0x1ed760, LV_OPA_COVER);
    utility_set_orb(4, 154, 204, 84, 0x06120b, LV_OPA_COVER);

    for (int i = 0; i < 3; ++i) {
        const int32_t y = 182 + i * 24;
        const int32_t lift = (int32_t)lrintf(sinf((float)anim_ms * 0.0014f + (float)i * 0.8f) * 5.0f);
        utility_set_line(i, 116, y + lift, 204, y + 18 + lift, 0x1ed760, 5 - i, 220);
    }

    for (int i = 0; i < 10; ++i) {
        const float phase = (float)anim_ms * 0.004f + (float)i * 0.58f;
        const int32_t h = 24 + (int32_t)lrintf(fabsf(sinf(phase)) * 104.0f);
        const uint32_t color = i % 3 == 0 ? 0x1ed760 : (i % 3 == 1 ? 0x74f7a6 : 0x98f0ff);
        utility_set_bar(i, 236 + i * 14, 292 - h, 8, h, color, (lv_opa_t)(150 + (i % 4) * 24), 4);
    }

    utility_set_orb(11, 284, 320, 12, 0xf4fff8, 154);
}

static void draw_utility_rocket(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    faculty175_rocket_status_t st = {};
    const bool ok = faculty175_rocket_current(&st) && st.count > 0;
    const faculty175_rocket_launch_t *next = ok ? &st.launches[0] : NULL;
    const int32_t cx = 233;
    const int32_t cy = 204;
    const int32_t r_outer = 170;
    const int32_t r_inner = 154;
    const int32_t r_mark = 142;

    utility_set_orb(0, cx, cy, 466, 0x060914, LV_OPA_COVER);
    const bool image_ok = rocket_image_load();
    if (image_ok && s_rocket_image != NULL) {
        lv_image_set_src(s_rocket_image, &s_rocket_image_texture);
        lv_obj_align(s_rocket_image, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_opa(s_rocket_image, 92, 0);
        native_obj_hidden(s_rocket_image, false);
    }

    utility_set_orb(1, cx, cy, r_outer * 2, 0x000000, 0);
    lv_obj_set_style_border_width(s_utility_orbs[1], 2, 0);
    lv_obj_set_style_border_color(s_utility_orbs[1], lv_color_hex(0x78aee2), 0);
    lv_obj_set_style_border_opa(s_utility_orbs[1], 180, 0);
    utility_set_orb(2, cx, cy, r_inner * 2, 0x000000, 0);
    lv_obj_set_style_border_width(s_utility_orbs[2], 2, 0);
    lv_obj_set_style_border_color(s_utility_orbs[2], lv_color_hex(0x304468), 0);
    lv_obj_set_style_border_opa(s_utility_orbs[2], 170, 0);
    utility_set_orb(3, cx, cy, 164, 0x091121, 228);
    utility_set_orb(4, cx, cy, 126, 0x101b31, 218);

    for (int h = -12, idx = 0; h <= 12 && idx < 9; h += 3) {
        if (h == 0) {
            continue;
        }
        const float a = -1.5708f + ((float)h / 12.0f) * 3.14159f;
        utility_set_line(idx++,
                         cx + (int32_t)lrintf(cosf(a) * (float)r_inner),
                         cy + (int32_t)lrintf(sinf(a) * (float)r_inner),
                         cx + (int32_t)lrintf(cosf(a) * (float)r_outer),
                         cy + (int32_t)lrintf(sinf(a) * (float)r_outer),
                         h < 0 ? 0x78aee2 : 0xff9a5f,
                         h % 6 == 0 ? 4 : 2,
                         166);
    }
    utility_set_line(9, cx, cy - r_outer - 4, cx, cy - r_inner + 4, 0xffd36a, 5, LV_OPA_COVER);

    if (next != NULL) {
        const int64_t now = astrolabe_time_valid() ? (int64_t)astrolabe_time_now() : (int64_t)time(NULL);
        const float half = 12.0f * 3600.0f;
        float rel = (float)(now - next->net_unix) / half;
        if (rel < -1.0f) {
            rel = -1.0f;
        } else if (rel > 1.0f) {
            rel = 1.0f;
        }
        const float a = (rel <= -0.999f || rel >= 0.999f) ? -1.5708f : -1.5708f + rel * 3.14159f;
        utility_set_orb(5,
                        cx + (int32_t)lrintf(cosf(a) * (float)r_mark),
                        cy + (int32_t)lrintf(sinf(a) * (float)r_mark),
                        18,
                        0xf8f0d0,
                        LV_OPA_COVER);
        char countdown[32];
        char local[32];
        faculty175_rocket_format_countdown(next->net_unix, countdown, sizeof(countdown));
        faculty175_rocket_format_local(next->net_unix, local, sizeof(local));
        utility_set_label(0, countdown, 118, 182, 240, 0xf4f7ff);
        utility_set_label(1, next->vehicle[0] != '\0' ? next->vehicle : "LAUNCH", 118, 212, 240, 0xffd36a);
        utility_set_label(2, next->name, 72, 244, 320, 0xdce8ff);
        utility_set_label(3, local, 142, 274, 188, 0x9fb8d4);
        utility_set_label(4, next->pad, 68, 318, 330, 0xf4f7ff);
        utility_set_label(5, next->location, 62, 342, 342, 0x9fb8d4);
        if (next->weather[0] != '\0') {
            char weather[72];
            snprintf(weather, sizeof(weather), "%s", next->weather);
            char *nl = strchr(weather, '\n');
            if (nl != NULL) {
                *nl = ' ';
            }
            utility_set_label(6, weather, 52, 394, 366, 0x9bcf9a);
        }
        if (st.count > 1) {
            char when[24];
            faculty175_rocket_format_local(st.launches[1].net_unix, when, sizeof(when));
            char row[96];
            snprintf(row,
                     sizeof(row),
                     "next %s  %s",
                     when,
                     st.launches[1].vehicle[0] ? st.launches[1].vehicle : st.launches[1].name);
            utility_set_label(7, row, 50, 430, 366, 0x8090a8);
        }
    } else {
        utility_set_label(0, "LAUNCH CLOCK", 108, 190, 260, 0xffd36a);
        utility_set_label(1, faculty175_rocket_state_name(), 140, 224, 190, 0xdce8ff);
        utility_set_label(2, faculty175_rocket_last(), 76, 254, 320, 0x9fb8d4);
        utility_set_label(3, "rocket fetch", 138, 326, 200, 0x78aee2);
    }
}

static void draw_utility_focus(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x080a0e, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 300, 0x151a22, LV_OPA_COVER);
    utility_set_orb(2, 233, 238, 214, 0x000000, 0);
    lv_obj_set_style_border_width(s_utility_orbs[2], 10, 0);
    lv_obj_set_style_border_color(s_utility_orbs[2], lv_color_hex(0xffcf66), 0);
    lv_obj_set_style_border_opa(s_utility_orbs[2], 214, 0);
    utility_set_orb(3, 233, 238, 156, 0x101720, LV_OPA_COVER);

    const float progress = (float)(anim_ms % 25000u) / 25000.0f;
    const int active = (int)lrintf(progress * 24.0f);
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5708f + (float)i * 0.523599f;
        const bool lit = i * 2 <= active;
        utility_set_line(i,
                         233 + (int32_t)lrintf(cosf(a) * 88.0f),
                         238 + (int32_t)lrintf(sinf(a) * 88.0f),
                         233 + (int32_t)lrintf(cosf(a) * 132.0f),
                         238 + (int32_t)lrintf(sinf(a) * 132.0f),
                         lit ? 0xffcf66 : 0x4b5560,
                         lit ? 5 : 3,
                         lit ? 222 : 110);
    }

    const float hand = -1.5708f + progress * 6.28318f;
    utility_set_line(14, 233, 238,
                     233 + (int32_t)lrintf(cosf(hand) * 96.0f),
                     238 + (int32_t)lrintf(sinf(hand) * 96.0f),
                     0xf4f7ff, 4, 210);
    utility_set_orb(10, 233, 238, 28, 0xffcf66, LV_OPA_COVER);
    utility_set_orb(11, 233, 238, 10, 0x080a0e, LV_OPA_COVER);

    for (int i = 0; i < 4; ++i) {
        const float a = (float)i * 1.5708f + (float)anim_ms * 0.00018f;
        utility_set_orb(4 + i,
                        233 + (int32_t)lrintf(cosf(a) * 174.0f),
                        238 + (int32_t)lrintf(sinf(a) * 174.0f),
                        10,
                        0xffcf66,
                        128);
    }
}

static void draw_utility_biometrics(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x12070c, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 304, 0x2a0f18, 220);
    utility_set_orb(2, 233, 238, 182, 0xff6a86, 74);

    for (int i = 0; i < 16; ++i) {
        const int32_t x0 = 62 + i * 27;
        const float beat = sinf((float)anim_ms * 0.0065f + (float)i * 0.72f);
        const int32_t y0 = 238 + (int32_t)lrintf(beat * 36.0f);
        const int32_t y1 = 238 + (int32_t)lrintf(sinf((float)anim_ms * 0.0065f + (float)(i + 1) * 0.72f) * 36.0f);
        utility_set_line(i, x0, y0, x0 + 28, y1, i % 3 == 0 ? 0xffcf66 : 0xff6a86, i % 4 == 0 ? 5 : 3, (lv_opa_t)(144 + (i % 5) * 18));
    }

    for (int i = 0; i < 6; ++i) {
        const float a = (float)i * 1.0472f + (float)anim_ms * 0.00032f;
        utility_set_orb(3 + i,
                        233 + (int32_t)lrintf(cosf(a) * 116.0f),
                        238 + (int32_t)lrintf(sinf(a) * 116.0f),
                        14 + (i % 2) * 6,
                        i % 2 == 0 ? 0xff6a86 : 0xffcf66,
                        190);
    }

    utility_set_orb(10, 233, 238, 44 + (int32_t)lrintf(fabsf(sinf((float)anim_ms * 0.003f)) * 18.0f), 0xff6a86, 178);
    utility_set_orb(11, 233, 238, 18, 0x12070c, LV_OPA_COVER);
}

static void draw_utility_hid(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);

    utility_set_orb(0, 233, 238, 466, 0x050911, LV_OPA_COVER);
    utility_set_bar(0, 116, 118, 234, 282, 0x101720, LV_OPA_COVER, 22);
    utility_set_bar(1, 128, 130, 210, 258, 0x172536, LV_OPA_COVER, 18);
    utility_set_bar(2, 146, 152, 174, 216, 0x071018, 180, 14);

    for (int i = 0; i < 5; ++i) {
        const int32_t x = 154 + i * 34;
        utility_set_line(i, x, 150, x, 368, 0x4a6682, 2, 104);
        utility_set_line(i + 5, 146, 164 + i * 42, 322, 164 + i * 42, 0x4a6682, 2, 104);
    }

    const int32_t x = 178 + (int32_t)((anim_ms / 24u) % 122u);
    const int32_t y = 194 + (int32_t)lrintf(sinf((float)anim_ms * 0.002f) * 78.0f);
    utility_set_orb(3, x, y, 40, 0x92e8ff, 82);
    utility_set_orb(4, x, y, 22, 0x92e8ff, 210);
    utility_set_orb(5, x, y, 8, 0xf4fbff, LV_OPA_COVER);

    for (int i = 0; i < 4; ++i) {
        const int32_t size = 58 + i * 34 + (int32_t)((anim_ms / 90u + (uint32_t)i * 7u) % 16u);
        utility_set_orb(6 + i, x, y, size, 0x000000, 0);
        lv_obj_set_style_border_width(s_utility_orbs[6 + i], 2, 0);
        lv_obj_set_style_border_color(s_utility_orbs[6 + i], lv_color_hex(0x92e8ff), 0);
        lv_obj_set_style_border_opa(s_utility_orbs[6 + i], (lv_opa_t)(132 - i * 20), 0);
    }
}

static void draw_utility_settings(uint32_t anim_ms)
{
    (void)anim_ms;
    native_obj_hidden(s_utility_title, false);
    native_obj_hidden(s_utility_status, false);

    const bool ap = faculty175_wifi_settings_ap_active();
    const bool client = faculty175_wifi_settings_ap_client_connected();
    const bool router = faculty175_wifi_settings_travel_router_enabled();
    const bool page_phase = !ap || client;
    const char *qr = page_phase ? faculty175_wifi_settings_page_qr_payload()
                                : faculty175_wifi_settings_ap_qr_payload();
    const char *ssid = faculty175_wifi_settings_ssid();
    const char *upstream = faculty175_wifi_settings_upstream_ssid();
    const char *url = faculty175_wifi_settings_url();

    utility_set_orb(0, 233, 238, 466, 0x080b10, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 326, 0xf4f7ff, 18);
    utility_set_orb(2, 233, 238, 252, 0x101720, LV_OPA_COVER);

    lv_label_set_text(s_utility_title, page_phase ? "OPEN SETTINGS" : "JOIN SETUP AP");
    lv_obj_set_width(s_utility_title, 300);
    lv_obj_align(s_utility_title, LV_ALIGN_TOP_MID, 0, 34);

    if (s_utility_qr != NULL && qr != NULL && qr[0] != '\0') {
        lv_qrcode_update(s_utility_qr, qr, strlen(qr));
        lv_obj_align(s_utility_qr, LV_ALIGN_TOP_MID, 0, 92);
        native_obj_hidden(s_utility_qr, false);
    }

    if (!page_phase) {
        utility_set_label(0, ssid[0] != '\0' ? ssid : "ASTROLABE AP", 78, 318, 310, 0xe8edf8);
        utility_set_label(1, router ? "travel router setup" : "scan again after join", 82, 352, 300, 0x94d7ff);
    } else {
        utility_set_label(0, url[0] != '\0' ? url : "http://192.168.4.1/wifi", 42, 318, 390, 0xe8edf8);
        utility_set_label(1,
                          router && upstream[0] != '\0' ? upstream
                                                         : (ap ? "setup page"
                                                               : (ssid[0] != '\0' ? ssid : "wifi settings")),
                          82,
                          352,
                          300,
                          0x94d7ff);
    }
    lv_obj_set_style_text_align(s_utility_labels[0], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(s_utility_labels[1], LV_TEXT_ALIGN_CENTER, 0);

    lv_label_set_text(s_utility_status, faculty175_wifi_settings_status());
    lv_obj_set_width(s_utility_status, 320);
    lv_obj_align(s_utility_status, LV_ALIGN_TOP_MID, 0, 392);
}

static void draw_utility_watcher(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, false);
    native_obj_hidden(s_utility_status, false);

    const size_t total = faculty175_wifi_monitor_count();
    faculty175_wifi_incident_t latest = {};
    const bool have = faculty175_wifi_monitor_get_newest(0, &latest);

    utility_set_orb(0, 233, 238, 466, 0x071018, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 308, 0x112030, LV_OPA_COVER);
    utility_set_orb(2, 184, 224, 96, 0xf4f7ff, LV_OPA_COVER);
    utility_set_orb(3, 282, 224, 96, 0xf4f7ff, LV_OPA_COVER);

    const int32_t glance = (int32_t)lrintf(sinf((float)anim_ms * 0.0011f) * 18.0f);
    utility_set_orb(4, 184 + glance, 224, 34, 0x101720, LV_OPA_COVER);
    utility_set_orb(5, 282 + glance, 224, 34, 0x101720, LV_OPA_COVER);
    utility_set_orb(6, 196 + glance, 212, 9, 0x92e8ff, LV_OPA_COVER);
    utility_set_orb(7, 294 + glance, 212, 9, 0x92e8ff, LV_OPA_COVER);

    utility_set_bar(0, 162, 304, 144, 12, 0x405468, 170, 6);
    utility_set_bar(1, 178, 330, 112, 8, 0x405468, 120, 4);
    for (int i = 0; i < 7; ++i) {
        const float a = -0.95f + (float)i * 0.316f + sinf((float)anim_ms * 0.001f) * 0.08f;
        utility_set_line(i,
                         233,
                         302,
                         233 + (int32_t)lrintf(cosf(a) * 158.0f),
                         302 + (int32_t)lrintf(sinf(a) * 116.0f),
                         i % 2 == 0 ? 0x92e8ff : 0xffcf66,
                         2,
                         (lv_opa_t)(78 + i * 14));
    }

    for (int i = 0; i < 4; ++i) {
        const int32_t y = 102 + i * 38;
        utility_set_line(8 + i, 96, y, 366, y + (i % 2 ? 12 : -12), 0x405468, 2, 92);
    }

    char line0[40];
    char line1[40];
    snprintf(line0, sizeof(line0), "events=%u", (unsigned)total);
    if (have) {
        snprintf(line1, sizeof(line1), "last=%s", latest.type);
    } else {
        snprintf(line1, sizeof(line1), "monitoring lab + wifi");
    }
    lv_label_set_text(s_utility_title, "WATCHER");
    utility_set_label(0, line0, 72, 118, 330, 0xf4f7ff);
    utility_set_label(1, have ? latest.type : "idle", 72, 168, 330, 0x92e8ff);
    utility_set_label(2, have ? latest.detail : "incidents face for log", 48, 214, 380, 0x94a3b8);
    lv_label_set_text(s_utility_status, line1);
    lv_obj_set_width(s_utility_status, 360);
    lv_obj_align(s_utility_status, LV_ALIGN_TOP_MID, 0, 392);
}

static void draw_utility_incidents(uint32_t anim_ms)
{
    (void)anim_ms;
    native_obj_hidden(s_utility_title, false);
    native_obj_hidden(s_utility_status, false);

    const size_t total = faculty175_wifi_monitor_count();
    size_t scroll = faculty175_face_incidents_scroll_index();
    if (scroll >= total && total > 0) {
        scroll = total - 1u;
    }
    faculty175_wifi_incident_t event = {};
    const bool have = total > 0 && faculty175_wifi_monitor_get_newest(scroll, &event);

    char line0[40];
    char line1[40];
    char line2[40];
    if (have) {
        snprintf(line0, sizeof(line0), "%s", event.type);
        snprintf(line1, sizeof(line1), "%s", event.ssid[0] != '\0' ? event.ssid : event.detail);
        snprintf(line2,
                 sizeof(line2),
                 "#%lu %u/%u swipe tap=clear",
                 (unsigned long)event.seq,
                 (unsigned)(scroll + 1u),
                 (unsigned)total);
    } else {
        snprintf(line0, sizeof(line0), "NO INCIDENTS");
        snprintf(line1, sizeof(line1), "SecOps events appear here");
        line2[0] = '\0';
    }

    utility_set_orb(0, 233, 238, 246, 0x0b1420, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 188, 0x112030, 220);
    utility_set_label(0, line0, 72, 118, 330, 0xf4f7ff);
    utility_set_label(1, line1, 72, 168, 330, 0xffa657);
    utility_set_label(2, have ? line2 : "lab + wifi events", 48, 214, 380, 0x94a3b8);
    lv_label_set_text(s_utility_title, "INCIDENTS");
    lv_label_set_text(s_utility_status, have ? "SECOPS LOG" : "EMPTY");
    lv_obj_set_width(s_utility_status, 360);
    lv_obj_align(s_utility_status, LV_ALIGN_TOP_MID, 0, 392);
}

static void draw_utility_deathstar(uint32_t anim_ms)
{
    native_obj_hidden(s_utility_title, true);
    native_obj_hidden(s_utility_status, true);
    lv_obj_set_style_bg_color(s_utility_screen, lv_color_hex(0x000000), 0);

    const uint32_t frame = s_deathstar_fps > 0 && s_deathstar_frame_count > 0
                               ? (uint32_t)(((uint64_t)anim_ms * (uint64_t)s_deathstar_fps / 1000u) %
                                            (uint64_t)s_deathstar_frame_count)
                               : 0u;
    if (deathstar_a1v_decode_frame(frame)) {
        if (s_deathstar_image == NULL) {
            s_deathstar_image = lv_image_create(s_utility_screen);
            lv_obj_move_to_index(s_deathstar_image, 1);
        }
        if (s_deathstar_image != NULL) {
            s_deathstar_texture_slot ^= 1u;
            s_deathstar_textures[s_deathstar_texture_slot].header.reserved_2 = (uint16_t)frame;
            lv_image_set_src(s_deathstar_image, &s_deathstar_textures[s_deathstar_texture_slot]);
            lv_obj_align(s_deathstar_image, LV_ALIGN_TOP_LEFT, 0, 0);
            lv_obj_set_style_opa(s_deathstar_image, LV_OPA_COVER, 0);
            lv_obj_move_foreground(s_deathstar_image);
            native_obj_hidden(s_deathstar_image, false);
            lv_obj_invalidate(s_deathstar_image);
            return;
        }
    }

    utility_set_label(0, "DEATH STAR", 112, 202, 260, 0xf5f8f4);
    utility_set_label(1, "MISSING /bust_cache/deathstar.a1v", 48, 238, 380, 0x7d8588);
}

static void draw_utility_apocalypso(uint32_t anim_ms)
{
    static uint32_t s_last_apoc_request_ms;
    static const char *const k_lab[12] = {
        "Bibl", "Nuke", "Bio", "AI", "Cyber", "Infra", "Mkt", "State", "Epis", "Clim", "BioS", "Solar",
    };
    static const uint32_t k_col[12] = {
        0xd4a853, 0xf07178, 0x7ee787, 0x79c0ff, 0xd2a8ff, 0xffa657,
        0xe3b341, 0xff7b72, 0xbc8cff, 0x56d4dd, 0x3fb950, 0xf0c674,
    };

    faculty175_apocalypso_status_t apoc = {};
    if (!faculty175_apocalypso_current(&apoc)) {
        apoc.ok = true;
        apoc.demo = true;
        static const float fallback[12] = {
            0.38f, 0.24f, 0.12f, 0.41f, 0.28f, 0.19f, 0.35f, 0.31f, 0.44f, 0.272f, 0.52f, 0.08f,
        };
        memcpy(apoc.value, fallback, sizeof(fallback));
        snprintf(apoc.updated_at, sizeof(apoc.updated_at), "fallback");
    }
    if (apoc.demo && (s_last_apoc_request_ms == 0 || anim_ms - s_last_apoc_request_ms > 60000u)) {
        s_last_apoc_request_ms = anim_ms;
        faculty175_apocalypso_request_refresh();
    }

    const int32_t cx = 233;
    const int32_t cy = 238;
    const int32_t rmax = 154;
    utility_set_orb(0, cx, cy, 466, 0x071018, LV_OPA_COVER);
    for (int ring = 1; ring <= 5; ++ring) {
        const int32_t rr = (rmax * ring) / 5;
        utility_set_orb(ring, cx, cy, rr * 2, 0x000000, 0);
        lv_obj_set_style_border_width(s_utility_orbs[ring], ring == 5 ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_utility_orbs[ring], lv_color_hex(0x374152), 0);
        lv_obj_set_style_border_opa(s_utility_orbs[ring], (lv_opa_t)(116 + ring * 14), 0);
    }

    int32_t vx[12] = {};
    int32_t vy[12] = {};
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        const int32_t sx = cx + (int32_t)lrintf(cosf(a) * (float)rmax);
        const int32_t sy = cy + (int32_t)lrintf(sinf(a) * (float)rmax);
        utility_set_line(i, cx, cy, sx, sy, 0x485466, 1, 150);

        float value = apoc.value[i];
        if (value < 0.0f) {
            value = 0.0f;
        } else if (value > 1.0f) {
            value = 1.0f;
        }
        const int32_t ri = (int32_t)lrintf((float)rmax * value);
        vx[i] = cx + (int32_t)lrintf(cosf(a) * (float)ri);
        vy[i] = cy + (int32_t)lrintf(sinf(a) * (float)ri);
    }

    for (int i = 0; i < 12; ++i) {
        const int j = (i + 1) % 12;
        utility_set_line(12 + i, vx[i], vy[i], vx[j], vy[j], 0xe8edf8, 3, 235);
    }

    int peak = 0;
    for (int i = 1; i < 12; ++i) {
        if (apoc.value[i] > apoc.value[peak]) {
            peak = i;
        }
    }
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        const int32_t lx = cx + (int32_t)lrintf(cosf(a) * 190.0f) - 28;
        const int32_t ly = cy + (int32_t)lrintf(sinf(a) * 190.0f) - 8;
        utility_set_label(i, k_lab[i], lx, ly, 58, k_col[i]);
        lv_obj_set_style_text_align(s_utility_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_opa(s_utility_labels[i], i == peak ? LV_OPA_COVER : 210, 0);
    }

    char status[96];
    snprintf(status,
             sizeof(status),
             "%s %.1f%%  %s",
             k_lab[peak],
             (double)(apoc.value[peak] * 100.0f),
             apoc.demo ? "DEMO" : "APOCALYPSO LIVE");
    lv_label_set_text(s_utility_status, status);
    lv_obj_set_style_text_color(s_utility_status, lv_color_hex(k_col[peak]), 0);
    lv_obj_set_style_text_align(s_utility_status, LV_TEXT_ALIGN_CENTER, 0);
}

static void draw_utility_faculty(uint32_t anim_ms)
{
    (void)anim_ms;
    if (faculty175_faculty_bust_status() == FACULTY175_FACULTY_BUST_IDLE ||
        faculty175_faculty_bust_status() == FACULTY175_FACULTY_BUST_ERROR) {
        faculty175_faculty_request_bust("a.einstein");
    }
    if (faculty_face_texture_load()) {
        if (s_faculty_face_image == NULL) {
            s_faculty_face_image = lv_image_create(s_utility_screen);
            lv_image_set_src(s_faculty_face_image, &s_faculty_face_texture);
            lv_obj_move_to_index(s_faculty_face_image, 1);
        } else {
            lv_image_set_src(s_faculty_face_image, &s_faculty_face_texture);
        }
        int x = 0;
        int y = 0;
        faculty175_faculty_bust_blit_origin(0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H, false, &x, &y);
        lv_obj_align(s_faculty_face_image, LV_ALIGN_TOP_LEFT, x, y);
        lv_obj_set_style_opa(s_faculty_face_image, LV_OPA_COVER, 0);
        native_obj_hidden(s_faculty_face_image, false);
        native_obj_hidden(s_utility_title, true);
        native_obj_hidden(s_utility_status, true);
        return;
    }

    utility_set_orb(11, 233, 218, 142, 0xd8d1c0, LV_OPA_COVER);
    utility_set_orb(12, 233, 316, 118, 0x40362e, LV_OPA_COVER);
    utility_set_label(0, "E", 222, 206, 36, 0x1a1714);
    lv_label_set_text(s_utility_status, "EINSTEIN READY");
}

static void draw_utility_wifilab(faculty175_face_id_t id, uint32_t anim_ms)
{
    faculty175_wifi_lab_state_t state = {};
    faculty175_wifi_lab_get_state(&state);

    const int32_t cx = 233;
    const int32_t cy = 238;
    const uint32_t accent = id == FACULTY175_FACE_DEAUTH     ? 0xff6b6b
                            : id == FACULTY175_FACE_EVILTWIN ? 0xffa657
                            : id == FACULTY175_FACE_HANDSHAKE ? 0xd2a8ff
                                                              : 0x62f0a8;
    utility_set_orb(0, cx, cy, 246, 0x0b1420, LV_OPA_COVER);
    for (int i = 0; i < 4; ++i) {
        utility_set_orb(i + 1, cx, cy, 58 + i * 44, 0x000000, 0);
        lv_obj_set_style_border_width(s_utility_orbs[i + 1], 2, 0);
        lv_obj_set_style_border_color(s_utility_orbs[i + 1], lv_color_hex(accent), 0);
        lv_obj_set_style_border_opa(s_utility_orbs[i + 1], (lv_opa_t)(180 - i * 30), 0);
    }
    const float sweep = -1.5708f + (float)(anim_ms % 4200u) / 4200.0f * 6.28318f;
    utility_set_line(0, cx, cy, cx + (int32_t)lrintf(cosf(sweep) * 132.0f),
                     cy + (int32_t)lrintf(sinf(sweep) * 132.0f), accent, 4, 220);

    char line0[40] = {};
    char line1[40] = {};
    char line2[40] = {};
    if (id == FACULTY175_FACE_WSCAN) {
        if (state.ap_count > 0 && state.selected < state.ap_count) {
            snprintf(line0, sizeof(line0), "%s", state.aps[state.selected].ssid);
            snprintf(line1, sizeof(line1), "%ddBm ch=%u", state.aps[state.selected].rssi,
                     (unsigned)state.aps[state.selected].channel);
            snprintf(line2, sizeof(line2), "%u/%u swipe target tap scan", (unsigned)state.selected + 1u,
                     (unsigned)state.ap_count);
        } else {
            snprintf(line0, sizeof(line0), "BROAD SCAN");
            snprintf(line1, sizeof(line1), "2.4 GHz hidden SSIDs");
            snprintf(line2, sizeof(line2), "tap to scan");
        }
    } else if (id == FACULTY175_FACE_DEAUTH) {
        snprintf(line0, sizeof(line0), state.active ? "DEAUTH ACTIVE" : "DEAUTH IDLE");
        snprintf(line1, sizeof(line1), "sent=%lu", (unsigned long)state.deauth_sent);
        snprintf(line2, sizeof(line2), "%s", state.target_ssid[0] != '\0' ? state.target_ssid : "scan first");
    } else if (id == FACULTY175_FACE_EVILTWIN) {
        snprintf(line0, sizeof(line0), state.active ? "TWIN LIVE" : "TWIN IDLE");
        snprintf(line1, sizeof(line1), "captures=%lu", (unsigned long)state.capture_count);
        snprintf(line2, sizeof(line2), "%s", state.last_cred[0] != '\0' ? state.last_cred : "portal /lab/portal");
    } else {
        snprintf(line0, sizeof(line0), state.active ? "CAPTURE ACTIVE" : "CAPTURE IDLE");
        snprintf(line1, sizeof(line1), "eapol=%lu", (unsigned long)state.handshake_count);
        if (state.pcap_path[0] != '\0') {
            snprintf(line2, sizeof(line2), "pcap %uB /lab/handshake.pcap", (unsigned)state.pcap_bytes);
        } else {
            snprintf(line2, sizeof(line2), "%s", state.target_ssid[0] != '\0' ? state.target_ssid : "scan first");
        }
    }

    utility_set_label(0, line0, 72, 118, 330, 0xf4f7ff);
    utility_set_label(1, line1, 72, 168, 330, accent);
    utility_set_label(2, line2, 48, 214, 380, 0x94a3b8);
    lv_obj_set_style_text_align(s_utility_labels[0], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(s_utility_labels[1], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(s_utility_labels[2], LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_utility_status, state.status[0] != '\0' ? state.status : "AUTHORIZED LAB ONLY");
    lv_obj_set_width(s_utility_status, 360);
    lv_obj_align(s_utility_status, LV_ALIGN_TOP_MID, 0, 392);
}

static void draw_utility_generic(faculty175_face_id_t id, uint32_t anim_ms)
{
    const uint32_t accent = native_hue_color(descriptor_hue_for_face(id), 30);
    utility_set_orb(0, 233, 238, 244, 0x111722, LV_OPA_COVER);
    utility_set_orb(1, 233, 238, 168, accent, 82);
    for (int i = 0; i < 8; ++i) {
        const float a = -1.5708f + (float)i * 0.785398f + (float)anim_ms * 0.00025f;
        utility_set_orb(i + 2, 233 + (int32_t)lrintf(cosf(a) * 112.0f),
                        238 + (int32_t)lrintf(sinf(a) * 112.0f), 18, accent, 190);
    }

    lv_label_set_text(s_utility_status, "READY");
}

static bool draw_utility(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (s_utility_screen == NULL) {
        create_utility_screen();
    }
    if (s_utility_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_utility_screen) {
        lv_screen_load(s_utility_screen);
    }

    utility_clear_objects();
    native_obj_hidden(s_utility_title, false);
    native_obj_hidden(s_utility_status, false);
    lv_label_set_text(s_utility_title, utility_title(id));
    if (id == FACULTY175_FACE_DIGITAL) {
        draw_utility_digital(anim_ms);
    } else if (id == FACULTY175_FACE_FACULTY) {
        draw_utility_faculty(anim_ms);
    } else if (id == FACULTY175_FACE_WEATHER) {
        draw_utility_weather(anim_ms);
    } else if (id == FACULTY175_FACE_CALCIFER) {
        draw_utility_calcifer(anim_ms);
    } else if (id == FACULTY175_FACE_CASTALIA) {
        draw_utility_castalia(anim_ms);
    } else if (id == FACULTY175_FACE_BABEL) {
        draw_utility_babel(anim_ms);
    } else if (id == FACULTY175_FACE_NOTES) {
        draw_utility_notes(anim_ms);
    } else if (id == FACULTY175_FACE_QUOTES) {
        draw_utility_quotes(anim_ms);
    } else if (id == FACULTY175_FACE_QDAY) {
        draw_utility_qday(anim_ms);
    } else if (id == FACULTY175_FACE_SPOTIFY) {
        draw_utility_spotify(anim_ms);
    } else if (id == FACULTY175_FACE_ROCKET) {
        draw_utility_rocket(anim_ms);
    } else if (id == FACULTY175_FACE_FOCUS) {
        draw_utility_focus(anim_ms);
    } else if (id == FACULTY175_FACE_BIOMETRICS) {
        draw_utility_biometrics(anim_ms);
    } else if (id == FACULTY175_FACE_HID) {
        draw_utility_hid(anim_ms);
    } else if (id == FACULTY175_FACE_SETTINGS) {
        draw_utility_settings(anim_ms);
    } else if (id == FACULTY175_FACE_WATCHER) {
        draw_utility_watcher(anim_ms);
    } else if (id == FACULTY175_FACE_INCIDENTS) {
        draw_utility_incidents(anim_ms);
    } else if (id == FACULTY175_FACE_DEATHSTAR) {
        draw_utility_deathstar(anim_ms);
    } else if (id == FACULTY175_FACE_APOCALYPSO) {
        draw_utility_apocalypso(anim_ms);
    } else if (id == FACULTY175_FACE_RADAR || id == FACULTY175_FACE_GLOBE) {
        draw_utility_radarish(id, anim_ms);
    } else if (id == FACULTY175_FACE_WSCAN || id == FACULTY175_FACE_DEAUTH || id == FACULTY175_FACE_EVILTWIN ||
               id == FACULTY175_FACE_HANDSHAKE) {
        draw_utility_wifilab(id, anim_ms);
    } else {
        draw_utility_generic(id, anim_ms);
    }

    lv_obj_invalidate(s_utility_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static float aleth_symbol_angle(int idx)
{
    return -1.57079632679f + ((float)idx * 6.28318530718f / 36.0f);
}

static float aleth_norm_angle(float a)
{
    while (a < -3.14159265359f) {
        a += 6.28318530718f;
    }
    while (a > 3.14159265359f) {
        a -= 6.28318530718f;
    }
    return a;
}

static void set_aleth_line(lv_obj_t *line, lv_point_precise_t points[2], float angle, int32_t r0, int32_t r1)
{
    const int32_t cx = FACULTY175_LCD_W / 2;
    const int32_t cy = FACULTY175_LCD_H / 2;
    points[0].x = (lv_value_precise_t)(cx + (int32_t)lrintf(cosf(angle) * (float)r0));
    points[0].y = (lv_value_precise_t)(cy + (int32_t)lrintf(sinf(angle) * (float)r0));
    points[1].x = (lv_value_precise_t)(cx + (int32_t)lrintf(cosf(angle) * (float)r1));
    points[1].y = (lv_value_precise_t)(cy + (int32_t)lrintf(sinf(angle) * (float)r1));
    lv_line_set_points(line, points, 2);
}

static void configure_aleth_line(lv_obj_t *line, uint32_t color, int32_t width, lv_opa_t opa)
{
    lv_obj_set_size(line, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_pos(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_opa(line, opa, 0);
}

#define ALETHIOMETER_EMOJI_SIZE 28

static bool aleth_custom_glyph_needed(int idx)
{
    switch (idx) {
        case 9:   /* Scythe */
        case 10:  /* Whip */
        case 16:  /* Stork */
        case 18:  /* Tower */
        case 19:  /* Garden */
        case 21:  /* Crossroads */
        case 29:  /* Lily */
        case 35:  /* Cross */
            return true;
        default:
            return false;
    }
}

static void aleth_glyph_put(uint8_t *dst, int x, int y, uint32_t color, uint8_t alpha)
{
    if (dst == NULL || x < 0 || x >= ALETHIOMETER_EMOJI_SIZE || y < 0 || y >= ALETHIOMETER_EMOJI_SIZE) {
        return;
    }
    const size_t off = ((size_t)y * ALETHIOMETER_EMOJI_SIZE + (size_t)x) * 4u;
    dst[off + 0u] = (uint8_t)(color & 0xffu);
    dst[off + 1u] = (uint8_t)((color >> 8) & 0xffu);
    dst[off + 2u] = (uint8_t)((color >> 16) & 0xffu);
    dst[off + 3u] = alpha;
}

static void aleth_glyph_line(uint8_t *dst, int x0, int y0, int x1, int y1, uint32_t color, int width)
{
    const int dx = abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        const int half = width / 2;
        for (int oy = -half; oy <= half; ++oy) {
            for (int ox = -half; ox <= half; ++ox) {
                if (ox * ox + oy * oy <= half * half + 1) {
                    aleth_glyph_put(dst, x0 + ox, y0 + oy, color, 255);
                }
            }
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void aleth_glyph_circle(uint8_t *dst, int cx, int cy, int r, uint32_t color, int width)
{
    for (int a = 0; a < 72; ++a) {
        const float t0 = (float)a * 6.28318530718f / 72.0f;
        const float t1 = (float)(a + 1) * 6.28318530718f / 72.0f;
        aleth_glyph_line(dst,
                         cx + (int)lrintf(cosf(t0) * (float)r),
                         cy + (int)lrintf(sinf(t0) * (float)r),
                         cx + (int)lrintf(cosf(t1) * (float)r),
                         cy + (int)lrintf(sinf(t1) * (float)r),
                         color,
                         width);
    }
}

static void aleth_glyph_arc(uint8_t *dst, int cx, int cy, int r, float a0, float a1, uint32_t color, int width)
{
    for (int i = 0; i < 24; ++i) {
        const float t0 = a0 + (a1 - a0) * (float)i / 24.0f;
        const float t1 = a0 + (a1 - a0) * (float)(i + 1) / 24.0f;
        aleth_glyph_line(dst,
                         cx + (int)lrintf(cosf(t0) * (float)r),
                         cy + (int)lrintf(sinf(t0) * (float)r),
                         cx + (int)lrintf(cosf(t1) * (float)r),
                         cy + (int)lrintf(sinf(t1) * (float)r),
                         color,
                         width);
    }
}

static bool aleth_render_custom_glyph(int idx, uint8_t *dst, uint32_t color)
{
    if (!aleth_custom_glyph_needed(idx) || dst == NULL) {
        return false;
    }
    memset(dst, 0, ALETHIOMETER_EMOJI_SIZE * ALETHIOMETER_EMOJI_SIZE * 4u);
    const uint32_t ink = color;
    const uint32_t dim = 0x8a7356;
    switch (idx) {
        case 9:  /* Scythe */
            aleth_glyph_arc(dst, 17, 11, 11, -2.7f, -0.15f, ink, 2);
            aleth_glyph_line(dst, 9, 20, 21, 6, ink, 2);
            aleth_glyph_line(dst, 7, 23, 12, 18, dim, 2);
            break;
        case 10:  /* Whip */
            aleth_glyph_arc(dst, 14, 15, 10, -2.8f, 1.2f, ink, 2);
            aleth_glyph_arc(dst, 15, 16, 6, -2.5f, 0.8f, dim, 2);
            aleth_glyph_line(dst, 18, 7, 23, 4, ink, 2);
            break;
        case 16:  /* Stork */
            aleth_glyph_line(dst, 7, 11, 15, 6, ink, 2);
            aleth_glyph_line(dst, 15, 6, 23, 10, ink, 2);
            aleth_glyph_line(dst, 13, 8, 11, 18, ink, 2);
            aleth_glyph_line(dst, 11, 18, 8, 24, ink, 2);
            aleth_glyph_line(dst, 13, 18, 18, 24, ink, 2);
            aleth_glyph_line(dst, 17, 8, 23, 5, dim, 1);
            break;
        case 18:  /* Tower */
            aleth_glyph_line(dst, 9, 24, 9, 7, ink, 2);
            aleth_glyph_line(dst, 19, 24, 19, 7, ink, 2);
            aleth_glyph_line(dst, 8, 7, 20, 7, ink, 2);
            aleth_glyph_line(dst, 7, 24, 21, 24, ink, 2);
            aleth_glyph_line(dst, 11, 4, 11, 8, dim, 2);
            aleth_glyph_line(dst, 14, 4, 14, 8, dim, 2);
            aleth_glyph_line(dst, 17, 4, 17, 8, dim, 2);
            aleth_glyph_line(dst, 12, 13, 16, 13, dim, 2);
            break;
        case 19:  /* Garden */
            aleth_glyph_circle(dst, 14, 14, 10, ink, 2);
            aleth_glyph_line(dst, 6, 18, 22, 18, dim, 2);
            aleth_glyph_line(dst, 9, 10, 9, 18, dim, 1);
            aleth_glyph_line(dst, 14, 8, 14, 18, dim, 1);
            aleth_glyph_line(dst, 19, 10, 19, 18, dim, 1);
            break;
        case 21:  /* Crossroads */
            aleth_glyph_line(dst, 14, 24, 14, 14, ink, 2);
            aleth_glyph_line(dst, 14, 14, 6, 6, ink, 2);
            aleth_glyph_line(dst, 14, 14, 22, 6, ink, 2);
            aleth_glyph_line(dst, 6, 6, 8, 11, ink, 2);
            aleth_glyph_line(dst, 6, 6, 11, 8, ink, 2);
            aleth_glyph_line(dst, 22, 6, 17, 8, ink, 2);
            aleth_glyph_line(dst, 22, 6, 20, 11, ink, 2);
            break;
        case 29:  /* Lily */
            aleth_glyph_line(dst, 14, 24, 14, 9, ink, 2);
            aleth_glyph_arc(dst, 10, 10, 6, -0.2f, 2.3f, ink, 2);
            aleth_glyph_arc(dst, 18, 10, 6, 0.8f, 3.2f, ink, 2);
            aleth_glyph_line(dst, 14, 9, 14, 4, dim, 2);
            aleth_glyph_line(dst, 8, 18, 20, 18, dim, 2);
            break;
        case 35:  /* Cross */
            aleth_glyph_line(dst, 14, 5, 14, 24, ink, 3);
            aleth_glyph_line(dst, 7, 12, 21, 12, ink, 3);
            break;
        default:
            return false;
    }
    return true;
}

static bool update_aleth_emoji_glyph(int idx, uint32_t color)
{
    if (idx < 0 || idx >= FACULTY175_ALETHIOMETER_GLYPH_COUNT) {
        return false;
    }
    if (s_aleth_glyph_pixels == NULL) {
        const size_t pixels = (size_t)FACULTY175_ALETHIOMETER_GLYPH_COUNT * ALETHIOMETER_EMOJI_SIZE * ALETHIOMETER_EMOJI_SIZE;
        s_aleth_glyph_pixels = heap_caps_malloc(pixels * 4u, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s_aleth_glyph_pixels == NULL) {
            s_aleth_glyph_pixels = heap_caps_malloc(pixels * 4u, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
        if (s_aleth_glyph_pixels == NULL) {
            ESP_LOGW(TAG, "alethiometer glyph pixel alloc failed");
            return false;
        }
        for (int i = 0; i < FACULTY175_ALETHIOMETER_GLYPH_COUNT; ++i) {
            s_aleth_glyph_colors[i] = UINT32_MAX;
            s_aleth_glyph_textures[i] = (lv_image_dsc_t) {
                .header = {
                    .magic = LV_IMAGE_HEADER_MAGIC,
                    .cf = LV_COLOR_FORMAT_ARGB8888,
                    .flags = 0,
                    .w = ALETHIOMETER_EMOJI_SIZE,
                    .h = ALETHIOMETER_EMOJI_SIZE,
                    .stride = ALETHIOMETER_EMOJI_SIZE * 4,
                    .reserved_2 = 0,
                },
                .data_size = ALETHIOMETER_EMOJI_SIZE * ALETHIOMETER_EMOJI_SIZE * 4,
                .data = &s_aleth_glyph_pixels[i * ALETHIOMETER_EMOJI_SIZE * ALETHIOMETER_EMOJI_SIZE * 4],
                .reserved = NULL,
                .reserved_2 = NULL,
            };
        }
    }
    if (s_aleth_glyph_colors[idx] == color) {
        return true;
    }
    uint8_t *dst = &s_aleth_glyph_pixels[idx * ALETHIOMETER_EMOJI_SIZE * ALETHIOMETER_EMOJI_SIZE * 4];
    if (aleth_render_custom_glyph(idx, dst, color)) {
        s_aleth_glyph_colors[idx] = color;
        return true;
    }
    if (!lenormand_glyph_pack_load(idx)) {
        return false;
    }

    for (int y = 0; y < ALETHIOMETER_EMOJI_SIZE; ++y) {
        const int sy = y * FACULTY175_LENORMAND_GLYPH_H / ALETHIOMETER_EMOJI_SIZE;
        for (int x = 0; x < ALETHIOMETER_EMOJI_SIZE; ++x) {
            const int sx = x * FACULTY175_LENORMAND_GLYPH_W / ALETHIOMETER_EMOJI_SIZE;
            const size_t src = ((size_t)sy * FACULTY175_LENORMAND_GLYPH_ROW_BYTES) + (size_t)sx * 4u;
            const size_t out = ((size_t)y * ALETHIOMETER_EMOJI_SIZE + (size_t)x) * 4u;
            dst[out + 0u] = s_lenormand_glyph_current_bits[src + 0u];
            dst[out + 1u] = s_lenormand_glyph_current_bits[src + 1u];
            dst[out + 2u] = s_lenormand_glyph_current_bits[src + 2u];
            dst[out + 3u] = s_lenormand_glyph_current_bits[src + 3u] < 18 ? 0 : s_lenormand_glyph_current_bits[src + 3u];
        }
    }
    s_aleth_glyph_colors[idx] = color;
    return true;
}

static void update_alethiometer_glyph_ring(const int targets[4])
{
    for (int i = 0; i < 36; ++i) {
        uint32_t color = 0xded0a2;
        for (int n = 0; n < 4; ++n) {
            if (targets[n] == i) {
                color = n == 3 ? 0x80d4ff : 0xe4b95c;
            }
        }
        if (update_aleth_emoji_glyph(i, color) && s_aleth_glyph_images[i] != NULL) {
            lv_image_set_src(s_aleth_glyph_images[i], &s_aleth_glyph_textures[i]);
            native_obj_hidden(s_aleth_glyph_images[i], false);
            lv_obj_invalidate(s_aleth_glyph_images[i]);
        }
        if ((i & 0x07) == 0x07) {
            vTaskDelay(1);
        }
    }
}

static void create_alethiometer_screen(void)
{
    s_aleth_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_aleth_screen);
    lv_obj_set_size(s_aleth_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_aleth_screen, lv_color_hex(0x08070c), 0);
    lv_obj_set_style_bg_opa(s_aleth_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_aleth_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_aleth_outer = make_circle(s_aleth_screen, 430, 0x08070c, LV_OPA_TRANSP);
    lv_obj_center(s_aleth_outer);
    lv_obj_set_style_border_width(s_aleth_outer, 0, 0);
    lv_obj_set_style_border_opa(s_aleth_outer, LV_OPA_TRANSP, 0);

    s_aleth_inner = make_circle(s_aleth_screen, 178, 0x1b1320, LV_OPA_COVER);
    lv_obj_center(s_aleth_inner);
    lv_obj_set_style_border_width(s_aleth_inner, 2, 0);
    lv_obj_set_style_border_color(s_aleth_inner, lv_color_hex(0x7d5a35), 0);
    lv_obj_set_style_border_opa(s_aleth_inner, 210, 0);

    static const int32_t center_sizes[4] = {184, 140, 96, 34};
    static const uint32_t center_colors[4] = {0x805c32, 0x46363e, 0x4e5662, 0xe8be62};
    static const int32_t center_widths[4] = {2, 2, 2, 3};
    for (int i = 0; i < 4; ++i) {
        s_aleth_center_rings[i] = make_arc_ring(s_aleth_screen,
                                                center_sizes[i],
                                                center_colors[i],
                                                center_widths[i],
                                                i == 3 ? 235 : 180);
        lv_obj_center(s_aleth_center_rings[i]);
    }

    for (int i = 0; i < 12; ++i) {
        const float a = -1.57079632679f + ((float)i * 6.28318530718f / 12.0f);
        s_aleth_center_spokes[i] = lv_line_create(s_aleth_screen);
        configure_aleth_line(s_aleth_center_spokes[i],
                             (i % 3 == 0) ? 0x986c38 : 0x42556f,
                             1,
                             130);
        set_aleth_line(s_aleth_center_spokes[i], s_aleth_spoke_points[i], a, 18, 86);
    }

    for (int i = 0; i < 36; ++i) {
        const float a = aleth_symbol_angle(i);
        s_aleth_glyph_images[i] = lv_image_create(s_aleth_screen);
        lv_obj_set_size(s_aleth_glyph_images[i], ALETHIOMETER_EMOJI_SIZE, ALETHIOMETER_EMOJI_SIZE);
        lv_obj_align(s_aleth_glyph_images[i],
                     LV_ALIGN_CENTER,
                     (int32_t)lrintf(cosf(a) * 205.0f),
                     (int32_t)lrintf(sinf(a) * 205.0f));
        native_obj_hidden(s_aleth_glyph_images[i], true);
    }

    for (int i = 0; i < 4; ++i) {
        s_aleth_needles[i] = lv_line_create(s_aleth_screen);
        configure_aleth_line(s_aleth_needles[i], i == 3 ? 0x80d4ff : 0xe0b65c, i == 3 ? 5 : 4, 235);
    }

    lv_obj_t *hub = make_circle(s_aleth_screen, 34, 0x1a1015, LV_OPA_COVER);
    lv_obj_center(hub);
    lv_obj_set_style_border_width(hub, 3, 0);
    lv_obj_set_style_border_color(hub, lv_color_hex(0xe8be62), 0);
    lv_obj_set_style_border_opa(hub, 235, 0);

    s_aleth_question = make_tarot_label(s_aleth_screen, 386, 360, 0x9f8b62);
    s_aleth_answer = make_tarot_label(s_aleth_screen, 410, 300, 0x6eb9e4);
    native_obj_hidden(s_aleth_question, true);
    native_obj_hidden(s_aleth_answer, true);
}

static bool draw_alethiometer(uint32_t anim_ms)
{
    if (s_aleth_screen == NULL) {
        create_alethiometer_screen();
    }
    if (s_aleth_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_aleth_screen) {
        lv_screen_load(s_aleth_screen);
    }

    int targets[4] = {};
    if (!faculty175_face_alethiometer_current(targets)) {
        return false;
    }
    update_alethiometer_glyph_ring(targets);
    float dt = s_aleth_last_anim_ms != 0 ? (float)(anim_ms - s_aleth_last_anim_ms) / 1000.0f : 0.016f;
    if (dt < 0.001f || dt > 0.08f) {
        dt = 0.016f;
    }
    s_aleth_last_anim_ms = anim_ms;
    for (int i = 0; i < 4; ++i) {
        const float target = aleth_symbol_angle(targets[i]);
        if (!s_aleth_angles_valid) {
            s_aleth_angles[i] = target;
            s_aleth_velocity[i] = 0.0f;
            s_aleth_last_targets[i] = targets[i];
        } else {
            if (s_aleth_last_targets[i] != targets[i]) {
                const float d = aleth_norm_angle(target - s_aleth_angles[i]);
                s_aleth_velocity[i] += d * (i == 3 ? 4.2f : 3.4f);
                s_aleth_last_targets[i] = targets[i];
            }
            const float d = aleth_norm_angle(target - s_aleth_angles[i]);
            const float stiffness = i == 3 ? 42.0f : 34.0f;
            const float damping = i == 3 ? 8.5f : 7.4f;
            s_aleth_velocity[i] += d * stiffness * dt;
            s_aleth_velocity[i] *= expf(-damping * dt);
            s_aleth_angles[i] = aleth_norm_angle(s_aleth_angles[i] + s_aleth_velocity[i] * dt);
            if (fabsf(d) < 0.002f && fabsf(s_aleth_velocity[i]) < 0.006f) {
                s_aleth_angles[i] = target;
                s_aleth_velocity[i] = 0.0f;
            }
        }
        const int32_t len = i == 3 ? 184 : 132 - i * 11;
        set_aleth_line(s_aleth_needles[i], s_aleth_needle_points[i], s_aleth_angles[i], -22, len);

    }
    s_aleth_angles_valid = true;

    char line[96];
    snprintf(line,
             sizeof(line),
             "%s  %s  %s",
             faculty175_face_alethiometer_symbol_name(targets[0]),
             faculty175_face_alethiometer_symbol_name(targets[1]),
             faculty175_face_alethiometer_symbol_name(targets[2]));
    lv_label_set_text(s_aleth_question, line);
    snprintf(line, sizeof(line), "ANSWER  %s", faculty175_face_alethiometer_symbol_name(targets[3]));
    lv_label_set_text(s_aleth_answer, line);

    (void)anim_ms;
    lv_obj_invalidate(s_aleth_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

typedef struct {
    const char *id;
    float ra;
    float dec;
    float mag;
} lvgl_sky_star_t;

typedef struct {
    uint8_t a;
    uint8_t b;
} lvgl_sky_seg_t;

static const lvgl_sky_star_t k_lvgl_sky_stars[] = {
    {"Sirius", 101.287f, -16.716f, -1.46f}, {"Canopus", 95.988f, -52.696f, -0.74f},
    {"Arcturus", 213.915f, 19.182f, -0.05f}, {"Vega", 279.235f, 38.784f, 0.03f},
    {"Capella", 79.172f, 45.998f, 0.08f}, {"Rigel", 78.634f, -8.202f, 0.13f},
    {"Procyon", 114.825f, 5.225f, 0.34f}, {"Betelgeuse", 88.793f, 7.407f, 0.42f},
    {"Altair", 297.695f, 8.868f, 0.76f}, {"Aldebaran", 68.98f, 16.509f, 0.85f},
    {"Antares", 247.352f, -26.432f, 0.96f}, {"Spica", 201.298f, -11.161f, 0.97f},
    {"Pollux", 116.329f, 28.026f, 1.14f}, {"Regulus", 152.093f, 11.967f, 1.35f},
    {"Castor", 113.649f, 31.888f, 1.57f}, {"Bellatrix", 81.283f, 6.35f, 1.64f},
    {"Alnilam", 84.053f, -1.202f, 1.69f}, {"Alnitak", 85.19f, -1.943f, 1.74f},
    {"Dubhe", 165.932f, 61.751f, 1.81f}, {"Alkaid", 206.885f, 49.313f, 1.85f},
    {"Polaris", 37.954f, 89.264f, 1.98f}, {"Saiph", 86.939f, -9.67f, 2.07f},
    {"Mizar", 200.981f, 54.925f, 2.23f}, {"Merak", 165.46f, 56.382f, 2.34f},
};

static const lvgl_sky_seg_t k_lvgl_sky_segments[] = {
    {7, 15}, {15, 17}, {7, 17}, {17, 16}, {16, 5}, {5, 21},
    {18, 23}, {23, 22}, {22, 19}, {18, 22},
    {6, 0}, {0, 21}, {10, 11}, {12, 14}, {9, 4}, {8, 3},
};

static bool sky_project_star(const lvgl_sky_star_t *star, float sidereal_deg, int *x, int *y)
{
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    const float hour = (sidereal_deg - star->ra) * 0.0174532925f;
    const float dec = star->dec * 0.0174532925f;
    const float alt_proxy = sinf(dec) * 0.35f + cosf(dec) * cosf(hour) * 0.65f;
    if (alt_proxy < -0.18f) {
        return false;
    }
    const float rr = (1.0f - alt_proxy) * 150.0f;
    const float az = atan2f(sinf(hour), cosf(hour) * sinf(dec) + 0.24f);
    *x = cx + (int)lrintf(sinf(az) * rr);
    *y = cy - (int)lrintf(cosf(az) * rr);
    const int dx = *x - cx;
    const int dy = *y - cy;
    return dx * dx + dy * dy < 178 * 178;
}

static void create_sky_screen(void)
{
    s_sky_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_sky_screen);
    lv_obj_set_size(s_sky_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_sky_screen, lv_color_hex(0x01030d), 0);
    lv_obj_set_style_bg_opa(s_sky_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_sky_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int grid_sizes[] = {428, 356, 236};
    for (int i = 0; i < 3; ++i) {
        s_sky_grid[i] = make_circle(s_sky_screen, grid_sizes[i], 0x000000, 0);
        lv_obj_center(s_sky_grid[i]);
        lv_obj_set_style_border_width(s_sky_grid[i], i == 0 ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_sky_grid[i], lv_color_hex(i == 0 ? 0x263958 : 0x162544), 0);
        lv_obj_set_style_border_opa(s_sky_grid[i], i == 0 ? 210 : 160, 0);
    }

    for (int i = 3; i < 5; ++i) {
        s_sky_grid[i] = lv_line_create(s_sky_screen);
        configure_aleth_line(s_sky_grid[i], 0x162544, 1, 155);
    }
    static lv_point_precise_t axis_h[2];
    static lv_point_precise_t axis_v[2];
    axis_h[0].x = 56;
    axis_h[0].y = FACULTY175_LCD_H / 2;
    axis_h[1].x = 410;
    axis_h[1].y = FACULTY175_LCD_H / 2;
    axis_v[0].x = FACULTY175_LCD_W / 2;
    axis_v[0].y = 56;
    axis_v[1].x = FACULTY175_LCD_W / 2;
    axis_v[1].y = 410;
    lv_line_set_points(s_sky_grid[3], axis_h, 2);
    lv_line_set_points(s_sky_grid[4], axis_v, 2);

    for (size_t i = 0; i < sizeof(s_sky_segments) / sizeof(s_sky_segments[0]); ++i) {
        s_sky_segments[i] = lv_line_create(s_sky_screen);
        configure_aleth_line(s_sky_segments[i], 0x5874ac, 1, 150);
        native_obj_hidden(s_sky_segments[i], true);
    }
    for (size_t i = 0; i < sizeof(s_sky_stars) / sizeof(s_sky_stars[0]); ++i) {
        s_sky_stars[i] = make_circle(s_sky_screen, 4, 0xdde6ff, LV_OPA_COVER);
        native_obj_hidden(s_sky_stars[i], true);
    }
    s_sky_label = make_tarot_label(s_sky_screen, 396, 260, 0xa8b8e0);
}

static bool draw_sky(uint32_t anim_ms)
{
    if (s_sky_screen == NULL) {
        create_sky_screen();
    }
    if (s_sky_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_sky_screen) {
        lv_screen_load(s_sky_screen);
    }

    const int shown_min = (int)((anim_ms / 1000u) % 1440u);
    const float sidereal = fmodf((float)shown_min * 0.25f + 110.0f, 360.0f);
    int sx[sizeof(k_lvgl_sky_stars) / sizeof(k_lvgl_sky_stars[0])];
    int sy[sizeof(k_lvgl_sky_stars) / sizeof(k_lvgl_sky_stars[0])];
    bool vis[sizeof(k_lvgl_sky_stars) / sizeof(k_lvgl_sky_stars[0])];
    for (size_t i = 0; i < sizeof(k_lvgl_sky_stars) / sizeof(k_lvgl_sky_stars[0]); ++i) {
        vis[i] = sky_project_star(&k_lvgl_sky_stars[i], sidereal, &sx[i], &sy[i]);
        if (!vis[i]) {
            native_obj_hidden(s_sky_stars[i], true);
            continue;
        }
        const int r = k_lvgl_sky_stars[i].mag < 0.5f ? 6 : (k_lvgl_sky_stars[i].mag < 1.8f ? 4 : 3);
        lv_obj_set_size(s_sky_stars[i], r, r);
        lv_obj_align(s_sky_stars[i], LV_ALIGN_TOP_LEFT, sx[i] - r / 2, sy[i] - r / 2);
        lv_obj_set_style_bg_opa(s_sky_stars[i], k_lvgl_sky_stars[i].mag < 0.5f ? LV_OPA_COVER : 210, 0);
        native_obj_hidden(s_sky_stars[i], false);
    }
    for (size_t i = 0; i < sizeof(k_lvgl_sky_segments) / sizeof(k_lvgl_sky_segments[0]); ++i) {
        const uint8_t a = k_lvgl_sky_segments[i].a;
        const uint8_t b = k_lvgl_sky_segments[i].b;
        if (!vis[a] || !vis[b]) {
            native_obj_hidden(s_sky_segments[i], true);
            continue;
        }
        s_sky_segment_points[i][0].x = sx[a];
        s_sky_segment_points[i][0].y = sy[a];
        s_sky_segment_points[i][1].x = sx[b];
        s_sky_segment_points[i][1].y = sy[b];
        lv_line_set_points(s_sky_segments[i], s_sky_segment_points[i], 2);
        native_obj_hidden(s_sky_segments[i], false);
    }

    char line[32];
    snprintf(line, sizeof(line), "%02d:%02d SKY", shown_min / 60, shown_min % 60);
    lv_label_set_text(s_sky_label, line);
    lv_obj_invalidate(s_sky_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void almanac_set_trimmed(lv_obj_t *label, const char *text, size_t max_chars)
{
    if (label == NULL) {
        return;
    }
    if (text == NULL || text[0] == '\0') {
        lv_label_set_text(label, "");
        return;
    }
    char buf[96];
    strlcpy(buf, text, sizeof(buf));
    if (max_chars > 3 && strlen(buf) > max_chars) {
        buf[max_chars - 3] = '.';
        buf[max_chars - 2] = '.';
        buf[max_chars - 1] = '.';
        buf[max_chars] = '\0';
    }
    lv_label_set_text(label, buf);
}

#if FACULTY175_ENABLE_ALMANAC_FACES
static void create_almanac_screen(void)
{
    s_almanac_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_almanac_screen);
    lv_obj_set_size(s_almanac_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_almanac_screen, lv_color_hex(0x07090d), 0);
    lv_obj_set_style_bg_opa(s_almanac_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_almanac_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_almanac_outer = lv_arc_create(s_almanac_screen);
    lv_obj_remove_style(s_almanac_outer, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_almanac_outer, 430, 430);
    lv_obj_center(s_almanac_outer);
    lv_arc_set_range(s_almanac_outer, 0, 100);
    lv_arc_set_value(s_almanac_outer, 78);
    lv_arc_set_bg_angles(s_almanac_outer, 35, 325);
    lv_arc_set_rotation(s_almanac_outer, 270);
    lv_obj_clear_flag(s_almanac_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_almanac_outer, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_almanac_outer, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_almanac_outer, lv_color_hex(0x243836), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_almanac_outer, lv_color_hex(0x6ec292), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_almanac_outer, 180, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_almanac_outer, 225, LV_PART_INDICATOR);

    s_almanac_moon = make_circle(s_almanac_screen, 74, 0xded8bf, LV_OPA_COVER);
    lv_obj_align(s_almanac_moon, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_border_width(s_almanac_moon, 2, 0);
    lv_obj_set_style_border_color(s_almanac_moon, lv_color_hex(0x6ec292), 0);
    lv_obj_set_style_border_opa(s_almanac_moon, 210, 0);
    s_almanac_moon_shadow = make_circle(s_almanac_screen, 58, 0x07090d, LV_OPA_COVER);
    lv_obj_align(s_almanac_moon_shadow, LV_ALIGN_TOP_MID, 18, 83);

    s_almanac_date = make_tarot_label(s_almanac_screen, 176, 300, 0xe6eadc);
    s_almanac_season = make_tarot_label(s_almanac_screen, 204, 330, 0x6ec292);
    s_almanac_sky = make_tarot_label(s_almanac_screen, 232, 360, 0x78aee2);
    s_almanac_event = make_tarot_label(s_almanac_screen, 282, 390, 0xdcae5e);
    s_almanac_planting = make_tarot_label(s_almanac_screen, 310, 390, 0xe0dfcc);
    s_almanac_status = make_tarot_label(s_almanac_screen, 360, 340, 0x809090);
}

static void phenology_release_texture(void)
{
    if (s_phenology_pixels != NULL) {
        heap_caps_free(s_phenology_pixels);
        s_phenology_pixels = NULL;
    }
    memset(&s_phenology_texture, 0, sizeof(s_phenology_texture));
    s_phenology_loaded_path[0] = '\0';
}

static bool phenology_image_load(const char *path)
{
    if (path == NULL || path[0] == '\0' || strstr(path, ".rgb565") == NULL) {
        return false;
    }
    if (s_phenology_pixels != NULL && strcmp(s_phenology_loaded_path, path) == 0) {
        return true;
    }
    if (!moon_storage_ready()) {
        return false;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    const size_t expected = (size_t)FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t);
    uint16_t *pixels = heap_caps_malloc(expected, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (pixels == NULL) {
        pixels = heap_caps_malloc(expected, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (pixels == NULL) {
        fclose(f);
        ESP_LOGW(TAG, "phenology image pixel alloc failed");
        return false;
    }
    const size_t got = fread(pixels, 1, expected, f);
    fclose(f);
    if (got != expected) {
        heap_caps_free(pixels);
        ESP_LOGW(TAG, "phenology image short read %u/%u %s", (unsigned)got, (unsigned)expected, path);
        return false;
    }

    phenology_release_texture();
    s_phenology_pixels = pixels;
    snprintf(s_phenology_loaded_path, sizeof(s_phenology_loaded_path), "%s", path);
    s_phenology_texture = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = FACULTY175_LCD_W,
            .h = FACULTY175_LCD_H,
            .stride = FACULTY175_LCD_W * sizeof(uint16_t),
            .reserved_2 = 0,
        },
        .data_size = expected,
        .data = (const uint8_t *)s_phenology_pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };
    ESP_LOGI(TAG, "phenology image loaded %s", path);
    return true;
}

static void create_phenology_screen(void)
{
    s_phenology_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_phenology_screen);
    lv_obj_set_size(s_phenology_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_phenology_screen, lv_color_hex(0x07100c), 0);
    lv_obj_set_style_bg_opa(s_phenology_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_phenology_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_phenology_image = lv_image_create(s_phenology_screen);
    native_obj_hidden(s_phenology_image, true);
    for (int i = 0; i < 8; ++i) {
        s_phenology_orbs[i] = make_circle(s_phenology_screen, 20, 0x6ec292, 180);
    }
    s_phenology_panel = lv_obj_create(s_phenology_screen);
    lv_obj_remove_style_all(s_phenology_panel);
    lv_obj_set_size(s_phenology_panel, FACULTY175_LCD_W, 126);
    lv_obj_align(s_phenology_panel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_phenology_panel, lv_color_hex(0x07100c), 0);
    lv_obj_set_style_bg_opa(s_phenology_panel, 218, 0);
    lv_obj_set_style_border_width(s_phenology_panel, 0, 0);
    s_phenology_subject = make_tarot_label(s_phenology_screen, 332, 360, 0xf2ead6);
    s_phenology_action = make_tarot_label(s_phenology_screen, 360, 390, 0xc8d8b0);
    s_phenology_status = make_tarot_label(s_phenology_screen, 414, 340, 0x809a88);
}

static bool draw_phenology(uint32_t anim_ms)
{
    if (s_phenology_screen == NULL) {
        create_phenology_screen();
    }
    if (s_phenology_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_phenology_screen) {
        lv_screen_load(s_phenology_screen);
    }

    char date[24];
    char subject[64];
    char action[96];
    char habitat[64];
    char prompt[160];
    char image_path[112];
    const bool cached = faculty175_almanac_cached_phenology(date,
                                                            sizeof(date),
                                                            subject,
                                                            sizeof(subject),
                                                            action,
                                                            sizeof(action),
                                                            habitat,
                                                            sizeof(habitat),
                                                            prompt,
                                                            sizeof(prompt),
                                                            image_path,
                                                            sizeof(image_path));
    const bool image_ok = cached && phenology_image_load(image_path);
    native_obj_hidden(s_phenology_image, !image_ok);
    if (image_ok) {
        lv_image_set_src(s_phenology_image, &s_phenology_texture);
        lv_obj_align(s_phenology_image, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    const uint32_t palette[4] = {0x173323, 0x466b35, 0xd0a85c, 0x7aa8c8};
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * 0.785398f + (float)anim_ms * 0.00018f;
        const int32_t r = 58 + (i % 4) * 31;
        const int32_t size = image_ok ? 26 + i * 2 : 54 + (i % 3) * 24;
        lv_obj_set_size(s_phenology_orbs[i], size, size);
        lv_obj_set_style_radius(s_phenology_orbs[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(s_phenology_orbs[i], lv_color_hex(palette[i % 4]), 0);
        lv_obj_set_style_bg_opa(s_phenology_orbs[i], image_ok ? 54 : (lv_opa_t)(150 + i * 10), 0);
        lv_obj_align(s_phenology_orbs[i],
                     LV_ALIGN_TOP_LEFT,
                     233 + (int32_t)lrintf(cosf(a) * (float)r) - size / 2,
                     176 + (int32_t)lrintf(sinf(a) * (float)r) - size / 2);
    }

    if (cached) {
        almanac_set_trimmed(s_phenology_subject, subject[0] != '\0' ? subject : "PHENOLOGY", 42);
        almanac_set_trimmed(s_phenology_action, action[0] != '\0' ? action : prompt, 48);
    } else {
        lv_label_set_text(s_phenology_subject, "PHENOLOGY");
        almanac_set_trimmed(s_phenology_action, faculty175_almanac_last(), 48);
    }
    char status[96];
    snprintf(status,
             sizeof(status),
             "%s%s%s",
             image_ok ? "image" : "waiting image",
             habitat[0] != '\0' ? " / " : "",
             habitat);
    almanac_set_trimmed(s_phenology_status, status, 42);

    lv_obj_invalidate(s_phenology_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static bool draw_almanac(uint32_t anim_ms)
{
    if (s_almanac_screen == NULL) {
        create_almanac_screen();
    }
    if (s_almanac_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_almanac_screen) {
        lv_screen_load(s_almanac_screen);
    }

    char date[24];
    char season[40];
    char moon[40];
    char sun[32];
    char event[64];
    char planting[72];
    char prompt[144];
    const bool cached = faculty175_almanac_cached_daily_ex(date,
                                                           sizeof(date),
                                                           season,
                                                           sizeof(season),
                                                           moon,
                                                           sizeof(moon),
                                                           sun,
                                                           sizeof(sun),
                                                           event,
                                                           sizeof(event),
                                                           planting,
                                                           sizeof(planting),
                                                           prompt,
                                                           sizeof(prompt));
    if (cached) {
        almanac_set_trimmed(s_almanac_date, date, 18);
        almanac_set_trimmed(s_almanac_season, season, 30);
        char sky[80];
        snprintf(sky, sizeof(sky), "%s  Sun %s", moon, sun[0] != '\0' ? sun : "-");
        almanac_set_trimmed(s_almanac_sky, sky, 42);
        almanac_set_trimmed(s_almanac_event, event[0] != '\0' ? event : planting, 44);
        almanac_set_trimmed(s_almanac_planting, planting[0] != '\0' ? planting : prompt, 44);
    } else {
        lv_label_set_text(s_almanac_date, "ALMANAC");
        lv_label_set_text(s_almanac_season, "NO CACHE");
        almanac_set_trimmed(s_almanac_sky, faculty175_almanac_last(), 42);
        lv_label_set_text(s_almanac_event, "almanac fetch");
        almanac_set_trimmed(s_almanac_planting, faculty175_almanac_manifest_url(), 44);
    }

    char status[64];
    snprintf(status, sizeof(status), "%s %s", faculty175_almanac_state_name(), faculty175_almanac_active() ? "SYNC" : "");
    almanac_set_trimmed(s_almanac_status, status, 28);
    lv_obj_set_style_text_color(s_almanac_status,
                                lv_color_hex(faculty175_almanac_active() ? 0x6ec292 : 0x809090),
                                0);
    lv_arc_set_value(s_almanac_outer, 68 + (int32_t)((anim_ms / 160u) % 24u));
    lv_obj_align(s_almanac_moon_shadow, LV_ALIGN_TOP_MID, cached ? 18 : 0, 83);

    lv_obj_invalidate(s_almanac_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}
#endif

typedef struct {
    const char *name;
    const char *scale;
    const char *note;
    double meters;
    uint32_t color;
} lvgl_scale_gate_t;

static const lvgl_scale_gate_t k_lvgl_scale_gates[] = {
    {"LOCAL", "10 m", "human room", 10.0, 0x5cdca8},
    {"EARTH", "12,742 km", "mean diameter", 12742000.0, 0x5aaaff},
    {"MOON", "384,400 km", "mean distance", 384400000.0, 0xd2dae8},
    {"SOLAR", "1 AU", "149,597,870 km", 149597870700.0, 0xffcc6a},
    {"STARS", "4.25 ly", "Proxima Centauri", 4.2465 * 9.4607304725808e15, 0x96d2ff},
    {"GALAXY", "105,700 ly", "Milky Way", 105700.0 * 9.4607304725808e15, 0xdeb2ff},
};

static float scale_smoothstep(float t)
{
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

static int scale_zoom_radius(float zoom, int base, int min_r, int max_r)
{
    int r = (int)lrintf((float)base * (0.48f + zoom * 1.52f));
    if (r < min_r) {
        r = min_r;
    }
    if (r > max_r) {
        r = max_r;
    }
    return r;
}

static void scale_format_time_line(char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    if (!astrolabe_time_valid()) {
        snprintf(out, cap, "time sync pending");
        return;
    }
    char local[24] = {};
    char utc[24] = {};
    (void)astrolabe_time_format_local(local, sizeof(local));
    (void)astrolabe_time_format_utc(utc, sizeof(utc));
    const char *local_time = strlen(local) >= 16 ? local + 11 : local;
    const char *utc_time = strlen(utc) >= 16 ? utc + 11 : utc;
    snprintf(out, cap, "LOCAL %.8s  UTC %.8s", local_time, utc_time);
}

static void create_scale_screen(void)
{
    s_scale_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_scale_screen);
    lv_obj_set_size(s_scale_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_scale_screen, lv_color_hex(0x030712), 0);
    lv_obj_set_style_bg_opa(s_scale_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_scale_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_scale_outer = lv_arc_create(s_scale_screen);
    lv_obj_remove_style(s_scale_outer, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_scale_outer, 424, 424);
    lv_obj_center(s_scale_outer);
    lv_arc_set_range(s_scale_outer, 0, 100);
    lv_arc_set_value(s_scale_outer, 50);
    lv_arc_set_bg_angles(s_scale_outer, 34, 326);
    lv_arc_set_rotation(s_scale_outer, 270);
    lv_obj_clear_flag(s_scale_outer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_scale_outer, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_scale_outer, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_scale_outer, lv_color_hex(0x1d2a42), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_scale_outer, 180, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_scale_outer, 230, LV_PART_INDICATOR);

    s_scale_inner = make_circle(s_scale_screen, 236, 0x07111e, LV_OPA_COVER);
    lv_obj_center(s_scale_inner);
    lv_obj_set_style_border_width(s_scale_inner, 1, 0);
    lv_obj_set_style_border_color(s_scale_inner, lv_color_hex(0x243a5c), 0);
    lv_obj_set_style_border_opa(s_scale_inner, 170, 0);

    s_scale_earth = lv_image_create(s_scale_screen);
    lv_image_set_src(s_scale_earth, &s_scale_earth_texture);
    lv_obj_align(s_scale_earth, LV_ALIGN_CENTER, 0, -22);
    lv_obj_set_style_radius(s_scale_earth, 4, 0);
    lv_obj_set_style_opa(s_scale_earth, 230, 0);

    s_scale_moon = make_circle(s_scale_screen, 18, 0xd2dae8, LV_OPA_COVER);
    s_scale_sun = make_circle(s_scale_screen, 20, 0xffcc6a, LV_OPA_COVER);

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2;
    for (size_t i = 0; i < sizeof(s_scale_gates) / sizeof(s_scale_gates[0]); ++i) {
        s_scale_gates[i] = make_circle(s_scale_screen, 8, 0x58667c, LV_OPA_COVER);
        const float log_m = (float)log10(k_lvgl_scale_gates[i].meters);
        const float t = (log_m - 1.0f) / 20.0f;
        const float a = -2.72f + t * 5.44f;
        const int x = cx + (int)lrintf(cosf(a) * 207.0f);
        const int y = cy + (int)lrintf(sinf(a) * 207.0f);
        lv_obj_align(s_scale_gates[i], LV_ALIGN_TOP_LEFT, x - 4, y - 4);
    }

    s_scale_gate_label = make_tarot_label(s_scale_screen, 72, 280, 0xf1e8ce);
    s_scale_value_label = make_tarot_label(s_scale_screen, 340, 320, 0xd8e2f0);
    s_scale_note_label = make_tarot_label(s_scale_screen, 370, 330, 0x95a5bd);
    s_scale_time_label = make_tarot_label(s_scale_screen, 398, 330, 0x66788e);
}

static bool draw_scale(uint32_t anim_ms)
{
    if (s_scale_screen == NULL) {
        create_scale_screen();
    }
    if (s_scale_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_scale_screen) {
        lv_screen_load(s_scale_screen);
    }

    const size_t gate_count = sizeof(k_lvgl_scale_gates) / sizeof(k_lvgl_scale_gates[0]);
    const uint32_t slot_ms = astrolabe_time_valid() ? 7000u : 5200u;
    const uint32_t phase_ms = astrolabe_time_valid() ? (uint32_t)((astrolabe_time_now() % (time_t)(gate_count * 7u)) * 1000u) :
                                                       (anim_ms % (slot_ms * (uint32_t)gate_count));
    const size_t active = (size_t)((phase_ms / slot_ms) % gate_count);
    const lvgl_scale_gate_t *gate = &k_lvgl_scale_gates[active];
    const float raw_phase = (float)(phase_ms % slot_ms) / (float)slot_ms;
    const float zoom = 0.22f + scale_smoothstep(sinf(raw_phase * 3.1415926f)) * 0.78f;

    lv_obj_set_style_arc_color(s_scale_outer, lv_color_hex(gate->color), LV_PART_INDICATOR);
    lv_arc_set_value(s_scale_outer, 22 + (int32_t)(zoom * 72.0f));

    const int earth_w = scale_zoom_radius(zoom, 96, 76, 150);
    const int earth_h = scale_zoom_radius(zoom, 48, 38, 74);
    lv_obj_set_size(s_scale_earth, earth_w, earth_h);
    lv_obj_align(s_scale_earth, LV_ALIGN_CENTER, ((int32_t)(anim_ms / 90u) % 9) - 4, -18);

    const int orbit = scale_zoom_radius(zoom, 92, 56, 160);
    const float moon_a = (float)(anim_ms % 7000u) / 7000.0f * 6.2831853f;
    lv_obj_align(s_scale_moon,
                 LV_ALIGN_CENTER,
                 (int32_t)lrintf(cosf(moon_a) * (float)orbit) - 9,
                 (int32_t)lrintf(sinf(moon_a) * (float)orbit) - 18);

    const float sun_a = (float)(anim_ms % 11000u) / 11000.0f * 6.2831853f;
    lv_obj_align(s_scale_sun,
                 LV_ALIGN_CENTER,
                 (int32_t)lrintf(cosf(sun_a) * 128.0f) - 10,
                 (int32_t)lrintf(sinf(sun_a) * 78.0f) - 10);
    native_obj_hidden(s_scale_moon, active == 0);
    native_obj_hidden(s_scale_sun, active < 3);

    for (size_t i = 0; i < gate_count; ++i) {
        const bool on = i == active;
        lv_obj_set_size(s_scale_gates[i], on ? 13 : 7, on ? 13 : 7);
        lv_obj_set_style_bg_color(s_scale_gates[i], lv_color_hex(on ? k_lvgl_scale_gates[i].color : 0x58667c), 0);
        lv_obj_set_style_bg_opa(s_scale_gates[i], on ? LV_OPA_COVER : 180, 0);
    }

    char top[44];
    snprintf(top, sizeof(top), "SCALE ATLAS  %s", gate->name);
    lv_label_set_text(s_scale_gate_label, top);
    char value[56];
    snprintf(value, sizeof(value), "%s  log10m %.1f", gate->scale, log10(gate->meters));
    lv_label_set_text(s_scale_value_label, value);
    lv_label_set_text(s_scale_note_label, gate->note);
    char time_line[48];
    scale_format_time_line(time_line, sizeof(time_line));
    lv_label_set_text(s_scale_time_label, time_line);

    lv_obj_invalidate(s_scale_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

typedef struct {
    const char *label;
    float base_lon;
    float deg_per_day;
    uint32_t color;
} lvgl_astro_body_t;

static const char *const k_lvgl_astro_signs[12] = {"Ar", "Ta", "Ge", "Cn", "Le", "Vi", "Li", "Sc", "Sg", "Cp", "Aq", "Pi"};
static const lvgl_astro_body_t k_lvgl_astro_bodies[7] = {
    {"Su", 280.5f, 0.9856f, 0xffd25a},
    {"Mo", 218.3f, 13.1764f, 0xd2dae8},
    {"Me", 296.1f, 4.0923f, 0xb2b6c4},
    {"Ve", 334.2f, 1.6021f, 0xffbe8c},
    {"Ma", 54.7f, 0.5240f, 0xe65a46},
    {"Ju", 72.0f, 0.0831f, 0xdcb478},
    {"Sa", 312.0f, 0.0335f, 0xbeaa8c},
};

static float astrology_wrap360(float v)
{
    while (v < 0.0f) {
        v += 360.0f;
    }
    while (v >= 360.0f) {
        v -= 360.0f;
    }
    return v;
}

static uint32_t astrology_days_since_j2000(uint32_t anim_ms)
{
    time_t now = astrolabe_time_valid() ? astrolabe_time_now() : time(NULL);
    if (now > 946728000) {
        return (uint32_t)((now - 946728000) / 86400);
    }
    return 9400u + anim_ms / 86400000u;
}

static void astrology_local_ephemeris(uint32_t anim_ms, float lon[7])
{
    const float days = (float)astrology_days_since_j2000(anim_ms) + (float)(anim_ms % 86400000u) / 86400000.0f;
    for (size_t i = 0; i < 7; ++i) {
        lon[i] = astrology_wrap360(k_lvgl_astro_bodies[i].base_lon + days * k_lvgl_astro_bodies[i].deg_per_day);
    }
}

static void astrology_xy_for_lon(double lon, int radius, int *x, int *y)
{
    const float a = 3.1415926f + (float)lon * 0.0174532925f;
    *x = FACULTY175_LCD_W / 2 + (int)lrintf(cosf(a) * (float)radius);
    *y = FACULTY175_LCD_H / 2 + 2 + (int)lrintf(sinf(a) * (float)radius);
}

static void astrology_glyph_clear(int sign)
{
    if (sign < 0 || sign >= 12) {
        return;
    }
    for (int i = 0; i < 7; ++i) {
        native_obj_hidden(s_astrology_glyph_lines[sign][i], true);
    }
}

static void astrology_glyph_seg(int sign,
                                int seg,
                                int cx,
                                int cy,
                                int x0,
                                int y0,
                                int x1,
                                int y1,
                                uint32_t color,
                                int width)
{
    if (sign < 0 || sign >= 12 || seg < 0 || seg >= 7 || s_astrology_glyph_lines[sign][seg] == NULL) {
        return;
    }
    s_astrology_glyph_points[sign][seg][0].x = (lv_value_precise_t)(cx + x0);
    s_astrology_glyph_points[sign][seg][0].y = (lv_value_precise_t)(cy + y0);
    s_astrology_glyph_points[sign][seg][1].x = (lv_value_precise_t)(cx + x1);
    s_astrology_glyph_points[sign][seg][1].y = (lv_value_precise_t)(cy + y1);
    lv_line_set_points(s_astrology_glyph_lines[sign][seg], s_astrology_glyph_points[sign][seg], 2);
    lv_obj_set_style_line_color(s_astrology_glyph_lines[sign][seg], lv_color_hex(color), 0);
    lv_obj_set_style_line_width(s_astrology_glyph_lines[sign][seg], width, 0);
    lv_obj_set_style_line_opa(s_astrology_glyph_lines[sign][seg], LV_OPA_COVER, 0);
    native_obj_hidden(s_astrology_glyph_lines[sign][seg], false);
}

static void astrology_draw_zodiac_glyph(int sign, int cx, int cy, bool highlight)
{
    const uint32_t color = highlight ? 0xfff6c8 : 0xaeb6cd;
    const int width = highlight ? 3 : 2;
    astrology_glyph_clear(sign);
    switch (sign) {
        case 0:
            astrology_glyph_seg(sign, 0, cx, cy, 0, 9, 0, -7, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, 0, -7, -11, 7, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 0, -7, 11, 7, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, -11, 7, -4, 0, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, 11, 7, 4, 0, color, width);
            break;
        case 1:
            astrology_glyph_seg(sign, 0, cx, cy, -9, 0, -5, 8, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -5, 8, 5, 8, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 5, 8, 9, 0, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 9, 0, 5, -8, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, 5, -8, -5, -8, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, -5, -8, -9, 0, color, width);
            astrology_glyph_seg(sign, 6, cx, cy, -11, -12, -3, -6, color, width);
            break;
        case 2:
            astrology_glyph_seg(sign, 0, cx, cy, -9, -10, 9, -10, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -9, 10, 9, 10, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, -6, -10, -6, 10, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 6, -10, 6, 10, color, width);
            break;
        case 3:
            astrology_glyph_seg(sign, 0, cx, cy, -10, -2, -2, -8, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -2, -8, 8, -4, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, -8, 4, 2, 8, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 2, 8, 10, 2, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -4, -5, -8, -1, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 4, 5, 8, 1, color, width);
            break;
        case 4:
            astrology_glyph_seg(sign, 0, cx, cy, -11, 6, -7, -3, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -7, -3, -1, -7, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, -1, -7, 6, -4, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 6, -4, 7, 3, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, 7, 3, 0, 10, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 0, 10, 11, 10, color, width);
            break;
        case 5:
            astrology_glyph_seg(sign, 0, cx, cy, -10, -10, -10, 10, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -5, -10, -5, 10, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 0, -10, 0, 8, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, -10, -5, -5, -10, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -5, -5, 0, -10, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 0, 7, 10, -1, color, width);
            astrology_glyph_seg(sign, 6, cx, cy, 10, -1, 7, 10, color, width);
            break;
        case 6:
            astrology_glyph_seg(sign, 0, cx, cy, -11, 8, 11, 8, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -11, 2, -3, 2, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 3, 2, 11, 2, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, -3, 2, -1, -6, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -1, -6, 1, -6, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 1, -6, 3, 2, color, width);
            break;
        case 7:
            astrology_glyph_seg(sign, 0, cx, cy, -10, -10, -10, 10, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -5, -10, -5, 10, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 0, -10, 0, 8, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, -10, -5, -5, -10, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -5, -5, 0, -10, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 0, 8, 10, 2, color, width);
            astrology_glyph_seg(sign, 6, cx, cy, 10, 2, 6, 1, color, width);
            break;
        case 8:
            astrology_glyph_seg(sign, 0, cx, cy, -9, 9, 9, -9, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, 9, -9, 7, 4, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 9, -9, -4, -7, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, -5, -1, 3, 7, color, width);
            break;
        case 9:
            astrology_glyph_seg(sign, 0, cx, cy, -10, -10, -5, 8, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -5, 8, 0, -10, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 0, -10, 0, 8, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 0, 8, 9, 6, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, 9, 6, 7, -1, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, 7, -1, 2, 1, color, width);
            break;
        case 10:
            astrology_glyph_seg(sign, 0, cx, cy, -11, -4, -6, -8, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -6, -8, -1, -4, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, -1, -4, 4, -8, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 4, -8, 11, -4, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -11, 6, -6, 2, color, width);
            astrology_glyph_seg(sign, 5, cx, cy, -6, 2, -1, 6, color, width);
            astrology_glyph_seg(sign, 6, cx, cy, -1, 6, 4, 2, color, width);
            break;
        case 11:
            astrology_glyph_seg(sign, 0, cx, cy, -9, -10, -4, 0, color, width);
            astrology_glyph_seg(sign, 1, cx, cy, -4, 0, -9, 10, color, width);
            astrology_glyph_seg(sign, 2, cx, cy, 9, -10, 4, 0, color, width);
            astrology_glyph_seg(sign, 3, cx, cy, 4, 0, 9, 10, color, width);
            astrology_glyph_seg(sign, 4, cx, cy, -11, 0, 11, 0, color, width);
            break;
        default:
            break;
    }
}

static void create_astrology_screen(void)
{
    s_astrology_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_astrology_screen);
    lv_obj_set_size(s_astrology_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_astrology_screen, lv_color_hex(0x070912), 0);
    lv_obj_set_style_bg_opa(s_astrology_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_astrology_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int ring_sizes[] = {432, 404, 188, 96};
    for (size_t i = 0; i < sizeof(ring_sizes) / sizeof(ring_sizes[0]); ++i) {
        s_astrology_rings[i] = make_circle(s_astrology_screen, ring_sizes[i], 0x000000, 0);
        lv_obj_center(s_astrology_rings[i]);
        lv_obj_set_style_border_width(s_astrology_rings[i], i == 0 ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_astrology_rings[i], lv_color_hex(i == 3 ? 0xb49a58 : 0x343e54), 0);
        lv_obj_set_style_border_opa(s_astrology_rings[i], i == 3 ? 210 : 170, 0);
    }

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        s_astrology_spoke_points[i][0].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 82.0f);
        s_astrology_spoke_points[i][0].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 82.0f);
        s_astrology_spoke_points[i][1].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 202.0f);
        s_astrology_spoke_points[i][1].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 202.0f);
        s_astrology_spokes[i] = lv_line_create(s_astrology_screen);
        configure_aleth_line(s_astrology_spokes[i], i % 3 == 0 ? 0x76809e : 0x4e586c, i % 3 == 0 ? 2 : 1, 180);
        lv_line_set_points(s_astrology_spokes[i], s_astrology_spoke_points[i], 2);

        s_astrology_signs[i] = make_tarot_label(s_astrology_screen, 0, 34, 0xaeb6cd);
        lv_label_set_text(s_astrology_signs[i], k_lvgl_astro_signs[i]);
        native_obj_hidden(s_astrology_signs[i], true);
        const float la = a + 6.2831853f / 24.0f;
        const int lx = cx + (int)lrintf(cosf(la) * 172.0f);
        const int ly = cy + (int)lrintf(sinf(la) * 172.0f);
        lv_obj_align(s_astrology_signs[i], LV_ALIGN_TOP_LEFT, lx - 17, ly - 7);
        for (int seg = 0; seg < 7; ++seg) {
            s_astrology_glyph_lines[i][seg] = lv_line_create(s_astrology_screen);
            configure_aleth_line(s_astrology_glyph_lines[i][seg], 0xaeb6cd, 2, LV_OPA_COVER);
            native_obj_hidden(s_astrology_glyph_lines[i][seg], true);
        }
    }

    for (int i = 0; i < 7; ++i) {
        s_astrology_bodies[i] = make_circle(s_astrology_screen, i == 0 ? 24 : 20, k_lvgl_astro_bodies[i].color, LV_OPA_COVER);
        lv_obj_set_style_border_width(s_astrology_bodies[i], 2, 0);
        lv_obj_set_style_border_color(s_astrology_bodies[i], lv_color_hex(0x080a12), 0);
        s_astrology_body_labels[i] = make_tarot_label(s_astrology_screen, 0, 28, 0x10131c);
        lv_label_set_text(s_astrology_body_labels[i], k_lvgl_astro_bodies[i].label);
        s_astrology_natal[i] = make_circle(s_astrology_screen, i == 0 ? 10 : 7, i == 0 ? 0x82dcff : 0x5ca8dc, LV_OPA_COVER);
        native_obj_hidden(s_astrology_natal[i], true);
    }

    s_astrology_title = make_tarot_label(s_astrology_screen, 52, 300, 0xe2decc);
    native_obj_hidden(s_astrology_title, true);
    s_astrology_line = make_tarot_label(s_astrology_screen, 366, 360, 0xbec6dc);
    lv_obj_set_style_text_align(s_astrology_line, LV_TEXT_ALIGN_CENTER, 0);
    s_astrology_source = make_tarot_label(s_astrology_screen, 394, 300, 0xbeaa70);
    native_obj_hidden(s_astrology_source, true);
}

static bool draw_astrology(uint32_t anim_ms)
{
    if (s_astrology_screen == NULL) {
        create_astrology_screen();
    }
    if (s_astrology_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_astrology_screen) {
        lv_screen_load(s_astrology_screen);
    }

    float transit_lon[7];
    astrology_local_ephemeris(anim_ms, transit_lon);
    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t natal = {};
    faculty175_chart_positions_t natal_pos = {};
    const bool has_natal = faculty175_charts_primary(&natal) &&
                           faculty175_charts_birth_positions(&natal, &natal_pos);
    const int sun_sign = ((int)(transit_lon[0] / 30.0f)) % 12;

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + ((float)i + 0.5f) * 6.2831853f / 12.0f;
        const int gx = cx + (int)lrintf(cosf(a) * 174.0f);
        const int gy = cy + (int)lrintf(sinf(a) * 174.0f);
        astrology_draw_zodiac_glyph(i, gx, gy, i == sun_sign);
    }

    for (int i = 0; i < 7; ++i) {
        const int r_planet = 132 - (i % 2) * 18;
        int x = 0;
        int y = 0;
        astrology_xy_for_lon(transit_lon[i], r_planet, &x, &y);
        const int sz = i == 0 ? 24 : 20;
        lv_obj_align(s_astrology_bodies[i], LV_ALIGN_TOP_LEFT, x - sz / 2, y - sz / 2);
        lv_obj_align(s_astrology_body_labels[i], LV_ALIGN_TOP_LEFT, x - 14, y - 7);
        lv_obj_set_style_text_color(s_astrology_body_labels[i], lv_color_hex(i == 0 ? 0x291d06 : 0x10131c), 0);

        if (has_natal && i < FACULTY175_CHART_BODY_COUNT) {
            astrology_xy_for_lon(natal_pos.lon[i], 94, &x, &y);
            const int nsz = i == 0 ? 10 : 7;
            lv_obj_align(s_astrology_natal[i], LV_ALIGN_TOP_LEFT, x - nsz / 2, y - nsz / 2);
            native_obj_hidden(s_astrology_natal[i], false);
        } else {
            native_obj_hidden(s_astrology_natal[i], true);
        }
    }

    native_obj_hidden(s_astrology_title, true);
    native_obj_hidden(s_astrology_source, true);
    char line[96];
    if (has_natal) {
        snprintf(line, sizeof(line), "%s  n.Su %s  t.Su %s", natal.name,
                 faculty175_charts_zodiac_abbr(natal_pos.lon[0]),
                 k_lvgl_astro_signs[sun_sign]);
    } else {
        snprintf(line, sizeof(line), "Su %s  Mo %s",
                 k_lvgl_astro_signs[(int)(transit_lon[0] / 30.0f) % 12],
                 k_lvgl_astro_signs[(int)(transit_lon[1] / 30.0f) % 12]);
    }
    lv_label_set_text(s_astrology_line, line);

    lv_obj_invalidate(s_astrology_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

typedef struct {
    int user_body;
    int target_body;
    int aspect_deg;
    double orb;
} lvgl_synastry_aspect_t;

static double synastry_norm360(double v)
{
    v = fmod(v, 360.0);
    if (v < 0.0) {
        v += 360.0;
    }
    return v;
}

static double synastry_aspect_distance(double a, double b)
{
    double d = fabs(synastry_norm360(a) - synastry_norm360(b));
    return d > 180.0 ? 360.0 - d : d;
}

static uint32_t synastry_aspect_color(int deg)
{
    switch (deg) {
        case 0: return 0xffe28e;
        case 60: return 0x78d2fa;
        case 90: return 0xf5645c;
        case 120: return 0x78e29a;
        case 180: return 0xc68af5;
        default: return 0x96a0b8;
    }
}

static const char *synastry_aspect_word(int deg)
{
    switch (deg) {
        case 0: return "conj";
        case 60: return "sext";
        case 90: return "sq";
        case 120: return "tri";
        case 180: return "opp";
        default: return "asp";
    }
}

static int synastry_rebuild_aspects(const faculty175_chart_positions_t *user,
                                    const faculty175_chart_positions_t *target,
                                    lvgl_synastry_aspect_t *out,
                                    int cap)
{
    static const int k_major[] = {0, 60, 90, 120, 180};
    int count = 0;
    for (int ub = 0; ub < FACULTY175_CHART_BODY_COUNT; ++ub) {
        for (int tb = 0; tb < FACULTY175_CHART_BODY_COUNT; ++tb) {
            const double sep = synastry_aspect_distance(user->lon[ub], target->lon[tb]);
            for (size_t ai = 0; ai < sizeof(k_major) / sizeof(k_major[0]); ++ai) {
                const double orb = fabs(sep - (double)k_major[ai]);
                if (orb > 4.5) {
                    continue;
                }
                const lvgl_synastry_aspect_t aspect = {
                    .user_body = ub,
                    .target_body = tb,
                    .aspect_deg = k_major[ai],
                    .orb = orb,
                };
                int ins = count < cap ? count : cap;
                for (int k = 0; k < ins; ++k) {
                    if (aspect.orb < out[k].orb) {
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
                    out[ins] = aspect;
                }
                break;
            }
        }
    }
    return count;
}

static void create_synastry_screen(void)
{
    s_synastry_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_synastry_screen);
    lv_obj_set_size(s_synastry_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_synastry_screen, lv_color_hex(0x070912), 0);
    lv_obj_set_style_bg_opa(s_synastry_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_synastry_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int ring_sizes[] = {432, 408, 344, 244, 144};
    for (size_t i = 0; i < sizeof(ring_sizes) / sizeof(ring_sizes[0]); ++i) {
        s_synastry_rings[i] =
            make_arc_ring(s_synastry_screen, ring_sizes[i], i == 4 ? 0x564870 : 0x343a52, i == 0 ? 2 : 1,
                          i == 4 ? 220 : 170);
        lv_obj_center(s_synastry_rings[i]);
    }

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 4;
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        s_synastry_spoke_points[i][0].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 72.0f);
        s_synastry_spoke_points[i][0].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 72.0f);
        s_synastry_spoke_points[i][1].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 204.0f);
        s_synastry_spoke_points[i][1].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 204.0f);
        s_synastry_spokes[i] = lv_line_create(s_synastry_screen);
        configure_aleth_line(s_synastry_spokes[i], i % 3 == 0 ? 0x647090 : 0x3e4862, i % 3 == 0 ? 2 : 1, 160);
        lv_line_set_points(s_synastry_spokes[i], s_synastry_spoke_points[i], 2);
    }

    for (int i = 0; i < 8; ++i) {
        s_synastry_aspects[i] = lv_line_create(s_synastry_screen);
        configure_aleth_line(s_synastry_aspects[i], 0x96a0b8, 2, 190);
        native_obj_hidden(s_synastry_aspects[i], true);
    }

    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        s_synastry_target_bodies[i] = make_circle(s_synastry_screen, i == 0 ? 16 : 12, 0xecccff, LV_OPA_COVER);
        s_synastry_user_bodies[i] = make_circle(s_synastry_screen, i == 0 ? 16 : 12, 0x74d0ff, LV_OPA_COVER);
        s_synastry_target_labels[i] = make_tarot_label(s_synastry_screen, 0, 28, 0xecccff);
        s_synastry_user_labels[i] = make_tarot_label(s_synastry_screen, 0, 28, 0x74d0ff);
        lv_label_set_text(s_synastry_target_labels[i], faculty175_charts_body_label(i));
        lv_label_set_text(s_synastry_user_labels[i], faculty175_charts_body_label(i));
    }

    lv_obj_t *hub = make_circle(s_synastry_screen, 72, 0x090a14, LV_OPA_COVER);
    lv_obj_center(hub);

    s_synastry_title = make_tarot_label(s_synastry_screen, 48, 300, 0xe6e4f6);
    s_synastry_names = make_tarot_label(s_synastry_screen, 74, 360, 0xb2bcda);
    s_synastry_line = make_tarot_label(s_synastry_screen, 392, 360, 0xc4cce2);
}

static bool draw_synastry(uint32_t anim_ms)
{
    (void)anim_ms;
    if (s_synastry_screen == NULL) {
        create_synastry_screen();
    }
    if (s_synastry_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_synastry_screen) {
        lv_screen_load(s_synastry_screen);
    }

    faculty175_charts_ensure_family_seed();
    faculty175_birth_chart_t user = {};
    faculty175_birth_chart_t target = {};
    faculty175_chart_positions_t user_pos = {};
    faculty175_chart_positions_t target_pos = {};
    const bool ready = faculty175_charts_primary(&user) && faculty175_charts_active(&target) &&
                       faculty175_charts_birth_positions(&user, &user_pos) &&
                       faculty175_charts_birth_positions(&target, &target_pos);
    if (!ready) {
        lv_label_set_text(s_synastry_title, "SYNASTRY");
        lv_label_set_text(s_synastry_names, "CHART DATA NEEDED");
        lv_label_set_text(s_synastry_line, "serial: charts seed");
        for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
            native_obj_hidden(s_synastry_target_bodies[i], true);
            native_obj_hidden(s_synastry_user_bodies[i], true);
            native_obj_hidden(s_synastry_target_labels[i], true);
            native_obj_hidden(s_synastry_user_labels[i], true);
        }
        for (int i = 0; i < 8; ++i) {
            native_obj_hidden(s_synastry_aspects[i], true);
        }
        lvgl_tick(16);
        lv_timer_handler();
        return true;
    }

    lvgl_synastry_aspect_t aspects[12] = {};
    const int aspect_count = synastry_rebuild_aspects(&user_pos, &target_pos, aspects, 12);
    const int r_user = 104;
    const int r_target = 152;
    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        int x = 0;
        int y = 0;
        astrology_xy_for_lon(target_pos.lon[i], r_target, &x, &y);
        const int target_sz = i == 0 ? 16 : 12;
        lv_obj_align(s_synastry_target_bodies[i], LV_ALIGN_TOP_LEFT, x - target_sz / 2, y - target_sz / 2);
        int lx = 0;
        int ly = 0;
        astrology_xy_for_lon(target_pos.lon[i], r_target + 19, &lx, &ly);
        lv_obj_align(s_synastry_target_labels[i], LV_ALIGN_TOP_LEFT, lx - 14, ly - 7);
        native_obj_hidden(s_synastry_target_bodies[i], false);
        native_obj_hidden(s_synastry_target_labels[i], false);

        astrology_xy_for_lon(user_pos.lon[i], r_user, &x, &y);
        const int user_sz = i == 0 ? 16 : 12;
        lv_obj_align(s_synastry_user_bodies[i], LV_ALIGN_TOP_LEFT, x - user_sz / 2, y - user_sz / 2);
        astrology_xy_for_lon(user_pos.lon[i], r_user - 24, &lx, &ly);
        lv_obj_align(s_synastry_user_labels[i], LV_ALIGN_TOP_LEFT, lx - 14, ly - 7);
        native_obj_hidden(s_synastry_user_bodies[i], false);
        native_obj_hidden(s_synastry_user_labels[i], false);
    }

    for (int i = 0; i < 8; ++i) {
        if (i >= aspect_count) {
            native_obj_hidden(s_synastry_aspects[i], true);
            continue;
        }
        const lvgl_synastry_aspect_t *a = &aspects[i];
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        astrology_xy_for_lon(user_pos.lon[a->user_body], r_user, &x0, &y0);
        astrology_xy_for_lon(target_pos.lon[a->target_body], r_target, &x1, &y1);
        s_synastry_aspect_points[i][0].x = x0;
        s_synastry_aspect_points[i][0].y = y0;
        s_synastry_aspect_points[i][1].x = x1;
        s_synastry_aspect_points[i][1].y = y1;
        lv_line_set_points(s_synastry_aspects[i], s_synastry_aspect_points[i], 2);
        lv_obj_set_style_line_color(s_synastry_aspects[i], lv_color_hex(synastry_aspect_color(a->aspect_deg)), 0);
        lv_obj_set_style_line_opa(s_synastry_aspects[i], i < 4 ? 210 : 150, 0);
        native_obj_hidden(s_synastry_aspects[i], false);
    }

    lv_label_set_text(s_synastry_title, "SYNASTRY");
    char names[80];
    snprintf(names, sizeof(names), "%s + %s", user.name, target.name);
    almanac_set_trimmed(s_synastry_names, names, 42);
    char line[96];
    if (aspect_count > 0) {
        const lvgl_synastry_aspect_t *a = &aspects[0];
        snprintf(line, sizeof(line), "%s %s %s  orb %.1f",
                 faculty175_charts_body_label(a->user_body),
                 synastry_aspect_word(a->aspect_deg),
                 faculty175_charts_body_label(a->target_body),
                 a->orb);
    } else {
        snprintf(line, sizeof(line), "%s %s  %s %s",
                 user.name,
                 faculty175_charts_zodiac_abbr(user_pos.lon[0]),
                 target.name,
                 faculty175_charts_zodiac_abbr(target_pos.lon[0]));
    }
    almanac_set_trimmed(s_synastry_line, line, 44);

    lv_obj_invalidate(s_synastry_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void transits_ephemeris_at(uint32_t anim_ms, uint32_t day_offset, float lon[7])
{
    float days = (float)astrology_days_since_j2000(anim_ms) + (float)day_offset;
    if (!astrolabe_time_valid()) {
        days += (float)(anim_ms % 86400000u) / 86400000.0f;
    } else {
        const time_t now = astrolabe_time_now();
        days += (float)(now % 86400) / 86400.0f;
    }
    for (size_t i = 0; i < 7; ++i) {
        lon[i] = astrology_wrap360(k_lvgl_astro_bodies[i].base_lon + days * k_lvgl_astro_bodies[i].deg_per_day);
    }
}

static int transits_day_of_year(uint32_t anim_ms, int *days_in_year)
{
    time_t now = astrolabe_time_valid() ? astrolabe_time_now() : time(NULL);
    struct tm local_now;
    if (localtime_r(&now, &local_now) == NULL) {
        local_now.tm_yday = 0;
        local_now.tm_year = 124;
    }
    const int year = local_now.tm_year + 1900;
    const bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (days_in_year != NULL) {
        *days_in_year = leap ? 366 : 365;
    }
    (void)anim_ms;
    return local_now.tm_yday;
}

static float transit_year_angle(int day, int days_in_year)
{
    float angle = 270.0f + 360.0f * (float)day / (float)days_in_year;
    while (angle < 0.0f) {
        angle += 360.0f;
    }
    while (angle >= 360.0f) {
        angle -= 360.0f;
    }
    return angle;
}

static void create_transits_screen(void)
{
    s_transits_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_transits_screen);
    lv_obj_set_size(s_transits_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_transits_screen, lv_color_hex(0x050914), 0);
    lv_obj_set_style_bg_opa(s_transits_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_transits_screen, LV_OBJ_FLAG_SCROLLABLE);

    const int ring_sizes[] = {430, 386, 282, 154};
    for (size_t i = 0; i < sizeof(ring_sizes) / sizeof(ring_sizes[0]); ++i) {
        s_transits_rings[i] =
            make_arc_ring(s_transits_screen, ring_sizes[i], i == 3 ? 0x234a5c : 0x2c3a56, i == 0 ? 2 : 1,
                          i == 3 ? 220 : 160);
        lv_obj_center(s_transits_rings[i]);
    }

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    for (int i = 0; i < 12; ++i) {
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        s_transits_spoke_points[i][0].x = cx + (lv_value_precise_t)lrintf(cosf(a) * 201.0f);
        s_transits_spoke_points[i][0].y = cy + (lv_value_precise_t)lrintf(sinf(a) * 201.0f);
        s_transits_spoke_points[i][1].x = cx + (lv_value_precise_t)lrintf(cosf(a) * (i % 3 == 0 ? 216.0f : 210.0f));
        s_transits_spoke_points[i][1].y = cy + (lv_value_precise_t)lrintf(sinf(a) * (i % 3 == 0 ? 216.0f : 210.0f));
        s_transits_spokes[i] = lv_line_create(s_transits_screen);
        configure_aleth_line(s_transits_spokes[i], i % 3 == 0 ? 0x8aa9c4 : 0x43556b, i % 3 == 0 ? 2 : 1, 190);
        lv_line_set_points(s_transits_spokes[i], s_transits_spoke_points[i], 2);
    }

    static const uint32_t transit_lane_colors[4] = {0xffd25a, 0xb2b6c4, 0xffbe8c, 0xe65a46};
    static const int transit_lane_sizes[4] = {414, 394, 374, 354};
    for (int i = 0; i < 4; ++i) {
        s_transits_year_arcs[i] = make_transit_year_arc(s_transits_screen, transit_lane_sizes[i],
                                                        transit_lane_colors[i], i == 0 ? 7 : 5);
    }

    s_transits_year_hand = lv_line_create(s_transits_screen);
    configure_aleth_line(s_transits_year_hand, 0xdcecff, 3, LV_OPA_COVER);
    s_transits_year_today = make_circle(s_transits_screen, 12, 0xdcecff, LV_OPA_COVER);

    static const char *const month_labels[12] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    for (int i = 0; i < 12; ++i) {
        s_transits_month_labels[i] = make_tarot_label(s_transits_screen, 0, 18, 0x9cb2c9);
        lv_label_set_text(s_transits_month_labels[i], month_labels[i]);
        const float a = -1.5707963f + (float)i * 6.2831853f / 12.0f;
        const int lx = cx + (int)lrintf(cosf(a) * 184.0f);
        const int ly = cy + (int)lrintf(sinf(a) * 184.0f);
        lv_obj_align(s_transits_month_labels[i], LV_ALIGN_TOP_LEFT, lx - 14, ly - 9);
    }

    for (int i = 0; i < 7; ++i) {
        s_transits_motion[i] = lv_line_create(s_transits_screen);
        configure_aleth_line(s_transits_motion[i], k_lvgl_astro_bodies[i].color, 2, 165);
        s_transits_next[i] = make_circle(s_transits_screen, i == 0 ? 13 : 10, k_lvgl_astro_bodies[i].color, 130);
        s_transits_now[i] = make_circle(s_transits_screen, i == 0 ? 20 : 16, k_lvgl_astro_bodies[i].color, LV_OPA_COVER);
        s_transits_labels[i] = make_tarot_label(s_transits_screen, 0, 28, i == 0 ? 0x211706 : 0x071019);
        lv_label_set_text(s_transits_labels[i], k_lvgl_astro_bodies[i].label);
        native_obj_hidden(s_transits_motion[i], true);
        native_obj_hidden(s_transits_next[i], true);
        native_obj_hidden(s_transits_now[i], true);
        native_obj_hidden(s_transits_labels[i], true);
    }

    lv_obj_t *hub = make_circle(s_transits_screen, 76, 0x07111f, LV_OPA_COVER);
    lv_obj_center(hub);

    s_transits_title = make_tarot_label(s_transits_screen, 50, 300, 0xdcecff);
    s_transits_line = make_tarot_label(s_transits_screen, 372, 360, 0xb8c8de);
    s_transits_clock = make_tarot_label(s_transits_screen, 400, 320, 0x78d2fa);
}

static bool draw_transits(uint32_t anim_ms)
{
    if (s_transits_screen == NULL) {
        create_transits_screen();
    }
    if (s_transits_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_transits_screen) {
        lv_screen_load(s_transits_screen);
    }

    float now_lon[7];
    float next_lon[7];
    transits_ephemeris_at(anim_ms, 0, now_lon);
    transits_ephemeris_at(anim_ms, 4, next_lon);

    for (int i = 0; i < 7; ++i) {
        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        const int r_now = 128 - (i % 2) * 16;
        const int r_next = 174 - (i % 2) * 12;
        astrology_xy_for_lon(now_lon[i], r_now, &x0, &y0);
        astrology_xy_for_lon(next_lon[i], r_next, &x1, &y1);

        s_transits_motion_points[i][0].x = x0;
        s_transits_motion_points[i][0].y = y0;
        s_transits_motion_points[i][1].x = x1;
        s_transits_motion_points[i][1].y = y1;
        lv_line_set_points(s_transits_motion[i], s_transits_motion_points[i], 2);
        lv_obj_set_style_line_opa(s_transits_motion[i], i == 1 ? 225 : 155, 0);

        const int next_sz = i == 0 ? 13 : 10;
        lv_obj_align(s_transits_next[i], LV_ALIGN_TOP_LEFT, x1 - next_sz / 2, y1 - next_sz / 2);
        const int now_sz = i == 0 ? 20 : 16;
        lv_obj_align(s_transits_now[i], LV_ALIGN_TOP_LEFT, x0 - now_sz / 2, y0 - now_sz / 2);
        lv_obj_align(s_transits_labels[i], LV_ALIGN_TOP_LEFT, x0 - 14, y0 - 7);
    }

    /* Map the calendar year clockwise around the bezel.  Each lane marks the
     * next sign-ingress window for a fast or personally legible planet; the
     * hand makes “where am I?” immediately obvious without tiny glyphs. */
    int days_in_year = 365;
    const int day_of_year = transits_day_of_year(anim_ms, &days_in_year);
    const int lane_body[4] = {0, 2, 3, 4}; /* Sun, Mercury, Venus, Mars */
    const int lane_window_days[4] = {4, 10, 16, 28};
    for (int lane = 0; lane < 4; ++lane) {
        const int body = lane_body[lane];
        const float speed = fmaxf(fabsf(k_lvgl_astro_bodies[body].deg_per_day), 0.01f);
        const float lon = now_lon[body];
        const float boundary = ceilf((lon + 0.05f) / 30.0f) * 30.0f;
        float days_to_ingress = astrology_wrap360(boundary - lon) / speed;
        if (days_to_ingress < 0.25f) {
            days_to_ingress += 30.0f / speed;
        }
        const float center_day = fmodf((float)day_of_year + days_to_ingress, (float)days_in_year);
        const float start_day = center_day - (float)lane_window_days[lane] * 0.5f;
        float start_angle = transit_year_angle((int)lrintf(start_day), days_in_year);
        const int sweep = (int)lrintf((float)lane_window_days[lane] * 360.0f / (float)days_in_year);
        lv_arc_set_rotation(s_transits_year_arcs[lane], (uint16_t)lrintf(start_angle));
        lv_arc_set_value(s_transits_year_arcs[lane], sweep > 1 ? sweep : 1);
    }

    const float today_angle = transit_year_angle(day_of_year, days_in_year) * 0.0174532925f;
    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 2;
    s_transits_year_hand_points[0].x = cx;
    s_transits_year_hand_points[0].y = cy;
    s_transits_year_hand_points[1].x = cx + (lv_value_precise_t)lrintf(cosf(today_angle) * 207.0f);
    s_transits_year_hand_points[1].y = cy + (lv_value_precise_t)lrintf(sinf(today_angle) * 207.0f);
    lv_line_set_points(s_transits_year_hand, s_transits_year_hand_points, 2);
    lv_obj_align(s_transits_year_today, LV_ALIGN_TOP_LEFT,
                 s_transits_year_hand_points[1].x - 6, s_transits_year_hand_points[1].y - 6);

    lv_label_set_text(s_transits_title, "TRANSITS / YEAR");
    char line[96];
    const float moon_delta = astrology_wrap360(next_lon[1] - now_lon[1]);
    snprintf(line, sizeof(line), "DAY %03d  Mo %s -> %s  +%.0f deg / 4d",
             day_of_year + 1,
             k_lvgl_astro_signs[(int)(now_lon[1] / 30.0f) % 12],
             k_lvgl_astro_signs[(int)(next_lon[1] / 30.0f) % 12],
             moon_delta);
    lv_label_set_text(s_transits_line, line);

    char clock[48];
    scale_format_time_line(clock, sizeof(clock));
    lv_label_set_text(s_transits_clock, clock);

    lv_obj_invalidate(s_transits_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

static void create_native_screen(void)
{
    s_native_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_native_screen);
    lv_obj_set_size(s_native_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_native_screen, lv_color_hex(0x04060b), 0);
    lv_obj_set_style_bg_opa(s_native_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_native_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_native_title = make_native_label(s_native_screen, 50, 360, 0xf0e8cc, LV_TEXT_ALIGN_CENTER);
    s_native_subtitle = make_native_label(s_native_screen, 76, 340, 0x8ea4b8, LV_TEXT_ALIGN_CENTER);
    s_native_category = make_native_label(s_native_screen, 404, 340, 0x66788e, LV_TEXT_ALIGN_CENTER);
    s_native_primary = make_native_label(s_native_screen, 216, 280, 0xf7efd8, LV_TEXT_ALIGN_CENTER);
    s_native_line_a = make_native_label(s_native_screen, 326, 330, 0xe0d6b8, LV_TEXT_ALIGN_CENTER);
    s_native_line_b = make_native_label(s_native_screen, 352, 330, 0x9eb4c6, LV_TEXT_ALIGN_CENTER);
    s_native_line_c = make_native_label(s_native_screen, 378, 330, 0x7d90a4, LV_TEXT_ALIGN_CENTER);

    s_native_arc = lv_arc_create(s_native_screen);
    lv_obj_remove_style(s_native_arc, NULL, LV_PART_KNOB);
    lv_obj_set_size(s_native_arc, 244, 244);
    lv_obj_center(s_native_arc);
    lv_arc_set_range(s_native_arc, 0, 100);
    lv_arc_set_bg_angles(s_native_arc, 0, 360);
    lv_arc_set_rotation(s_native_arc, 270);
    lv_obj_clear_flag(s_native_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_native_arc, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_native_arc, 7, LV_PART_INDICATOR);

    for (size_t i = 0; i < sizeof(s_native_orbit) / sizeof(s_native_orbit[0]); ++i) {
        s_native_orbit[i] = lv_obj_create(s_native_screen);
        lv_obj_remove_style_all(s_native_orbit[i]);
        lv_obj_set_size(s_native_orbit[i], 14, 14);
        lv_obj_set_style_radius(s_native_orbit[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_native_orbit[i], LV_OPA_COVER, 0);
    }
    for (size_t i = 0; i < sizeof(s_native_bars) / sizeof(s_native_bars[0]); ++i) {
        s_native_bars[i] = lv_obj_create(s_native_screen);
        lv_obj_remove_style_all(s_native_bars[i]);
        lv_obj_set_style_radius(s_native_bars[i], 3, 0);
        lv_obj_set_style_bg_opa(s_native_bars[i], 210, 0);
    }
    for (size_t i = 0; i < sizeof(s_native_cards) / sizeof(s_native_cards[0]); ++i) {
        s_native_cards[i] = lv_obj_create(s_native_screen);
        lv_obj_remove_style_all(s_native_cards[i]);
        lv_obj_set_size(s_native_cards[i], 44, 72);
        lv_obj_set_style_radius(s_native_cards[i], 4, 0);
        lv_obj_set_style_bg_opa(s_native_cards[i], 72, 0);
        lv_obj_set_style_border_width(s_native_cards[i], 1, 0);
        lv_obj_set_style_border_opa(s_native_cards[i], 170, 0);
    }
}

static void update_native_body(const faculty175_native_face_t *face, uint32_t anim_ms, uint32_t accent)
{
    const uint32_t dim = 0x34404c;
    lv_obj_set_style_arc_color(s_native_arc, lv_color_hex(dim), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(s_native_arc, 180, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_native_arc, lv_color_hex(accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(s_native_arc, 230, LV_PART_INDICATOR);
    lv_arc_set_value(s_native_arc, (int32_t)((anim_ms / 28u) % 100u));
    native_obj_hidden(s_native_arc, false);

    for (size_t i = 0; i < sizeof(s_native_orbit) / sizeof(s_native_orbit[0]); ++i) {
        native_obj_hidden(s_native_orbit[i], false);
        const float a = ((float)i / 8.0f) * 6.28318530718f + (float)(anim_ms % 5000u) / 5000.0f;
        const int r = face->style == FACULTY175_NATIVE_RADAR ? 120 : 94;
        lv_obj_set_pos(s_native_orbit[i],
                       (FACULTY175_LCD_W / 2) + (int32_t)(cosf(a) * (float)r) - 7,
                       (FACULTY175_LCD_H / 2) + (int32_t)(sinf(a) * (float)r) - 7);
        lv_obj_set_style_bg_color(s_native_orbit[i], lv_color_hex(native_hue_color(face->hue + (uint8_t)i, 20)), 0);
        lv_obj_set_style_bg_opa(s_native_orbit[i], 120 + (lv_opa_t)((i % 3) * 42), 0);
    }
    for (size_t i = 0; i < sizeof(s_native_bars) / sizeof(s_native_bars[0]); ++i) {
        native_obj_hidden(s_native_bars[i], face->style != FACULTY175_NATIVE_INSTRUMENT &&
                                            face->style != FACULTY175_NATIVE_STATUS);
        const int h = 26 + (int)((anim_ms / 90u + i * 13u) % 82u);
        lv_obj_set_size(s_native_bars[i], 14, h);
        lv_obj_align(s_native_bars[i], LV_ALIGN_CENTER, (int32_t)i * 26 - 78, 70 - h / 2);
        lv_obj_set_style_bg_color(s_native_bars[i], lv_color_hex(native_hue_color(face->hue + (uint8_t)i, 0)), 0);
    }
    for (size_t i = 0; i < sizeof(s_native_cards) / sizeof(s_native_cards[0]); ++i) {
        const bool cards = face->style == FACULTY175_NATIVE_ORACLE || face->id == FACULTY175_FACE_INQ ||
                           face->id == FACULTY175_FACE_GEOMANCY;
        native_obj_hidden(s_native_cards[i], !cards);
        lv_obj_align(s_native_cards[i], LV_ALIGN_CENTER, (int32_t)i * 34 - 68, 30 + (i % 2 ? 8 : -8));
        lv_obj_set_style_bg_color(s_native_cards[i], lv_color_hex(0x121824), 0);
        lv_obj_set_style_border_color(s_native_cards[i],
                                      lv_color_hex(native_hue_color(face->hue + (uint8_t)i, 30)), 0);
    }

    switch (face->style) {
        case FACULTY175_NATIVE_ANALOG:
            lv_label_set_text(s_native_primary, "12H");
            break;
        case FACULTY175_NATIVE_DIGITAL: {
            char line[24];
            const uint32_t day_s = watch_seconds_of_day(anim_ms, NULL);
            snprintf(line, sizeof(line), "%02u:%02u", (unsigned)(day_s / 3600u) % 24u,
                     (unsigned)(day_s / 60u) % 60u);
            lv_label_set_text(s_native_primary, line);
            break;
        }
        case FACULTY175_NATIVE_INSTRUMENT:
            lv_label_set_text(s_native_primary, "PLAY");
            break;
        case FACULTY175_NATIVE_CELESTIAL:
            lv_label_set_text(s_native_primary, "ORBITS");
            break;
        case FACULTY175_NATIVE_RADAR:
            lv_label_set_text(s_native_primary, "SCAN");
            break;
        case FACULTY175_NATIVE_STATUS:
            lv_label_set_text(s_native_primary, "STATUS");
            break;
        case FACULTY175_NATIVE_TEXT:
            lv_label_set_text(s_native_primary, face->a != NULL ? face->a : "TEXT");
            break;
        case FACULTY175_NATIVE_ORACLE:
        default:
            lv_label_set_text(s_native_primary, "ORACLE");
            break;
    }
}

static void watch_tint_for_hour(float hour, uint32_t *color, lv_opa_t *opa)
{
    if (hour < 5.5f || hour >= 21.0f) {
        *color = 0x122a68;
        *opa = 84;
    } else if (hour < 8.0f) {
        *color = 0xf69a4e;
        *opa = 62;
    } else if (hour < 17.0f) {
        *color = 0xffefbc;
        *opa = 42;
    } else if (hour < 20.0f) {
        *color = 0xec7a3e;
        *opa = 78;
    } else {
        *color = 0x223e84;
        *opa = 70;
    }
}

static uint32_t watch_seconds_of_day(uint32_t anim_ms, bool *time_valid)
{
    const int64_t mono_ms = esp_timer_get_time() / 1000;
    if (astrolabe_time_valid()) {
        const time_t now = astrolabe_time_now();
        struct tm local = {};
        localtime_r(&now, &local);
        const uint32_t wall_day_s =
            (uint32_t)local.tm_hour * 3600u + (uint32_t)local.tm_min * 60u + (uint32_t)local.tm_sec;
        if (time_valid != NULL) {
            *time_valid = true;
        }
        const uint32_t predicted =
            s_watch_time_base_valid
                ? (s_watch_time_base_day_s + (uint32_t)((mono_ms - s_watch_time_base_ms) / 1000)) % 86400u
                : wall_day_s;
        const int32_t drift = (int32_t)predicted - (int32_t)wall_day_s;
        if (!s_watch_time_base_valid || !s_watch_time_base_wall_valid || drift > 1 || drift < -1) {
            s_watch_time_base_valid = true;
            s_watch_time_base_wall_valid = true;
            s_watch_time_base_day_s = wall_day_s;
            s_watch_time_base_ms = mono_ms;
            return wall_day_s;
        }
        return predicted;
    }
    if (time_valid != NULL) {
        *time_valid = false;
    }
    if (!s_watch_time_base_valid || s_watch_time_base_wall_valid) {
        s_watch_time_base_valid = true;
        s_watch_time_base_wall_valid = false;
        s_watch_time_base_day_s = (anim_ms / 1000u) % 86400u;
        s_watch_time_base_ms = mono_ms;
    }
    return (s_watch_time_base_day_s + (uint32_t)((mono_ms - s_watch_time_base_ms) / 1000)) % 86400u;
}

static int32_t watch_tick_boundary_late_ms(uint32_t day_s)
{
    if (!s_watch_time_base_valid) {
        return 0;
    }
    const uint32_t delta_s = (day_s + 86400u - s_watch_time_base_day_s) % 86400u;
    if (delta_s > 43200u) {
        return 0;
    }
    const int64_t mono_ms = esp_timer_get_time() / 1000;
    const int64_t expected_ms = s_watch_time_base_ms + (int64_t)delta_s * 1000;
    const int64_t late_ms = mono_ms - expected_ms;
    if (late_ms > INT32_MAX) {
        return INT32_MAX;
    }
    if (late_ms < INT32_MIN) {
        return INT32_MIN;
    }
    return (int32_t)late_ms;
}

static void create_watch_screen(void)
{
    s_watch_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_watch_screen);
    lv_obj_set_size(s_watch_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(s_watch_screen, lv_color_hex(0x07090d), 0);
    lv_obj_set_style_bg_opa(s_watch_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_watch_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bg = lv_image_create(s_watch_screen);
    if (ensure_watch_bg()) {
        lv_image_set_src(bg, &s_watch_bg);
        lv_obj_align(bg, LV_ALIGN_CENTER, 0, 0);
    } else {
        lv_obj_set_size(bg, FACULTY175_LCD_W, FACULTY175_LCD_H);
        lv_obj_center(bg);
    }

    lv_obj_t *shade = lv_obj_create(s_watch_screen);
    lv_obj_remove_style_all(shade);
    lv_obj_set_size(shade, 466, 466);
    lv_obj_center(shade);
    lv_obj_set_style_radius(shade, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(shade, lv_color_hex(0x05070a), 0);
    lv_obj_set_style_bg_opa(shade, 42, 0);

    s_watch_tint = lv_obj_create(s_watch_screen);
    lv_obj_remove_style_all(s_watch_tint);
    lv_obj_set_size(s_watch_tint, 466, 466);
    lv_obj_center(s_watch_tint);
    lv_obj_set_style_radius(s_watch_tint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_watch_tint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_watch_tint, LV_OPA_TRANSP, 0);

    lv_obj_t *dial_glass = lv_obj_create(s_watch_screen);
    lv_obj_remove_style_all(dial_glass);
    lv_obj_set_size(dial_glass, 386, 386);
    lv_obj_center(dial_glass);
    lv_obj_set_style_radius(dial_glass, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dial_glass, 2, 0);
    lv_obj_set_style_border_color(dial_glass, lv_color_hex(0xfee8a3), 0);
    lv_obj_set_style_border_opa(dial_glass, 112, 0);
    lv_obj_set_style_bg_color(dial_glass, lv_color_hex(0x0c141b), 0);
    lv_obj_set_style_bg_opa(dial_glass, 28, 0);

    s_watch_hour_line = lv_line_create(s_watch_screen);
    s_watch_minute_line = lv_line_create(s_watch_screen);
    s_watch_second_line = lv_line_create(s_watch_screen);
    s_watch_hour_tail = lv_line_create(s_watch_screen);
    s_watch_minute_tail = lv_line_create(s_watch_screen);
    s_watch_hour_accent = lv_line_create(s_watch_screen);
    s_watch_minute_accent = lv_line_create(s_watch_screen);
    configure_watch_line(s_watch_hour_line, 16, 0x47351e);
    configure_watch_line(s_watch_minute_line, 11, 0x4a3c25);
    configure_watch_line(s_watch_second_line, 2, 0xf4b84b);
    configure_watch_line(s_watch_hour_tail, 8, 0x47351e);
    configure_watch_line(s_watch_minute_tail, 6, 0x4a3c25);
    configure_watch_line(s_watch_hour_accent, 4, 0xfff2c4);
    configure_watch_line(s_watch_minute_accent, 3, 0xd9ecf0);

    lv_obj_t *cap_shadow = lv_obj_create(s_watch_screen);
    lv_obj_remove_style_all(cap_shadow);
    lv_obj_set_size(cap_shadow, 36, 36);
    lv_obj_center(cap_shadow);
    lv_obj_set_style_radius(cap_shadow, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap_shadow, lv_color_hex(0x2e2114), 0);
    lv_obj_set_style_bg_opa(cap_shadow, LV_OPA_COVER, 0);

    lv_obj_t *cap = lv_obj_create(s_watch_screen);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 24, 24);
    lv_obj_center(cap);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap, lv_color_hex(0xffdd84), 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cap, 3, 0);
    lv_obj_set_style_border_color(cap, lv_color_hex(0x6f5128), 0);
    lv_obj_set_style_border_opa(cap, 220, 0);
    s_watch_rendered_day_s = UINT32_MAX;
    s_watch_rendered_minute = UINT32_MAX;
    s_watch_rendered_tint_color = UINT32_MAX;
    s_watch_rendered_tint_opa = 0xff;
    s_watch_created = true;
}

esp_err_t faculty175_lvgl_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    lv_init();
    s_display = lv_display_create(FACULTY175_LCD_W, FACULTY175_LCD_H);
    if (s_display == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);

    size_t buf_size = (size_t)FACULTY175_LCD_W * LVGL_DRAW_BUF_ROWS * sizeof(uint16_t);
    s_draw_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_draw_buf_2 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_draw_buf == NULL) {
        if (s_draw_buf_2 != NULL) {
            heap_caps_free(s_draw_buf_2);
            s_draw_buf_2 = NULL;
        }
        buf_size = (size_t)FACULTY175_LCD_W * 64u * sizeof(uint16_t);
        s_draw_buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_draw_buf == NULL) {
        s_draw_buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_draw_buf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (s_draw_buf_2 == NULL && buf_size == (size_t)FACULTY175_LCD_W * LVGL_DRAW_BUF_ROWS * sizeof(uint16_t)) {
        ESP_LOGW(TAG, "lvgl second full-screen draw buffer unavailable; using single buffer");
    }
    lv_display_set_buffers(s_display, s_draw_buf, s_draw_buf_2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    ESP_LOGI(TAG,
             "lvgl draw buffers rows=%u bytes=%u double=%d psram_free=%u",
             (unsigned)(buf_size / ((size_t)FACULTY175_LCD_W * sizeof(uint16_t))),
             (unsigned)buf_size,
             s_draw_buf_2 != NULL ? 1 : 0,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    lv_display_set_flush_cb(s_display, display_flush);

    if (faculty175_touch_ready()) {
        s_touch_indev = lv_indev_create();
        if (s_touch_indev != NULL) {
            lv_indev_set_type(s_touch_indev, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(s_touch_indev, s_display);
            lv_indev_set_read_cb(s_touch_indev, touch_read_cb);
            lv_indev_set_long_press_time(s_touch_indev, 650);
            lv_indev_set_scroll_limit(s_touch_indev, 8);
            lv_indev_set_gesture_min_distance(s_touch_indev, 18);
            lv_indev_set_gesture_min_velocity(s_touch_indev, 2);
        }
    }

    s_ready = true;
    ESP_LOGI(TAG, "LVGL ready (%dx%d RGB565 touch=%s)",
             FACULTY175_LCD_W,
             FACULTY175_LCD_H,
             s_touch_indev != NULL ? "yes" : "no");
    return ESP_OK;
}

bool faculty175_lvgl_ready(void)
{
    return s_ready;
}

bool faculty175_lvgl_face_supported(faculty175_face_id_t id)
{
    switch (id) {
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_FACULTY:
        case FACULTY175_FACE_APOCALYPSO:
        case FACULTY175_FACE_DIGITAL:
        case FACULTY175_FACE_SPOTIFY:
        case FACULTY175_FACE_NOTES:
        case FACULTY175_FACE_MOON:
        case FACULTY175_FACE_CALCIFER:
        case FACULTY175_FACE_CASTALIA:
        case FACULTY175_FACE_ASTROLOGY:
        case FACULTY175_FACE_SYNASTRY:
        case FACULTY175_FACE_TAROT:
        case FACULTY175_FACE_INQ:
        case FACULTY175_FACE_RUNES:
        case FACULTY175_FACE_ALETHIOMETER:
        case FACULTY175_FACE_SPECTRUM:
        case FACULTY175_FACE_CHAKRA:
        case FACULTY175_FACE_BOWL:
        case FACULTY175_FACE_ROCKET:
        case FACULTY175_FACE_RADAR:
        case FACULTY175_FACE_WEATHER:
        case FACULTY175_FACE_GLOBE:
        case FACULTY175_FACE_SCALE:
        case FACULTY175_FACE_SKY:
        case FACULTY175_FACE_TRANSITS:
        case FACULTY175_FACE_OCARINA:
        case FACULTY175_FACE_PITCH:
        case FACULTY175_FACE_BONGO:
        case FACULTY175_FACE_PIANO:
        case FACULTY175_FACE_KALIMBA:
        case FACULTY175_FACE_DRONE:
        case FACULTY175_FACE_CHORD:
        case FACULTY175_FACE_LEVEL:
        case FACULTY175_FACE_TUNING:
        case FACULTY175_FACE_PANDRUM:
        case FACULTY175_FACE_ORIENT:
        case FACULTY175_FACE_QDAY:
        case FACULTY175_FACE_FOCUS:
        case FACULTY175_FACE_BIOMETRICS:
        case FACULTY175_FACE_WATCHER:
        case FACULTY175_FACE_LENORMAND:
        case FACULTY175_FACE_GEOMANCY:
        case FACULTY175_FACE_ENOCHIAN:
        case FACULTY175_FACE_HID:
        case FACULTY175_FACE_WSCAN:
        case FACULTY175_FACE_DEAUTH:
        case FACULTY175_FACE_EVILTWIN:
        case FACULTY175_FACE_HANDSHAKE:
        case FACULTY175_FACE_INCIDENTS:
        case FACULTY175_FACE_BABEL:
        case FACULTY175_FACE_SOLAR:
        case FACULTY175_FACE_MAGNETOSPHERE:
        case FACULTY175_FACE_QUOTES:
        case FACULTY175_FACE_SETTINGS:
        case FACULTY175_FACE_POCKETWATCH:
            return true;
        default:
            return false;
    }
}

bool faculty175_lvgl_draw_native_face(const faculty175_native_face_t *face, uint32_t anim_ms)
{
    if (face == NULL) {
        return false;
    }
    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }
    if (s_native_screen == NULL) {
        create_native_screen();
    }
    if (s_native_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_native_screen) {
        lv_screen_load(s_native_screen);
    }

    const uint32_t accent = native_hue_color(face->hue, 18);
    lv_obj_set_style_text_color(s_native_title, lv_color_hex(accent), 0);
    lv_obj_set_style_text_color(s_native_primary, lv_color_hex(native_hue_color(face->hue, 80)), 0);
    lv_label_set_text(s_native_title, face->title != NULL ? face->title : "Face");
    lv_label_set_text(s_native_subtitle, face->subtitle != NULL ? face->subtitle : "");
    const faculty175_face_desc_t *desc = faculty175_faces_get(face->id);
    lv_label_set_text(s_native_category, category_name(desc != NULL ? desc->categories : 0));
    lv_label_set_text(s_native_line_a, face->a != NULL ? face->a : "");
    lv_label_set_text(s_native_line_b, face->b != NULL ? face->b : "");
    lv_label_set_text(s_native_line_c, face->c != NULL ? face->c : "");
    update_native_body(face, anim_ms, accent);

    lv_obj_invalidate(s_native_screen);
    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

void faculty175_lvgl_service(uint32_t now_ms)
{
    if (!s_ready) {
        return;
    }
    uint32_t elapsed = s_last_service_ms == 0 ? 16u : now_ms - s_last_service_ms;
    s_last_service_ms = now_ms;
    lvgl_tick(elapsed);
    lv_timer_handler();
}

bool faculty175_lvgl_draw_nav(const faculty175_face_desc_t *center,
                              const faculty175_face_desc_t *left,
                              const faculty175_face_desc_t *right,
                              const faculty175_face_desc_t *up,
                              const faculty175_face_desc_t *down,
                              uint32_t anim_ms)
{
    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }
    if (s_nav_screen == NULL) {
        create_nav_screen();
    }
    if (s_nav_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_nav_screen) {
        lv_screen_load(s_nav_screen);
    }

    nav_align_center(0, false);
    nav_update_center_labels(center);
    (void)left;
    (void)right;
    (void)up;
    (void)down;

    lvgl_tick(16);
    lv_timer_handler();
    return true;
}

bool faculty175_lvgl_transition_nav(const faculty175_face_desc_t *center,
                                    bool vertical,
                                    int delta,
                                    uint32_t duration_ms)
{
    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }
    if (s_nav_screen == NULL) {
        create_nav_screen();
    }
    if (s_nav_screen == NULL) {
        return false;
    }
    if (lv_screen_active() != s_nav_screen) {
        lv_screen_load(s_nav_screen);
    }

    const int32_t dir = delta >= 0 ? 1 : -1;
    const int32_t travel = vertical ? 58 : 72;
    const uint32_t duration = duration_ms > 0 ? duration_ms : 72;
    const uint32_t step_ms = 8;
    const uint32_t steps = duration / step_ms > 0 ? duration / step_ms : 1;
    int64_t metric_start_us = 0;
    int64_t metric_last_us = 0;
    uint32_t metric_frames = 0;
    uint32_t metric_max_gap_ms = 0;

    const char *old_center = lv_label_get_text(s_nav_center);
    const char *old_slug = lv_label_get_text(s_nav_center_slug);
    lv_label_set_text(s_nav_out_center, old_center != NULL ? old_center : "");
    lv_label_set_text(s_nav_out_slug, old_slug != NULL ? old_slug : "");
    lv_obj_set_style_text_opa(s_nav_out_center, LV_OPA_COVER, 0);
    lv_obj_set_style_text_opa(s_nav_out_slug, LV_OPA_COVER, 0);
    nav_align_pair(s_nav_out_center, s_nav_out_slug, 0, vertical);
    native_obj_hidden(s_nav_out_center, false);
    native_obj_hidden(s_nav_out_slug, false);

    nav_update_center_labels(center);
    nav_align_center(dir * travel, vertical);
    lv_obj_set_style_text_opa(s_nav_center, 120, 0);
    lv_obj_set_style_text_opa(s_nav_center_slug, 120, 0);
    nav_anim_metric_start(&metric_last_us, &metric_start_us, &metric_frames, &metric_max_gap_ms);
    for (uint32_t step = 0; step <= steps; ++step) {
        const uint32_t t = (step * 1024u) / steps;
        const uint32_t eased = (t * t * (3072u - 2u * t)) / (1024u * 1024u);
        const int32_t in_offset = (int32_t)((int64_t)dir * travel * (int64_t)(1024u - eased) / 1024);
        const int32_t out_offset = (int32_t)((int64_t)-dir * travel * (int64_t)eased / 1024);
        const lv_opa_t opa = (lv_opa_t)(128 + (int32_t)(127u * eased) / 1024);
        const lv_opa_t old_opa = (lv_opa_t)(255 - (int32_t)(175u * eased) / 1024);
        nav_align_center(in_offset, vertical);
        nav_align_pair(s_nav_out_center, s_nav_out_slug, out_offset, vertical);
        lv_obj_set_style_text_opa(s_nav_center, opa, 0);
        lv_obj_set_style_text_opa(s_nav_center_slug, opa, 0);
        lv_obj_set_style_text_opa(s_nav_out_center, old_opa, 0);
        lv_obj_set_style_text_opa(s_nav_out_slug, old_opa, 0);
        lv_obj_invalidate(s_nav_screen);
        lvgl_tick(step_ms);
        lv_timer_handler();
        nav_anim_metric_frame(&metric_last_us, &metric_frames, &metric_max_gap_ms);
        if (step < steps) {
            vTaskDelay(pdMS_TO_TICKS(step_ms));
        }
    }

    nav_align_center(0, vertical);
    lv_obj_set_style_text_opa(s_nav_center, LV_OPA_COVER, 0);
    lv_obj_set_style_text_opa(s_nav_center_slug, LV_OPA_COVER, 0);
    native_obj_hidden(s_nav_out_center, true);
    native_obj_hidden(s_nav_out_slug, true);
    lvgl_tick(1);
    lv_timer_handler();
    nav_anim_metric_log("nav-preview",
                        vertical ? "vertical" : "horizontal",
                        delta,
                        duration,
                        metric_start_us,
                        metric_frames,
                        metric_max_gap_ms);
    return true;
}

static void lvgl_tick(uint32_t elapsed_ms)
{
    if (elapsed_ms == 0 || elapsed_ms > 100) {
        elapsed_ms = 16;
    }
    lv_tick_inc(elapsed_ms);
}

static lv_obj_t *create_snapshot_screen(const uint16_t *pixels, lv_image_dsc_t *image)
{
    if (pixels == NULL || image == NULL) {
        return NULL;
    }

    *image = (lv_image_dsc_t) {
        .header = {
            .magic = LV_IMAGE_HEADER_MAGIC,
            .cf = LV_COLOR_FORMAT_RGB565,
            .flags = 0,
            .w = FACULTY175_LCD_W,
            .h = FACULTY175_LCD_H,
            .stride = FACULTY175_LCD_W * sizeof(uint16_t),
            .reserved_2 = 0,
        },
        .data_size = FACULTY175_LCD_W * FACULTY175_LCD_H * sizeof(uint16_t),
        .data = (const uint8_t *)pixels,
        .reserved = NULL,
        .reserved_2 = NULL,
    };

    lv_obj_t *screen = lv_obj_create(NULL);
    if (screen == NULL) {
        return NULL;
    }
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x07090d), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *img = lv_image_create(screen);
    if (img == NULL) {
        lv_obj_delete(screen);
        return NULL;
    }
    lv_image_set_src(img, image);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    return screen;
}

static lv_obj_t *idle_screen(void)
{
    if (s_idle_screen == NULL) {
        s_idle_screen = lv_obj_create(NULL);
        if (s_idle_screen != NULL) {
            lv_obj_remove_style_all(s_idle_screen);
            lv_obj_set_size(s_idle_screen, FACULTY175_LCD_W, FACULTY175_LCD_H);
            lv_obj_set_style_bg_color(s_idle_screen, lv_color_hex(0x07090d), 0);
            lv_obj_set_style_bg_opa(s_idle_screen, LV_OPA_COVER, 0);
            lv_obj_clear_flag(s_idle_screen, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
    return s_idle_screen;
}

bool faculty175_lvgl_animate_frames(const uint16_t *from,
                                    const uint16_t *to,
                                    bool vertical,
                                    int delta,
                                    uint32_t duration_ms)
{
    if (from == NULL || to == NULL) {
        return false;
    }
    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }

    lv_image_dsc_t from_image;
    lv_image_dsc_t to_image;
    lv_obj_t *from_screen = create_snapshot_screen(from, &from_image);
    lv_obj_t *to_screen = create_snapshot_screen(to, &to_image);
    if (from_screen == NULL || to_screen == NULL) {
        if (from_screen != NULL) {
            lv_obj_delete(from_screen);
        }
        if (to_screen != NULL) {
            lv_obj_delete(to_screen);
        }
        return false;
    }

    lv_screen_load(from_screen);
    lvgl_tick(16);
    lv_timer_handler();

    const lv_screen_load_anim_t anim = vertical
                                           ? (delta >= 0 ? LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM
                                                         : LV_SCREEN_LOAD_ANIM_MOVE_TOP)
                                           : (delta >= 0 ? LV_SCREEN_LOAD_ANIM_MOVE_RIGHT
                                                         : LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
    const uint32_t duration = duration_ms > 0 ? duration_ms : 72;
    lv_screen_load_anim(to_screen, anim, duration, 0, false);
    int64_t metric_start_us = 0;
    int64_t metric_last_us = 0;
    uint32_t metric_frames = 0;
    uint32_t metric_max_gap_ms = 0;
    nav_anim_metric_start(&metric_last_us, &metric_start_us, &metric_frames, &metric_max_gap_ms);
    for (uint32_t elapsed = 0; elapsed <= duration; elapsed += 8) {
        lvgl_tick(8);
        lv_timer_handler();
        nav_anim_metric_frame(&metric_last_us, &metric_frames, &metric_max_gap_ms);
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    lv_screen_load(to_screen);
    lvgl_tick(16);
    lv_timer_handler();

    faculty175_display_draw_rgb565(to, 0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);
    faculty175_display_flush_rect(0, 0, FACULTY175_LCD_W, FACULTY175_LCD_H);

    lv_obj_t *idle = idle_screen();
    if (idle != NULL) {
        lv_screen_load(idle);
    }
    nav_anim_metric_log("snapshot-slide",
                        vertical ? "vertical" : "horizontal",
                        delta,
                        duration,
                        metric_start_us,
                        metric_frames,
                        metric_max_gap_ms);
    lv_obj_delete(from_screen);
    lv_obj_delete(to_screen);
    return true;
}

static bool draw_watch(uint32_t anim_ms)
{
    if (!s_watch_created) {
        create_watch_screen();
    }

    const uint32_t elapsed = s_last_anim_ms == 0 ? 16u : anim_ms - s_last_anim_ms;
    s_last_anim_ms = anim_ms;
    lvgl_tick(elapsed);

    bool time_valid = false;
    const uint32_t day_s = watch_seconds_of_day(anim_ms, &time_valid);
    if (day_s == s_watch_rendered_day_s) {
        if (lv_screen_active() != s_watch_screen) {
            lv_screen_load(s_watch_screen);
        }
        lv_timer_handler();
        return true;
    }
    const int32_t boundary_late_ms = watch_tick_boundary_late_ms(day_s);
    const uint32_t previous_day_s = s_watch_rendered_day_s;
    const int64_t update_start_us = esp_timer_get_time();
    const bool first_render = previous_day_s == UINT32_MAX;
    const uint32_t minute_bucket = day_s / 60u;
    const bool slow_layer_update = first_render || minute_bucket != s_watch_rendered_minute;
    s_watch_rendered_day_s = day_s;

    if (slow_layer_update) {
        const float hour = (float)day_s / 3600.0f;
        uint32_t tint_color = 0;
        lv_opa_t tint_opa = 0;
        watch_tint_for_hour(hour, &tint_color, &tint_opa);
        if (!time_valid) {
            tint_opa = (lv_opa_t)((uint16_t)tint_opa * 3u / 4u);
        }
        if (s_watch_tint != NULL &&
            (tint_color != s_watch_rendered_tint_color || tint_opa != s_watch_rendered_tint_opa)) {
            lv_obj_set_style_bg_color(s_watch_tint, lv_color_hex(tint_color), 0);
            lv_obj_set_style_bg_opa(s_watch_tint, tint_opa, 0);
            s_watch_rendered_tint_color = tint_color;
            s_watch_rendered_tint_opa = tint_opa;
        }
        s_watch_rendered_minute = minute_bucket;
    }

    const uint32_t whole_second = day_s % 60u;
    const float second_u = (float)whole_second / 60.0f;
    if (slow_layer_update) {
        const float minute_u = (float)(day_s % 3600u) / 3600.0f;
        const float hour_u = (float)(day_s % 43200u) / 43200.0f;
        set_hand_segment(s_watch_hour_line, s_hour_points, hour_u, 10, 92);
        set_hand_segment(s_watch_minute_line, s_minute_points, minute_u, 8, 146);
        set_hand_segment(s_watch_hour_tail, s_hour_tail_points, hour_u, -34, -6);
        set_hand_segment(s_watch_minute_tail, s_minute_tail_points, minute_u, -44, -7);
        set_hand_segment(s_watch_hour_accent, s_hour_accent_points, hour_u, 18, 78);
        set_hand_segment(s_watch_minute_accent, s_minute_accent_points, minute_u, 20, 126);
        set_line_points(s_watch_second_line, s_second_points, second_u, 172);
    } else {
        set_second_line_points_radial(s_watch_second_line, s_second_points, second_u, 172);
    }
    if (lv_screen_active() != s_watch_screen) {
        lv_screen_load(s_watch_screen);
    }
    lv_timer_handler();
    const uint32_t update_us = (uint32_t)(esp_timer_get_time() - update_start_us);
    uint32_t step_s = 1;
    if (previous_day_s != UINT32_MAX) {
        step_s = (day_s + 86400u - previous_day_s) % 86400u;
    }
    ESP_LOGI(TAG,
             "watch-metrics second=%02u boundary_late_ms=%d update_us=%u step_s=%u time_valid=%u count=%u",
             (unsigned)(day_s % 60u),
             (int)boundary_late_ms,
             (unsigned)update_us,
             (unsigned)step_s,
             time_valid ? 1u : 0u,
             (unsigned)++s_watch_tick_metrics_count);
    return true;
}

static faculty175_native_style_t descriptor_style_for_face(const faculty175_face_desc_t *desc)
{
    if (desc == NULL) {
        return FACULTY175_NATIVE_STATUS;
    }
    switch (desc->id) {
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_POCKETWATCH:
            return FACULTY175_NATIVE_ANALOG;
        case FACULTY175_FACE_DIGITAL:
            return FACULTY175_NATIVE_DIGITAL;
        case FACULTY175_FACE_RADAR:
        case FACULTY175_FACE_DEATHSTAR:
            return FACULTY175_NATIVE_RADAR;
        case FACULTY175_FACE_SETTINGS:
        case FACULTY175_FACE_HID:
        case FACULTY175_FACE_BIOMETRICS:
        case FACULTY175_FACE_WATCHER:
            return FACULTY175_NATIVE_STATUS;
        case FACULTY175_FACE_NOTES:
        case FACULTY175_FACE_QUOTES:
        case FACULTY175_FACE_QDAY:
        case FACULTY175_FACE_BABEL:
        case FACULTY175_FACE_FACULTY:
            return FACULTY175_NATIVE_TEXT;
        default:
            break;
    }
    if ((desc->categories & FACULTY175_FACE_CAT_INSTRUMENT) != 0) {
        return FACULTY175_NATIVE_INSTRUMENT;
    }
    if ((desc->categories & FACULTY175_FACE_CAT_ORACLE) != 0) {
        return FACULTY175_NATIVE_ORACLE;
    }
    if (desc->id == FACULTY175_FACE_SCALE ||
        desc->id == FACULTY175_FACE_SKY || desc->id == FACULTY175_FACE_GLOBE ||
        desc->id == FACULTY175_FACE_WEATHER || desc->id == FACULTY175_FACE_TRANSITS ||
        desc->id == FACULTY175_FACE_SOLAR || desc->id == FACULTY175_FACE_MAGNETOSPHERE) {
        return FACULTY175_NATIVE_CELESTIAL;
    }
    return FACULTY175_NATIVE_STATUS;
}

static uint8_t descriptor_hue_for_face(faculty175_face_id_t id)
{
    return (uint8_t)(((uint32_t)id * 5u + 1u) % 6u);
}

static const char *descriptor_subtitle_for_face(const faculty175_face_desc_t *desc)
{
    if (desc == NULL) {
        return "";
    }
    switch (desc->id) {
        case FACULTY175_FACE_FACULTY: return "Ask Castalia";
        case FACULTY175_FACE_CLASSIC: return "Analog time";
        case FACULTY175_FACE_APOCALYPSO: return "Storm dial";
        case FACULTY175_FACE_DIGITAL: return "Local time";
        case FACULTY175_FACE_SPOTIFY: return "Now playing";
        case FACULTY175_FACE_NOTES: return "Commonplace";
        case FACULTY175_FACE_CALCIFER: return "Hearth spirit";
        case FACULTY175_FACE_CASTALIA: return "System signal";
        case FACULTY175_FACE_ASTROLOGY: return "Natal wheel";
        case FACULTY175_FACE_JYOTISH: return "Sidereal rasi";
        case FACULTY175_FACE_BAZI: return "Four pillars";
        case FACULTY175_FACE_SYNASTRY: return "Two charts";
        case FACULTY175_FACE_TAROT: return "Full deck";
        case FACULTY175_FACE_INQ: return "Question card";
        case FACULTY175_FACE_RUNES: return "Three-rune spread";
        case FACULTY175_FACE_ALETHIOMETER: return "Symbol dial";
        case FACULTY175_FACE_SPECTRUM: return "Audio spectrum";
        case FACULTY175_FACE_CHAKRA: return "Tonal center";
        case FACULTY175_FACE_BOWL: return "Singing bowl";
        case FACULTY175_FACE_ROCKET: return "Launch vector";
        case FACULTY175_FACE_RADAR: return "Sweep scan";
        case FACULTY175_FACE_WEATHER: return "Local sky";
        case FACULTY175_FACE_GLOBE: return "World view";
        case FACULTY175_FACE_SCALE: return "Worlds in proportion";
        case FACULTY175_FACE_SKY: return "Night map";
        case FACULTY175_FACE_QUOTES: return "Collected lines";
        case FACULTY175_FACE_TRANSITS: return "Live transits";
        case FACULTY175_FACE_OCARINA: return "Breath instrument";
        case FACULTY175_FACE_PITCH: return "Reference tone";
        case FACULTY175_FACE_BONGO: return "Percussion";
        case FACULTY175_FACE_PIANO: return "Keys";
        case FACULTY175_FACE_KALIMBA: return "Tines";
        case FACULTY175_FACE_DRONE: return "Continuous tone";
        case FACULTY175_FACE_CHORD: return "Harmony";
        case FACULTY175_FACE_LEVEL: return "Orientation";
        case FACULTY175_FACE_TUNING: return "Tuner";
        case FACULTY175_FACE_PANDRUM: return "Handpan";
        case FACULTY175_FACE_ORIENT: return "Compass";
        case FACULTY175_FACE_QDAY: return "Daily question";
        case FACULTY175_FACE_FOCUS: return "Timer";
        case FACULTY175_FACE_BIOMETRICS: return "Body state";
        case FACULTY175_FACE_WATCHER: return "Device watch";
        case FACULTY175_FACE_LENORMAND: return "Oracle tableau";
        case FACULTY175_FACE_GEOMANCY: return "Figures";
        case FACULTY175_FACE_ENOCHIAN: return "Angel table";
        case FACULTY175_FACE_HID: return "Touchpad";
        case FACULTY175_FACE_WSCAN: return "Broad network scan";
        case FACULTY175_FACE_DEAUTH: return "802.11 deauth lab";
        case FACULTY175_FACE_EVILTWIN: return "Credential capture lab";
        case FACULTY175_FACE_HANDSHAKE: return "WPA handshake lab";
        case FACULTY175_FACE_INCIDENTS: return "SecOps incident log";
        case FACULTY175_FACE_BABEL: return "Live translation";
        case FACULTY175_FACE_DEATHSTAR: return "Trench run";
        case FACULTY175_FACE_SOLAR: return "Live solar map";
        case FACULTY175_FACE_MAGNETOSPHERE: return "NASA CCMC map";
        case FACULTY175_FACE_SETTINGS: return "Device settings";
        default: return category_name(desc->categories);
    }
}

static const char *descriptor_primary_for_style(faculty175_native_style_t style)
{
    switch (style) {
        case FACULTY175_NATIVE_ANALOG: return "DIAL";
        case FACULTY175_NATIVE_DIGITAL: return "TIME";
        case FACULTY175_NATIVE_ORACLE: return "CAST";
        case FACULTY175_NATIVE_INSTRUMENT: return "PLAY";
        case FACULTY175_NATIVE_CELESTIAL: return "ORBITS";
        case FACULTY175_NATIVE_RADAR: return "SCAN";
        case FACULTY175_NATIVE_TEXT: return "READ";
        case FACULTY175_NATIVE_STATUS:
        default: return "STATUS";
    }
}

static bool draw_face_descriptor(faculty175_face_id_t id, uint32_t anim_ms)
{
    faculty175_native_face_t generated = {};

    const faculty175_face_desc_t *desc = faculty175_faces_get(id);
    if (desc == NULL) {
        return false;
    }

    const faculty175_native_style_t style = descriptor_style_for_face(desc);
    generated.id = id;
    generated.title = desc->label;
    generated.subtitle = descriptor_subtitle_for_face(desc);
    generated.style = style;
    generated.hue = descriptor_hue_for_face(id);
    generated.a = descriptor_primary_for_style(style);
    generated.b = category_name(desc->categories);
    generated.c = desc->slug;
    return faculty175_lvgl_draw_native_face(&generated, anim_ms);
}

bool faculty175_lvgl_draw_face(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (id == FACULTY175_FACE_JYOTISH || id == FACULTY175_FACE_BAZI ||
        id == FACULTY175_FACE_DEATHSTAR || id == FACULTY175_FACE_TRON || id == FACULTY175_FACE_MAZE ||
        id == FACULTY175_FACE_CRYSTAL_BALL) {
        return false;
    }

    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }

    if (instrument_face_id(id)) {
        return draw_instrument(id, anim_ms);
    }
    if (oracle_face_id(id)) {
        return draw_oracle(id, anim_ms);
    }
    if (utility_face_id(id)) {
        return draw_utility(id, anim_ms);
    }

    switch (id) {
        case FACULTY175_FACE_CRYSTAL_BALL:
        case FACULTY175_FACE_ALMANAC:
        case FACULTY175_FACE_PHENOLOGY:
        case FACULTY175_FACE_LUOPAN:
        case FACULTY175_FACE_PYTHIA:
        case FACULTY175_FACE_MAZE:
        case FACULTY175_FACE_TRON:
            return false;
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_POCKETWATCH:
            return draw_watch(anim_ms);
        case FACULTY175_FACE_MOON:
            return draw_moon(anim_ms);
        case FACULTY175_FACE_TAROT:
            return draw_tarot(anim_ms);
        case FACULTY175_FACE_RUNES:
            return draw_runes(anim_ms);
        case FACULTY175_FACE_ALETHIOMETER:
            return draw_alethiometer(anim_ms);
        case FACULTY175_FACE_SKY:
            return draw_sky(anim_ms);
        case FACULTY175_FACE_SCALE:
            return draw_scale(anim_ms);
        case FACULTY175_FACE_ASTROLOGY:
            return draw_astrology(anim_ms);
        case FACULTY175_FACE_SYNASTRY:
            return draw_synastry(anim_ms);
        case FACULTY175_FACE_TRANSITS:
            return draw_transits(anim_ms);
        case FACULTY175_FACE_LENORMAND:
            return draw_lenormand(anim_ms);
        case FACULTY175_FACE_PIANO:
            return draw_piano(anim_ms);
        case FACULTY175_FACE_SOLAR:
            return draw_solar(anim_ms);
        case FACULTY175_FACE_MAGNETOSPHERE:
            return draw_magnetosphere(anim_ms);
        default:
            return draw_face_descriptor(id, anim_ms);
    }
}

static lv_obj_t *face_screen_for_id(faculty175_face_id_t id)
{
    if (instrument_face_id(id)) {
        return s_instrument_screen;
    }
    if (oracle_face_id(id)) {
        return s_oracle_screen;
    }
    if (utility_face_id(id)) {
        return s_utility_screen;
    }

    switch (id) {
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_POCKETWATCH:
            return s_watch_screen;
        case FACULTY175_FACE_MOON:
            return s_moon_screen;
        case FACULTY175_FACE_TAROT:
            return s_tarot_screen;
        case FACULTY175_FACE_RUNES:
            return s_runes_screen;
        case FACULTY175_FACE_ALETHIOMETER:
            return s_aleth_screen;
        case FACULTY175_FACE_SKY:
            return s_sky_screen;
        case FACULTY175_FACE_SCALE:
            return s_scale_screen;
        case FACULTY175_FACE_ASTROLOGY:
            return s_astrology_screen;
        case FACULTY175_FACE_SYNASTRY:
            return s_synastry_screen;
        case FACULTY175_FACE_TRANSITS:
            return s_transits_screen;
        case FACULTY175_FACE_LENORMAND:
            return s_lenormand_screen;
        case FACULTY175_FACE_PIANO:
            return s_piano_screen;
        case FACULTY175_FACE_SOLAR:
            return s_solar_screen;
        case FACULTY175_FACE_MAGNETOSPHERE:
            return s_magnet_screen;
        default:
            return s_native_screen;
    }
}

static int face_screen_slot_for_id(faculty175_face_id_t id)
{
    if (instrument_face_id(id)) {
        return 1000;
    }
    if (oracle_face_id(id)) {
        return 1001;
    }
    if (utility_face_id(id)) {
        return 1002;
    }

    switch (id) {
        case FACULTY175_FACE_CLASSIC:
        case FACULTY175_FACE_POCKETWATCH:
            return 1003;
        case FACULTY175_FACE_MOON:
        case FACULTY175_FACE_TAROT:
        case FACULTY175_FACE_RUNES:
        case FACULTY175_FACE_ALETHIOMETER:
        case FACULTY175_FACE_SKY:
        case FACULTY175_FACE_SCALE:
        case FACULTY175_FACE_ASTROLOGY:
        case FACULTY175_FACE_SYNASTRY:
        case FACULTY175_FACE_TRANSITS:
        case FACULTY175_FACE_LENORMAND:
        case FACULTY175_FACE_PIANO:
        case FACULTY175_FACE_SOLAR:
        case FACULTY175_FACE_MAGNETOSPHERE:
            return (int)id;
        default:
            return 1004;
    }
}

bool faculty175_lvgl_faces_share_transition_screen(faculty175_face_id_t a, faculty175_face_id_t b)
{
    return a != b && face_screen_slot_for_id(a) == face_screen_slot_for_id(b);
}

bool faculty175_lvgl_transition_face(faculty175_face_id_t from_id,
                                     faculty175_face_id_t to_id,
                                     uint32_t anim_ms,
                                     bool vertical,
                                     int delta,
                                     uint32_t duration_ms,
                                     bool *animated_out)
{
    if (animated_out != NULL) {
        *animated_out = false;
    }
    if (!s_ready && faculty175_lvgl_init() != ESP_OK) {
        return false;
    }

    lv_obj_t *from_screen = face_screen_for_id(from_id);
    const bool from_active = from_screen != NULL && lv_screen_active() == from_screen;
    const bool cross_screen = face_screen_slot_for_id(from_id) != face_screen_slot_for_id(to_id);
    const bool suspend_preload_flush = from_active && cross_screen;
    if (suspend_preload_flush) {
        faculty175_display_flush_suspended_set(true);
    }
    if (!faculty175_lvgl_draw_face(to_id, anim_ms)) {
        if (suspend_preload_flush) {
            faculty175_display_flush_suspended_set(false);
        }
        return false;
    }
    if (suspend_preload_flush) {
        faculty175_display_flush_suspended_set(false);
    }

    lv_obj_t *to_screen = face_screen_for_id(to_id);
    if (!suspend_preload_flush || from_screen == NULL || to_screen == NULL || from_screen == to_screen) {
        return true;
    }

    lv_screen_load(from_screen);
    lvgl_tick(1);
    lv_timer_handler();

    const lv_screen_load_anim_t anim = vertical
                                           ? (delta >= 0 ? LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM
                                                         : LV_SCREEN_LOAD_ANIM_MOVE_TOP)
                                           : (delta >= 0 ? LV_SCREEN_LOAD_ANIM_MOVE_RIGHT
                                                         : LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
    const uint32_t duration = duration_ms > 0 ? duration_ms : 72;
    lv_screen_load_anim(to_screen, anim, duration, 0, false);
    int64_t metric_start_us = 0;
    int64_t metric_last_us = 0;
    uint32_t metric_frames = 0;
    uint32_t metric_max_gap_ms = 0;
    nav_anim_metric_start(&metric_last_us, &metric_start_us, &metric_frames, &metric_max_gap_ms);
    for (uint32_t elapsed = 0; elapsed <= duration; elapsed += 8) {
        lvgl_tick(8);
        lv_timer_handler();
        nav_anim_metric_frame(&metric_last_us, &metric_frames, &metric_max_gap_ms);
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    lv_screen_load(to_screen);
    lvgl_tick(1);
    lv_timer_handler();
    if (animated_out != NULL) {
        *animated_out = true;
    }
    nav_anim_metric_log("screen-slide",
                        vertical ? "vertical" : "horizontal",
                        delta,
                        duration,
                        metric_start_us,
                        metric_frames,
                        metric_max_gap_ms);
    return true;
}
