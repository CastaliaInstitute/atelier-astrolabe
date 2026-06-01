#include "faculty175_gesture.h"

#include "atom_log.h"
#include "faculty175_touch.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "faculty_gesture";

#define GESTURE_SWIPE_MIN_PX 36
#define GESTURE_SWIPE_MAX_MS 900
#define GESTURE_POLL_MS 10

static faculty175_gesture_t s_pending = {
    .kind = FACULTY175_GESTURE_NONE,
};

static bool s_down;
static bool s_swipe_fired;
static uint32_t s_t_down;
static int16_t s_x0;
static int16_t s_y0;
static int16_t s_last_x;
static int16_t s_last_y;
static int16_t s_madx;
static int16_t s_mady;

static int16_t i16_abs(int16_t v)
{
    return v < 0 ? (int16_t)(-v) : v;
}

static int16_t i16_max(int16_t a, int16_t b)
{
    return a > b ? a : b;
}

static int16_t centroid1(const int16_t *v, uint8_t n)
{
    if (n == 0) {
        return 0;
    }
    int32_t sum = 0;
    for (uint8_t i = 0; i < n; ++i) {
        sum += v[i];
    }
    return (int16_t)(sum / (int32_t)n);
}

static bool horizontal_swipe(int16_t madx, int16_t mady)
{
    return madx >= GESTURE_SWIPE_MIN_PX && madx >= mady;
}

static void queue_swipe(int16_t cx, int16_t cy)
{
    if (s_swipe_fired || s_pending.kind != FACULTY175_GESTURE_NONE) {
        return;
    }
    s_pending.kind = (cx > s_x0) ? FACULTY175_GESTURE_SWIPE_RIGHT : FACULTY175_GESTURE_SWIPE_LEFT;
    s_pending.x = cx;
    s_pending.y = cy;
    s_swipe_fired = true;
    ATOM_LOG_STAGE(TAG, "gesture", "swipe %s dx=%d dy=%d",
                   s_pending.kind == FACULTY175_GESTURE_SWIPE_LEFT ? "left" : "right", (int)s_madx, (int)s_mady);
}

static void try_swipe(uint32_t now_ms, int16_t cx, int16_t cy)
{
    if (!s_down || s_swipe_fired) {
        return;
    }
    if ((now_ms - s_t_down) > GESTURE_SWIPE_MAX_MS) {
        return;
    }
    if (!horizontal_swipe(s_madx, s_mady)) {
        return;
    }
    queue_swipe(cx, cy);
}

static void on_release(uint32_t now_ms, int16_t cx, int16_t cy)
{
    if (s_swipe_fired) {
        return;
    }
    if ((now_ms - s_t_down) > GESTURE_SWIPE_MAX_MS) {
        return;
    }
    if (!horizontal_swipe(s_madx, s_mady)) {
        return;
    }
    queue_swipe(cx, cy);
}

void faculty175_gesture_poll(uint32_t now_ms)
{
    if (!faculty175_touch_ready()) {
        return;
    }

    int16_t xs[2];
    int16_t ys[2];
    const uint8_t n = faculty175_touch_sample(xs, ys, 1);
    if (n == 0) {
        if (s_down) {
            const int16_t cx = s_last_x;
            const int16_t cy = s_last_y;
            s_down = false;
            on_release(now_ms, cx, cy);
        }
        return;
    }

    const int16_t cx = centroid1(xs, n);
    const int16_t cy = centroid1(ys, n);
    if (!s_down) {
        s_down = true;
        s_swipe_fired = false;
        s_t_down = now_ms;
        s_x0 = cx;
        s_y0 = cy;
        s_last_x = cx;
        s_last_y = cy;
        s_madx = 0;
        s_mady = 0;
        return;
    }

    s_last_x = cx;
    s_last_y = cy;
    s_madx = i16_max(s_madx, i16_abs((int16_t)(cx - s_x0)));
    s_mady = i16_max(s_mady, i16_abs((int16_t)(cy - s_y0)));
    (void)s_y0;

    try_swipe(now_ms, cx, cy);
}

bool faculty175_gesture_consume(faculty175_gesture_t *out)
{
    if (s_pending.kind == FACULTY175_GESTURE_NONE) {
        return false;
    }
    if (out != NULL) {
        *out = s_pending;
    }
    s_pending.kind = FACULTY175_GESTURE_NONE;
    return true;
}

static void gesture_task(void *arg)
{
    (void)arg;
    while (true) {
        const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        faculty175_gesture_poll(now_ms);
        vTaskDelay(pdMS_TO_TICKS(GESTURE_POLL_MS));
    }
}

void faculty175_gesture_start_task(void)
{
    if (!faculty175_touch_ready()) {
        ESP_LOGI(TAG, "touch absent — gesture task skipped");
        return;
    }
    (void)xTaskCreate(gesture_task, "gesture", 3072, NULL, 6, NULL);
}
