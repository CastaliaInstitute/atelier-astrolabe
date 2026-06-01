#include "atom_faculty_roster.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "atom_util.h"

static const char *TAG = "atom_roster";
static const char *kNs = "fac_roster";

static const atom_faculty_roster_entry_t kDefaultRoster[] = {
    {"nabokov", "Nabokov"},
    {"hesse", "Hesse"},
    {"a.huxley", "Huxley"},
    {"t.leary", "Leary"},
    {"m.shelley", "Mary Shelley"},
    {"j.austen", "Jane Austen"},
    {"a.jung", "Carl Jung"},
};

static void roster_key_slug(char *out, size_t cap, int index)
{
    snprintf(out, cap, "s%d", index);
}

static void roster_key_name(char *out, size_t cap, int index)
{
    snprintf(out, cap, "n%d", index);
}

static bool roster_read_entry(nvs_handle_t nvs, int index, atom_faculty_roster_entry_t *out)
{
    if (out == NULL || index < 0 || index >= ATOM_FACULTY_ROSTER_MAX) {
        return false;
    }
    char key_slug[8];
    char key_name[8];
    roster_key_slug(key_slug, sizeof(key_slug), index);
    roster_key_name(key_name, sizeof(key_name), index);

    size_t slug_len = sizeof(out->slug);
    size_t name_len = sizeof(out->name);
    if (nvs_get_str(nvs, key_slug, out->slug, &slug_len) != ESP_OK || out->slug[0] == '\0') {
        return false;
    }
    if (nvs_get_str(nvs, key_name, out->name, &name_len) != ESP_OK || out->name[0] == '\0') {
        atom_strlcpy(out->name, out->slug, sizeof(out->name));
    }
    return true;
}

static void roster_write_default(nvs_handle_t nvs)
{
    const int count = (int)(sizeof(kDefaultRoster) / sizeof(kDefaultRoster[0]));
    for (int i = 0; i < count && i < ATOM_FACULTY_ROSTER_MAX; ++i) {
        char key_slug[8];
        char key_name[8];
        roster_key_slug(key_slug, sizeof(key_slug), i);
        roster_key_name(key_name, sizeof(key_name), i);
        (void)nvs_set_str(nvs, key_slug, kDefaultRoster[i].slug);
        (void)nvs_set_str(nvs, key_name, kDefaultRoster[i].name);
    }
    (void)nvs_set_i32(nvs, "count", count);
    (void)nvs_set_i32(nvs, "active", 0);
    (void)nvs_commit(nvs);
    ESP_LOGI(TAG, "roster seeded (%d faculty)", count);
}

static bool roster_matches_default(nvs_handle_t nvs)
{
    const int expected = (int)(sizeof(kDefaultRoster) / sizeof(kDefaultRoster[0]));
    int32_t count = 0;
    if (nvs_get_i32(nvs, "count", &count) != ESP_OK || count != expected) {
        return false;
    }
    atom_faculty_roster_entry_t entry = {};
    if (!roster_read_entry(nvs, 0, &entry)) {
        return false;
    }
    return strcmp(entry.slug, kDefaultRoster[0].slug) == 0;
}

void atom_faculty_roster_ensure_default(void)
{
    nvs_handle_t nvs;
    if (nvs_open(kNs, NVS_READWRITE, &nvs) != ESP_OK) {
        ESP_LOGW(TAG, "roster NVS open failed");
        return;
    }

    int32_t count = 0;
    if (nvs_get_i32(nvs, "count", &count) != ESP_OK || count <= 0 || !roster_matches_default(nvs)) {
        roster_write_default(nvs);
    }
    nvs_close(nvs);
}

int atom_faculty_roster_count(void)
{
    nvs_handle_t nvs;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }
    int32_t count = 0;
    if (nvs_get_i32(nvs, "count", &count) != ESP_OK || count <= 0) {
        nvs_close(nvs);
        return (int)(sizeof(kDefaultRoster) / sizeof(kDefaultRoster[0]));
    }
    if (count > ATOM_FACULTY_ROSTER_MAX) {
        count = ATOM_FACULTY_ROSTER_MAX;
    }
    nvs_close(nvs);
    return (int)count;
}

bool atom_faculty_roster_get(int index, atom_faculty_roster_entry_t *out)
{
    if (out == NULL || index < 0) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) {
        if (index < (int)(sizeof(kDefaultRoster) / sizeof(kDefaultRoster[0]))) {
            atom_strlcpy(out->slug, kDefaultRoster[index].slug, sizeof(out->slug));
            atom_strlcpy(out->name, kDefaultRoster[index].name, sizeof(out->name));
            return true;
        }
        return false;
    }
    const bool ok = roster_read_entry(nvs, index, out);
    nvs_close(nvs);
    return ok;
}

int atom_faculty_roster_active_index(void)
{
    nvs_handle_t nvs;
    if (nvs_open(kNs, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }
    int32_t active = 0;
    (void)nvs_get_i32(nvs, "active", &active);
    nvs_close(nvs);
    const int count = atom_faculty_roster_count();
    if (count <= 0) {
        return 0;
    }
    if (active < 0 || active >= count) {
        return 0;
    }
    return (int)active;
}

bool atom_faculty_roster_active(atom_faculty_roster_entry_t *out)
{
    return atom_faculty_roster_get(atom_faculty_roster_active_index(), out);
}

bool atom_faculty_roster_set_active_index(int index)
{
    const int count = atom_faculty_roster_count();
    if (index < 0 || index >= count) {
        return false;
    }
    nvs_handle_t nvs;
    if (nvs_open(kNs, NVS_READWRITE, &nvs) != ESP_OK) {
        return false;
    }
    (void)nvs_set_i32(nvs, "active", index);
    (void)nvs_commit(nvs);
    nvs_close(nvs);
    return true;
}

int atom_faculty_roster_cycle_delta(int delta)
{
    const int count = atom_faculty_roster_count();
    if (count <= 0 || delta == 0) {
        return -1;
    }
    int next = atom_faculty_roster_active_index() + delta;
    while (next < 0) {
        next += count;
    }
    next %= count;
    if (!atom_faculty_roster_set_active_index(next)) {
        return -1;
    }
    atom_faculty_roster_entry_t entry = {};
    if (atom_faculty_roster_active(&entry)) {
        ESP_LOGI(TAG, "roster active -> %s (%s)", entry.name, entry.slug);
    }
    return next;
}

int atom_faculty_roster_cycle_next(void)
{
    return atom_faculty_roster_cycle_delta(1);
}
