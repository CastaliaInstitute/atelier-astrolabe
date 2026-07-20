#include "faculty175_ble.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "faculty175_board.h"
#include "faculty175_motion.h"

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static void centered(const char *text, int y, uint16_t color)
{
    faculty175_display_draw_centered_text(text, y, color);
}

static void label_at(const char *text, int x, int y, uint16_t color)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    const int w = (int)strlen(text) * 6;
    faculty175_display_draw_text(text, x - w / 2, y, color);
}

static int clamp_i(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

static void short_name(const faculty175_ble_peer_t *peer, char *out, size_t cap)
{
    if (out == NULL || cap == 0) {
        return;
    }
    out[0] = '\0';
    if (peer == NULL) {
        snprintf(out, cap, "BLE");
        return;
    }
    const char *name = peer->name;
    if (name[0] == '\0' || strncasecmp(name, "BLE ", 4) == 0) {
        snprintf(out, cap, "%s %04X", peer->astrolabe ? "AST" : (peer->ring ? "RING" : "BLE"), peer->addr_hash);
        return;
    }
    if (strncasecmp(name, "Astrolabe ", 10) == 0) {
        name += 10;
    }
    if (strlen(name) <= 10) {
        snprintf(out, cap, "%s", name);
    } else {
        snprintf(out, cap, "%s %04X", peer->astrolabe ? "AST" : (peer->ring ? "RING" : "BLE"), peer->addr_hash);
    }
}

static void draw_astrolabe_icon(int x, int y, uint16_t color, uint16_t accent)
{
    faculty175_display_draw_circle(x, y, 9, color);
    faculty175_display_draw_circle(x, y, 4, accent);
    faculty175_display_draw_line(x - 12, y, x - 5, y, color);
    faculty175_display_draw_line(x + 5, y, x + 12, y, color);
    faculty175_display_draw_line(x, y - 12, x, y - 5, color);
    faculty175_display_draw_line(x, y + 5, x, y + 12, color);
    faculty175_display_fill_circle(x, y, 2, color);
}

static void draw_ring_icon(int x, int y, uint16_t color, uint16_t accent)
{
    faculty175_display_draw_circle(x, y, 10, color);
    faculty175_display_draw_circle(x, y, 6, accent);
    faculty175_display_fill_circle(x + 7, y - 7, 3, color);
}

static void draw_ble_icon(int x, int y, uint16_t color)
{
    faculty175_display_fill_circle(x, y, 5, color);
    faculty175_display_draw_circle(x, y, 10, color);
}

void faculty175_face_radar_draw(uint32_t anim_ms)
{
    faculty175_ble_radar_tick(0);

    faculty175_ble_peer_t peers[FACULTY175_BLE_PEER_MAX] = {};
    const size_t peer_count = faculty175_ble_peers_snapshot(peers, FACULTY175_BLE_PEER_MAX);
    const bool ble_on = faculty175_ble_enabled();
    const bool scanning = faculty175_ble_scanning();

    const int cx = FACULTY175_LCD_W / 2;
    const int cy = FACULTY175_LCD_H / 2 + 8;
    const int outer = 178;
    const uint16_t bg = rgb(4, 8, 12);
    const uint16_t grid = rgb(22, 55, 58);
    const uint16_t dim = rgb(102, 132, 140);
    const uint16_t accent = rgb(92, 240, 168);
    const uint16_t peer_col = rgb(116, 198, 255);
    const uint16_t ring_col = rgb(238, 176, 255);
    const uint16_t generic_col = rgb(216, 188, 118);
    float self_pitch = 0.0f;
    float self_roll = 0.0f;
    const bool self_imu = faculty175_motion_pitch_roll(&self_pitch, &self_roll);

    faculty175_display_fill_rgb565(bg);
    faculty175_display_draw_bezel_label("ASTROLABE RADAR", false, 220, anim_ms, accent);
    faculty175_display_draw_bezel_label(scanning ? "SCANNING BLE FIELD" : "PEER FIELD RSSI", true, 220, anim_ms, dim);

    for (int r = 58; r <= outer; r += 40) {
        faculty175_display_draw_circle(cx, cy, r, grid);
    }
    faculty175_display_draw_line(cx - outer, cy, cx + outer, cy, grid);
    faculty175_display_draw_line(cx, cy - outer, cx, cy + outer, grid);

    const float sweep = ((float)(anim_ms % 3600u) / 3600.0f) * 6.2831853f - 1.5707963f;
    const int sx = cx + (int)lrintf(cosf(sweep) * (float)outer);
    const int sy = cy + (int)lrintf(sinf(sweep) * (float)outer);
    faculty175_display_draw_line(cx, cy, sx, sy, accent);
    faculty175_display_fill_circle(cx, cy, 7, accent);
    faculty175_display_draw_circle(cx, cy, 14, rgb(20, 92, 70));

    for (size_t i = 0; i < peer_count; ++i) {
        const faculty175_ble_peer_t *peer = &peers[i];
        const float a = ((float)peer->bearing_deg / 360.0f) * 6.2831853f - 1.5707963f;
        const int radius = clamp_i(((int)peer->range_pct * outer) / 100, 28, outer - 8);
        const int x = cx + (int)lrintf(cosf(a) * (float)radius);
        const int y = cy + (int)lrintf(sinf(a) * (float)radius);
        const uint16_t col = peer->astrolabe ? peer_col : (peer->ring ? ring_col : generic_col);
        const uint16_t icon_accent = peer->imu_valid ? accent : rgb(22, 70, 76);
        if (peer->astrolabe) {
            draw_astrolabe_icon(x, y, col, icon_accent);
        } else if (peer->ring) {
            draw_ring_icon(x, y, col, icon_accent);
        } else {
            draw_ble_icon(x, y, col);
        }
        if (peer->imu_valid) {
            const float tilt = ((float)peer->imu_roll_deg / 180.0f) * 3.1415927f;
            const int tx = x + (int)lrintf(cosf(tilt) * 14.0f);
            const int ty = y + (int)lrintf(sinf(tilt) * 14.0f);
            faculty175_display_draw_line(x, y, tx, ty, accent);
        }
        if (i < 4) {
            char label[18];
            short_name(peer, label, sizeof(label));
            const int label_y = peer->astrolabe ? y + 17 : (peer->ring ? y + 16 : y + 14);
            label_at(label, clamp_i(x, 58, FACULTY175_LCD_W - 58), clamp_i(label_y, 80, 386), col);
        }
    }

    char line[64];
    snprintf(line, sizeof(line), "%u PEERS  %s", (unsigned)peer_count, scanning ? "LIVE" : (ble_on ? "IDLE" : "BLE OFF"));
    centered(line, 58, ble_on ? accent : rgb(220, 104, 92));
    if (self_imu) {
        snprintf(line, sizeof(line), "SELF IMU P%+.0f R%+.0f", (double)self_pitch, (double)self_roll);
        centered(line, 78, dim);
    }

    if (peer_count == 0) {
        centered(ble_on ? "WAITING FOR ADVERTISEMENTS" : "ENABLE BLE TO SCAN", 394, dim);
    } else {
        const faculty175_ble_peer_t *p = &peers[0];
        char label[18];
        short_name(p, label, sizeof(label));
        if (p->observation_count > 0 && p->observations[0].valid) {
            const faculty175_ble_observation_t *obs = &p->observations[0];
            snprintf(line, sizeof(line), "%s SEES %s %04X %ddBm",
                     p->astrolabe ? "AST" : (p->ring ? "RING" : "BLE"),
                     obs->astrolabe ? "AST" : (obs->ring ? "RING" : "BLE"),
                     obs->addr_hash,
                     obs->rssi);
        } else {
            snprintf(line, sizeof(line), "%s %s  %ddBm  %u%%  %s",
                     p->astrolabe ? "AST" : (p->ring ? "RING" : "BLE"),
                     label,
                     p->rssi,
                     p->confidence_pct,
                     p->imu_valid ? "IMU" : "RSSI");
        }
        centered(line, 402, p->astrolabe ? peer_col : (p->ring ? ring_col : generic_col));
    }

    faculty175_display_flush();
}
