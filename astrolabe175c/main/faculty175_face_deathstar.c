#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "esp_partition.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "miniz.h"

#include "faculty175_board.h"
#define A1Z_PATH "/bust_cache/deathstar.a1v"
#define A1R1_MAGIC "A1R1"
#define A1Z1_MAGIC "A1Z1"
#define A1Z_HEADER_SIZE 32u

static const char *TAG = "deathstar";

static FILE *s_file;
static uint32_t *s_offsets;
static uint32_t s_file_size;
static uint16_t s_width;
static uint16_t s_height;
static uint16_t s_fps;
static uint32_t s_frame_count;
static uint8_t *s_comp;
static size_t s_comp_cap;
static size_t s_comp_len;
static uint8_t *s_bits;
static size_t s_bits_cap;
static size_t s_bits_len;
static uint16_t *s_row;
static bool s_ready;
static bool s_failed;
static bool s_listed_missing;
static bool s_zipped;
static bool s_logged_first_draw;

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return faculty175_display_rgb888(r, g, b);
}

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool storage_mount(void)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
                                                           "storage");
    if (part == NULL) {
        ESP_LOGW(TAG, "storage partition not found");
    } else {
        ESP_LOGI(TAG, "storage partition offset=0x%lx size=%lu KiB",
                 (unsigned long)part->address,
                 (unsigned long)(part->size / 1024u));
    }
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/bust_cache",
        .partition_label = "storage",
        .max_files = 12,
        .format_if_mount_failed = false,
    };
    const esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "storage SPIFFS mount failed: %s", esp_err_to_name(err));
        return false;
    }
    size_t total = 0;
    size_t used = 0;
    if (esp_spiffs_info("storage", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "storage SPIFFS ok %u/%u KiB",
                 (unsigned)(used / 1024u),
                 (unsigned)(total / 1024u));
    }
    return true;
}

static bool read_exact(void *dst, size_t len)
{
    return s_file != NULL && fread(dst, 1, len, s_file) == len;
}

static bool deathstar_open(void)
{
    if (s_ready) {
        return true;
    }
    if (s_failed) {
        return false;
    }
    if (!storage_mount()) {
        ESP_LOGW(TAG, "storage mount unavailable");
        s_failed = true;
        return false;
    }

    const char *opened_path = A1Z_PATH;
    s_file = fopen(opened_path, "rb");
    if (s_file == NULL) {
        ESP_LOGW(TAG, "missing %s", A1Z_PATH);
        s_failed = true;
        if (!s_listed_missing) {
            s_listed_missing = true;
            DIR *dir = opendir("/bust_cache");
            if (dir == NULL) {
                ESP_LOGW(TAG, "opendir /bust_cache failed");
            } else {
                struct dirent *ent = NULL;
                while ((ent = readdir(dir)) != NULL) {
                    ESP_LOGW(TAG, "bust_cache entry: %s", ent->d_name);
                }
                closedir(dir);
            }
        }
        return false;
    }
    if (fseek(s_file, 0, SEEK_END) != 0) {
        s_failed = true;
        return false;
    }
    const long end = ftell(s_file);
    if (end <= (long)A1Z_HEADER_SIZE || fseek(s_file, 0, SEEK_SET) != 0) {
        s_failed = true;
        return false;
    }
    s_file_size = (uint32_t)end;

    uint8_t hdr[A1Z_HEADER_SIZE];
    if (!read_exact(hdr, sizeof(hdr)) ||
        (memcmp(hdr, A1R1_MAGIC, 4) != 0 && memcmp(hdr, A1Z1_MAGIC, 4) != 0)) {
        ESP_LOGW(TAG, "invalid a1v header");
        s_failed = true;
        return false;
    }
    s_zipped = memcmp(hdr, A1Z1_MAGIC, 4) == 0;
    s_width = read_le16(&hdr[4]);
    s_height = read_le16(&hdr[6]);
    s_fps = read_le16(&hdr[8]);
    s_frame_count = read_le32(&hdr[12]);
    const uint32_t index_offset = read_le32(&hdr[16]);
    const uint32_t data_offset = read_le32(&hdr[20]);
    if (s_width != FACULTY175_LCD_W || s_height != FACULTY175_LCD_H || s_fps == 0 || s_frame_count == 0 ||
        index_offset < A1Z_HEADER_SIZE || data_offset <= index_offset ||
        data_offset > s_file_size || s_frame_count > 10000u) {
        ESP_LOGW(TAG, "unsupported a1v geometry %ux%u fps=%u frames=%u",
                 (unsigned)s_width,
                 (unsigned)s_height,
                 (unsigned)s_fps,
                 (unsigned)s_frame_count);
        s_failed = true;
        return false;
    }

    s_offsets = (uint32_t *)malloc(sizeof(uint32_t) * s_frame_count);
    s_row = (uint16_t *)malloc(sizeof(uint16_t) * FACULTY175_LCD_W);
    if (s_offsets == NULL || s_row == NULL) {
        ESP_LOGW(TAG, "a1v alloc failed");
        s_failed = true;
        return false;
    }
    if (fseek(s_file, (long)index_offset, SEEK_SET) != 0) {
        s_failed = true;
        return false;
    }
    for (uint32_t i = 0; i < s_frame_count; ++i) {
        uint8_t off[4];
        if (!read_exact(off, sizeof(off))) {
            s_failed = true;
            return false;
        }
        s_offsets[i] = read_le32(off);
        if (s_offsets[i] < data_offset || s_offsets[i] >= s_file_size ||
            (i > 0 && s_offsets[i] <= s_offsets[i - 1])) {
            ESP_LOGW(TAG, "bad a1v offset %u", (unsigned)i);
            s_failed = true;
            return false;
        }
    }
    s_ready = true;
    ESP_LOGI(TAG, "%s ready path=%s frames=%u fps=%u size=%u KiB",
             s_zipped ? "a1z" : "a1v",
             opened_path,
             (unsigned)s_frame_count,
             (unsigned)s_fps,
             (unsigned)(s_file_size / 1024u));
    return true;
}

static bool ensure_comp(size_t len)
{
    if (len <= s_comp_cap) {
        return true;
    }
    uint8_t *next = (uint8_t *)realloc(s_comp, len);
    if (next == NULL) {
        return false;
    }
    s_comp = next;
    s_comp_cap = len;
    return true;
}

static bool ensure_bits(size_t len)
{
    if (len <= s_bits_cap) {
        return true;
    }
    uint8_t *next = (uint8_t *)realloc(s_bits, len);
    if (next == NULL) {
        return false;
    }
    s_bits = next;
    s_bits_cap = len;
    return true;
}

static bool load_frame(uint32_t frame)
{
    const uint32_t start = s_offsets[frame];
    const uint32_t end = frame + 1u < s_frame_count ? s_offsets[frame + 1u] : s_file_size;
    if (end <= start || !ensure_comp(end - start)) {
        return false;
    }
    if (fseek(s_file, (long)start, SEEK_SET) != 0 || fread(s_comp, 1, end - start, s_file) != end - start) {
        return false;
    }
    s_comp_len = end - start;
    vTaskDelay(1);
    if (s_zipped) {
        const size_t want = ((size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H + 7u) / 8u;
        if (!ensure_bits(want)) {
            return false;
        }
        const size_t dest_len = tinfl_decompress_mem_to_mem(s_bits,
                                                            want,
                                                            s_comp,
                                                            s_comp_len,
                                                            TINFL_FLAG_PARSE_ZLIB_HEADER);
        if (dest_len != want) {
            ESP_LOGW(TAG, "a1z inflate failed out=%u want=%u", (unsigned)dest_len, (unsigned)want);
            return false;
        }
        s_bits_len = (size_t)dest_len;
        vTaskDelay(1);
    }
    return true;
}

static bool read_run(const uint8_t **p, const uint8_t *end, uint32_t *run)
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

static bool draw_rle_frame(void)
{
    const uint16_t black = rgb(0, 0, 0);
    const uint16_t white = rgb(245, 248, 244);
    if (s_comp_len < 2) {
        return false;
    }
    const uint8_t *p = s_comp;
    const uint8_t *end = s_comp + s_comp_len;
    bool color = *p++ != 0;
    int x = 0;
    int y = 0;
    while (p < end && y < FACULTY175_LCD_H) {
        uint32_t run = 0;
        if (!read_run(&p, end, &run)) {
            return false;
        }
        const uint16_t px = color ? white : black;
        while (run > 0 && y < FACULTY175_LCD_H) {
            const int chunk = (int)((run < (uint32_t)(FACULTY175_LCD_W - x)) ? run : (uint32_t)(FACULTY175_LCD_W - x));
            for (int i = 0; i < chunk; ++i) {
                s_row[x + i] = px;
            }
            x += chunk;
            run -= (uint32_t)chunk;
            if (x == FACULTY175_LCD_W) {
                faculty175_display_draw_rgb565(s_row, 0, y, FACULTY175_LCD_W, 1);
                x = 0;
                ++y;
                if ((y & 0x3f) == 0) {
                    vTaskDelay(1);
                }
            }
        }
        color = !color;
    }
    return y == FACULTY175_LCD_H && x == 0;
}

static bool draw_packed_1bpp_frame(void)
{
    const size_t want = ((size_t)FACULTY175_LCD_W * (size_t)FACULTY175_LCD_H + 7u) / 8u;
    if (s_bits_len < want) {
        return false;
    }
    const uint16_t black = rgb(0, 0, 0);
    const uint16_t white = rgb(245, 248, 244);
    for (int y = 0; y < FACULTY175_LCD_H; ++y) {
        const size_t row_bit = (size_t)y * (size_t)FACULTY175_LCD_W;
        for (int x = 0; x < FACULTY175_LCD_W; ++x) {
            const size_t bit = row_bit + (size_t)x;
            const uint8_t packed = s_bits[bit >> 3u];
            s_row[x] = ((packed >> (7u - (bit & 7u))) & 1u) != 0 ? white : black;
        }
        faculty175_display_draw_rgb565(s_row, 0, y, FACULTY175_LCD_W, 1);
        if ((y & 0x3f) == 0) {
            vTaskDelay(1);
        }
    }
    return true;
}

static void draw_missing(uint32_t anim_ms)
{
    (void)anim_ms;
    const uint16_t black = rgb(0, 0, 0);
    const uint16_t white = rgb(235, 238, 232);
    const uint16_t dim = rgb(92, 96, 92);
    faculty175_display_fill_rgb565(black);
    faculty175_display_draw_circle(FACULTY175_LCD_W / 2, FACULTY175_LCD_H / 2, 214, dim);
    faculty175_display_draw_centered_text("DEATH STAR", 204, white);
    faculty175_display_draw_centered_text("COPY /bust_cache/deathstar.a1v", 232, dim);
    faculty175_display_flush();
}

void faculty175_face_deathstar_draw(uint32_t anim_ms)
{
    if (!deathstar_open()) {
        draw_missing(anim_ms);
        return;
    }
    const uint32_t frame = (uint32_t)(((uint64_t)anim_ms * (uint64_t)s_fps / 1000u) % s_frame_count);
    if (!s_logged_first_draw) {
        s_logged_first_draw = true;
        ESP_LOGI(TAG, "drawing video frame=%u zipped=%s", (unsigned)frame, s_zipped ? "yes" : "no");
    }
    if (!load_frame(frame)) {
        draw_missing(anim_ms);
        return;
    }
    if (!(s_zipped ? draw_packed_1bpp_frame() : draw_rle_frame())) {
        draw_missing(anim_ms);
        return;
    }
    faculty175_display_flush();
}
