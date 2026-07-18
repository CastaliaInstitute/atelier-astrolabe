#include "faculty175_deep_sleep.h"

#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "faculty175_board.h"

#define DEEP_SLEEP_MAGIC 0x4c534450u /* LSDP */
#define DEEP_SLEEP_MIN_S 60u
#define DEEP_SLEEP_MAX_S (7u * 24u * 60u * 60u)
#define DEEP_SLEEP_WAKE_GPIO GPIO_NUM_0

typedef struct {
    uint32_t magic;
    uint32_t requested_sleep_s;
    uint32_t started_epoch_s;
    int32_t start_battery_percent;
    uint16_t start_battery_mv;
    uint16_t reserved;
} retained_sleep_t;

static const char *TAG = "faculty175_sleep";
RTC_DATA_ATTR static retained_sleep_t s_retained;
static portMUX_TYPE s_request_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_pending_sleep_s;

bool faculty175_deep_sleep_request(uint32_t sleep_s)
{
    if (sleep_s < DEEP_SLEEP_MIN_S || sleep_s > DEEP_SLEEP_MAX_S) {
        return false;
    }
    portENTER_CRITICAL(&s_request_mux);
    s_pending_sleep_s = sleep_s;
    portEXIT_CRITICAL(&s_request_mux);
    return true;
}

void faculty175_deep_sleep_cancel(void)
{
    portENTER_CRITICAL(&s_request_mux);
    s_pending_sleep_s = 0;
    portEXIT_CRITICAL(&s_request_mux);
}

bool faculty175_deep_sleep_request_pending(void)
{
    portENTER_CRITICAL(&s_request_mux);
    const bool pending = s_pending_sleep_s != 0;
    portEXIT_CRITICAL(&s_request_mux);
    return pending;
}

void faculty175_deep_sleep_status(faculty175_deep_sleep_status_t *out)
{
    if (out == NULL) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->pending = faculty175_deep_sleep_request_pending();
    out->wake_cause = (int)esp_sleep_get_wakeup_cause();
    if (s_retained.magic != DEEP_SLEEP_MAGIC) {
        return;
    }
    out->retained = true;
    out->requested_sleep_s = s_retained.requested_sleep_s;
    out->started_epoch_s = s_retained.started_epoch_s;
    out->start_battery_percent = s_retained.start_battery_percent;
    out->start_battery_mv = s_retained.start_battery_mv;
    out->completed = out->wake_cause == ESP_SLEEP_WAKEUP_TIMER ||
                     out->wake_cause == ESP_SLEEP_WAKEUP_EXT0;
}

void faculty175_deep_sleep_enter(const faculty175_pmu_status_t *pmu)
{
    uint32_t sleep_s = 0;
    portENTER_CRITICAL(&s_request_mux);
    sleep_s = s_pending_sleep_s;
    s_pending_sleep_s = 0;
    portEXIT_CRITICAL(&s_request_mux);
    if (sleep_s < DEEP_SLEEP_MIN_S || pmu == NULL || !pmu->present ||
        !pmu->battery_present || pmu->vbus_in || pmu->charging) {
        ESP_LOGE(TAG, "deep sleep entry rejected: request=%lu battery=%d vbus=%d charging=%d",
                 (unsigned long)sleep_s,
                 pmu != NULL && pmu->battery_present,
                 pmu != NULL && pmu->vbus_in,
                 pmu != NULL && pmu->charging);
        return;
    }

    const time_t now = time(NULL);
    s_retained = (retained_sleep_t){
        .magic = DEEP_SLEEP_MAGIC,
        .requested_sleep_s = sleep_s,
        .started_epoch_s = now > 1700000000 ? (uint32_t)now : 0,
        .start_battery_percent = pmu->battery_percent,
        .start_battery_mv = pmu->battery_mv,
    };

    const esp_err_t audio_err = faculty175_audio_prepare_deep_sleep(1000);
    if (audio_err != ESP_OK) {
        ESP_LOGE(TAG, "deep sleep entry rejected: audio quiesce failed: %s",
                 esp_err_to_name(audio_err));
        return;
    }
    faculty175_board_set_backlight(0);
    faculty175_board_display_on(false);
    faculty175_display_flush_suspended_set(true);
    vTaskDelay(pdMS_TO_TICKS(100));
    faculty175_pmu_prepare_deep_sleep();

    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)sleep_s * 1000000ULL));
    ESP_ERROR_CHECK(rtc_gpio_pullup_en(DEEP_SLEEP_WAKE_GPIO));
    ESP_ERROR_CHECK(rtc_gpio_pulldown_dis(DEEP_SLEEP_WAKE_GPIO));
    ESP_ERROR_CHECK(esp_sleep_enable_ext0_wakeup(DEEP_SLEEP_WAKE_GPIO, 0));
    ESP_LOGI(TAG,
             "entering deep sleep for %lu s; timer or GPIO0 wakes",
             (unsigned long)sleep_s);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
