#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define FACULTY175_FAMILY_SUBJECT_NAME_MAX 24
#define FACULTY175_FAMILY_SUBJECT_MAX 6

enum {
    FACULTY175_FAMILY_FLAG_STRESS_VALID = 1u << 0,
    FACULTY175_FAMILY_FLAG_HRV_VALID = 1u << 1,
    FACULTY175_FAMILY_FLAG_HR_VALID = 1u << 2,
    FACULTY175_FAMILY_FLAG_SPO2_VALID = 1u << 3,
    FACULTY175_FAMILY_FLAG_SLEEP_VALID = 1u << 4,
    FACULTY175_FAMILY_FLAG_BATTERY_VALID = 1u << 5,
};

typedef struct {
    bool valid;
    uint8_t subject_id;
    char subject_name[FACULTY175_FAMILY_SUBJECT_NAME_MAX];
    uint8_t source_mac[6];
    uint32_t seq;
    uint32_t last_rx_ms;
    uint32_t age_ms;
    uint8_t flags;
    uint8_t stress;
    uint8_t hrv_ms;
    uint8_t heart_rate_bpm;
    uint8_t spo2_percent;
    uint16_t sleep_total_min;
    uint16_t sleep_light_min;
    uint16_t sleep_deep_min;
    uint16_t sleep_rem_min;
    uint16_t sleep_awake_min;
    uint8_t battery_percent;
    int8_t stress_trend_30m;
    uint16_t day_sample_count;
    uint8_t day_stress_avg;
    uint8_t day_stress_peak;
    uint16_t day_high_stress_samples;
    uint16_t day_low_hrv_samples;
    uint8_t day_load_avg;
    uint8_t day_load_peak;
    uint32_t day_first_ms;
    uint32_t day_last_ms;
} faculty175_family_wellness_t;

esp_err_t faculty175_family_init(void);
void faculty175_family_tick(uint32_t now_ms);
bool faculty175_family_ready(void);
int faculty175_family_channel(void);
uint32_t faculty175_family_tx_count(void);
uint32_t faculty175_family_rx_count(void);
uint8_t faculty175_family_local_subject(char *name, size_t cap);
esp_err_t faculty175_family_send_now(void);
size_t faculty175_family_snapshot(faculty175_family_wellness_t *out, size_t cap);
bool faculty175_family_primary_partner(faculty175_family_wellness_t *out);
const char *faculty175_family_guidance_cue(const faculty175_family_wellness_t *state);
uint8_t faculty175_family_load_score(const faculty175_family_wellness_t *state);
uint16_t faculty175_family_sleep_debt_min(const faculty175_family_wellness_t *state);
void faculty175_family_format_summary(char *out, size_t cap);
void faculty175_family_format_day_summary(const faculty175_family_wellness_t *state, char *out, size_t cap);
void faculty175_family_format_voice_context(char *out, size_t cap);
