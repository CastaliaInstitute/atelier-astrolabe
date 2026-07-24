#include "faculty175_gesture.h"

#include <math.h>

#include "faculty175_board.h"
#include "faculty175_faces.h"
#include "faculty175_log.h"
#include "faculty175_touch.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "faculty_gesture";

#define GESTURE_SWIPE_MIN_PX 32
#define GESTURE_SWIPE_MAX_MS 900
#define GESTURE_TAP_MAX_PX 12
#define GESTURE_TAP_MAX_MS 650
#define GESTURE_LONG_TAP_MS 480
#define GESTURE_LONG_TAP_MAX_PX 48
#define GESTURE_CENTER_LONG_TAP_MAX_R 150
#define GESTURE_POLL_MS 16
#define GESTURE_QUEUE_DEPTH 8
#define GESTURE_NAV_MAX_AGE_MS 1000
#define GESTURE_TAP_MAX_AGE_MS 650
#define GESTURE_TASK_PRIO 5
#define GESTURE_TASK_STACK 4096
#define GESTURE_TASK_CORE 1
#define BEZEL_MIN_R 196
#define BEZEL_MAX_R 236
#define BEZEL_TAP_MAX_PX 18
#define BEZEL_TAP_MAX_MS 900
#define BEZEL_STEP_FRACTION 0.72f
#define FACULTY175_ENABLE_BEZEL_TOUCH 0

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static bool s_down;
static bool s_swipe_fired;
static bool s_long_tap_fired;
static uint32_t s_t_down;
static int16_t s_x0;
static int16_t s_y0;
static int16_t s_last_x;
static int16_t s_last_y;
static int16_t s_madx;
static int16_t s_mady;
static bool s_bezel_down;
static float s_bezel_last_angle;
static float s_bezel_accum_angle;

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
    /* A deliberate swipe must have a clear dominant axis. This keeps a
     * diagonal finger lift or a touch-indicator wobble from changing faces. */
    return madx >= GESTURE_SWIPE_MIN_PX && ((int32_t)madx * 5) >= ((int32_t)mady * 8);
}

static bool vertical_swipe(int16_t madx, int16_t mady)
{
    return mady >= GESTURE_SWIPE_MIN_PX && ((int32_t)mady * 5) >= ((int32_t)madx * 8);
}

static const char *gesture_name(faculty175_gesture_kind_t kind)
{
    switch (kind) {
        case FACULTY175_GESTURE_SWIPE_LEFT:
            return "left";
        case FACULTY175_GESTURE_SWIPE_RIGHT:
            return "right";
        case FACULTY175_GESTURE_SWIPE_UP:
            return "up";
        case FACULTY175_GESTURE_SWIPE_DOWN:
            return "down";
        case FACULTY175_GESTURE_TAP:
            return "tap";
        case FACULTY175_GESTURE_LONG_TAP:
            return "long tap";
        case FACULTY175_GESTURE_BEZEL_TAP:
            return "bezel tap";
        case FACULTY175_GESTURE_BEZEL_ROTATE_CW:
            return "bezel cw";
        case FACULTY175_GESTURE_BEZEL_ROTATE_CCW:
            return "bezel ccw";
        default:
            return "?";
    }
}

static bool gesture_is_navigation_step(faculty175_gesture_kind_t kind)
{
    return kind == FACULTY175_GESTURE_SWIPE_LEFT ||
           kind == FACULTY175_GESTURE_SWIPE_RIGHT ||
           kind == FACULTY175_GESTURE_SWIPE_UP ||
           kind == FACULTY175_GESTURE_SWIPE_DOWN ||
           kind == FACULTY175_GESTURE_BEZEL_ROTATE_CW ||
           kind == FACULTY175_GESTURE_BEZEL_ROTATE_CCW;
}

static uint32_t gesture_max_age_ms(faculty175_gesture_kind_t kind)
{
    return gesture_is_navigation_step(kind) ? GESTURE_NAV_MAX_AGE_MS : GESTURE_TAP_MAX_AGE_MS;
}

static void drop_queued_navigation_steps(void)
{
    if (s_queue == NULL) {
        return;
    }

    faculty175_gesture_t kept[GESTURE_QUEUE_DEPTH];
    size_t kept_count = 0;
    faculty175_gesture_t gesture = {};
    while (xQueueReceive(s_queue, &gesture, 0) == pdTRUE) {
        if (gesture_is_navigation_step(gesture.kind)) {
            continue;
        }
        if (kept_count < GESTURE_QUEUE_DEPTH) {
            kept[kept_count++] = gesture;
        }
    }
    for (size_t i = 0; i < kept_count; ++i) {
        (void)xQueueSend(s_queue, &kept[i], 0);
    }
}

static void queue_gesture(faculty175_gesture_kind_t kind, int16_t cx, int16_t cy, int16_t value)
{
    if (kind == FACULTY175_GESTURE_NONE || s_queue == NULL) {
        return;
    }
    if (gesture_is_navigation_step(kind)) {
        drop_queued_navigation_steps();
    }
    faculty175_gesture_t gesture = {
        .kind = kind,
        .x = cx,
        .y = cy,
        .value = value,
        .queued_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS),
    };
    if (xQueueSend(s_queue, &gesture, 0) != pdTRUE) {
        /* A noisy controller can report many tap releases while a real swipe
         * is being classified.  Never let one of those low-value taps evict
         * a queued navigation step; otherwise a perfectly valid swipe can
         * disappear before input_task gets its next time slice. */
        if (!gesture_is_navigation_step(kind)) {
            return;
        }
        faculty175_gesture_t dropped = {};
        (void)xQueueReceive(s_queue, &dropped, 0);
        (void)xQueueSend(s_queue, &gesture, 0);
    }
}

static void queue_swipe(int16_t cx, int16_t cy)
{
    if (s_swipe_fired || s_queue == NULL) {
        return;
    }
    faculty175_gesture_kind_t kind = FACULTY175_GESTURE_NONE;
    if (horizontal_swipe(s_madx, s_mady) && (!vertical_swipe(s_madx, s_mady) || s_madx >= s_mady)) {
        kind = (cx > s_x0) ? FACULTY175_GESTURE_SWIPE_RIGHT : FACULTY175_GESTURE_SWIPE_LEFT;
    } else if (vertical_swipe(s_madx, s_mady)) {
        kind = (cy > s_y0) ? FACULTY175_GESTURE_SWIPE_DOWN : FACULTY175_GESTURE_SWIPE_UP;
    }
    if (kind == FACULTY175_GESTURE_NONE) {
        return;
    }
    queue_gesture(kind, cx, cy, 0);
    s_swipe_fired = true;
    FACULTY175_LOG_STAGE(TAG, "gesture", "swipe %s dx=%d dy=%d", gesture_name(kind), (int)s_madx, (int)s_mady);
}

static int32_t point_radius_sq(int16_t x, int16_t y)
{
    const int32_t dx = (int32_t)x - (FACULTY175_LCD_W / 2);
    const int32_t dy = (int32_t)y - (FACULTY175_LCD_H / 2);
    return dx * dx + dy * dy;
}

static bool point_on_bezel(int16_t x, int16_t y)
{
    const int32_t r2 = point_radius_sq(x, y);
    return r2 >= (int32_t)BEZEL_MIN_R * BEZEL_MIN_R && r2 <= (int32_t)BEZEL_MAX_R * BEZEL_MAX_R;
}

static bool point_in_center_long_tap_zone(int16_t x, int16_t y)
{
    const int32_t r2 = point_radius_sq(x, y);
    return r2 <= (int32_t)GESTURE_CENTER_LONG_TAP_MAX_R * GESTURE_CENTER_LONG_TAP_MAX_R;
}

static float bezel_angle(int16_t x, int16_t y)
{
    return atan2f((float)y - (float)(FACULTY175_LCD_H / 2), (float)x - (float)(FACULTY175_LCD_W / 2));
}

static float wrap_delta_angle(float delta)
{
    while (delta > (float)M_PI) {
        delta -= 2.0f * (float)M_PI;
    }
    while (delta < -(float)M_PI) {
        delta += 2.0f * (float)M_PI;
    }
    return delta;
}

static int16_t bezel_index_for_point(int16_t x, int16_t y)
{
    size_t current_index = 0;
    size_t count = 0;
    if (!faculty175_faces_nav_position(&current_index, &count) || count == 0) {
        return -1;
    }
    (void)current_index;

    const float full = 2.0f * (float)M_PI;
    const float step = full / (float)count;
    const float start = -0.5f * (float)M_PI - (step * 0.5f);
    float a = bezel_angle(x, y) - start;
    while (a < 0.0f) {
        a += full;
    }
    while (a >= full) {
        a -= full;
    }
    int index = (int)floorf(a / step);
    if (index < 0) {
        index = 0;
    } else if ((size_t)index >= count) {
        index = (int)count - 1;
    }
    return (int16_t)index;
}

static void handle_bezel_move(int16_t cx, int16_t cy)
{
    if (!s_bezel_down || !point_on_bezel(cx, cy)) {
        return;
    }
    size_t index = 0;
    size_t count = 0;
    if (!faculty175_faces_nav_position(&index, &count) || count <= 1) {
        return;
    }
    (void)index;

    const float angle = bezel_angle(cx, cy);
    s_bezel_accum_angle += wrap_delta_angle(angle - s_bezel_last_angle);
    s_bezel_last_angle = angle;

    const float threshold = (2.0f * (float)M_PI / (float)count) * BEZEL_STEP_FRACTION;
    while (s_bezel_accum_angle >= threshold) {
        queue_gesture(FACULTY175_GESTURE_BEZEL_ROTATE_CW, cx, cy, 1);
        s_bezel_accum_angle -= threshold;
        s_swipe_fired = true;
        FACULTY175_LOG_STAGE(TAG, "gesture", "bezel rotate cw");
    }
    while (s_bezel_accum_angle <= -threshold) {
        queue_gesture(FACULTY175_GESTURE_BEZEL_ROTATE_CCW, cx, cy, -1);
        s_bezel_accum_angle += threshold;
        s_swipe_fired = true;
        FACULTY175_LOG_STAGE(TAG, "gesture", "bezel rotate ccw");
    }
}

static void try_swipe(uint32_t now_ms, int16_t cx, int16_t cy)
{
    if (!s_down || s_swipe_fired || s_long_tap_fired) {
        return;
    }
    if ((now_ms - s_t_down) > GESTURE_SWIPE_MAX_MS) {
        return;
    }
    if (!horizontal_swipe(s_madx, s_mady) && !vertical_swipe(s_madx, s_mady)) {
        return;
    }
    queue_swipe(cx, cy);
}

static void try_long_tap(uint32_t now_ms, int16_t cx, int16_t cy)
{
    if (!s_down || s_swipe_fired || s_long_tap_fired) {
        return;
    }
    if ((now_ms - s_t_down) < GESTURE_LONG_TAP_MS) {
        return;
    }
    if (s_madx > GESTURE_LONG_TAP_MAX_PX || s_mady > GESTURE_LONG_TAP_MAX_PX) {
        return;
    }
    if (!point_in_center_long_tap_zone(s_x0, s_y0)) {
        return;
    }
    queue_gesture(FACULTY175_GESTURE_LONG_TAP, cx, cy, 0);
    s_long_tap_fired = true;
    FACULTY175_LOG_STAGE(TAG, "gesture", "center long tap");
}

static void on_release(uint32_t now_ms, int16_t cx, int16_t cy)
{
    if (!s_long_tap_fired && (now_ms - s_t_down) >= GESTURE_LONG_TAP_MS &&
        s_madx <= GESTURE_LONG_TAP_MAX_PX && s_mady <= GESTURE_LONG_TAP_MAX_PX &&
        point_in_center_long_tap_zone(s_x0, s_y0)) {
        queue_gesture(FACULTY175_GESTURE_LONG_TAP, cx, cy, 0);
        s_long_tap_fired = true;
        FACULTY175_LOG_STAGE(TAG, "gesture", "center long tap release");
        return;
    }
    if (FACULTY175_ENABLE_BEZEL_TOUCH && s_bezel_down) {
        if (!s_swipe_fired && !s_long_tap_fired && (now_ms - s_t_down) <= BEZEL_TAP_MAX_MS &&
            s_madx <= BEZEL_TAP_MAX_PX && s_mady <= BEZEL_TAP_MAX_PX) {
            const int16_t index = bezel_index_for_point(cx, cy);
            if (index >= 0) {
                queue_gesture(FACULTY175_GESTURE_BEZEL_TAP, cx, cy, index);
                FACULTY175_LOG_STAGE(TAG, "gesture", "bezel tap index=%d", (int)index);
            }
        }
        return;
    }
    if (s_swipe_fired) {
        return;
    }
    if (s_long_tap_fired) {
        return;
    }
    if ((now_ms - s_t_down) <= GESTURE_TAP_MAX_MS &&
        s_madx <= GESTURE_TAP_MAX_PX && s_mady <= GESTURE_TAP_MAX_PX) {
        queue_gesture(FACULTY175_GESTURE_TAP, cx, cy, 0);
        FACULTY175_LOG_STAGE(TAG, "gesture", "tap");
        return;
    }
    if ((now_ms - s_t_down) > GESTURE_SWIPE_MAX_MS) {
        return;
    }
    if (!horizontal_swipe(s_madx, s_mady) && !vertical_swipe(s_madx, s_mady)) {
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
            faculty175_touch_state_update(false, cx, cy, now_ms);
            faculty175_display_touch_visual_update(cx, cy, false, now_ms);
            on_release(now_ms, cx, cy);
        }
        return;
    }

    const int16_t cx = centroid1(xs, n);
    const int16_t cy = centroid1(ys, n);
    if (!s_down) {
        s_down = true;
        s_swipe_fired = false;
        s_long_tap_fired = false;
        s_t_down = now_ms;
        s_x0 = cx;
        s_y0 = cy;
        s_last_x = cx;
        s_last_y = cy;
        s_madx = 0;
        s_mady = 0;
        s_bezel_down = FACULTY175_ENABLE_BEZEL_TOUCH && point_on_bezel(cx, cy);
        s_bezel_last_angle = s_bezel_down ? bezel_angle(cx, cy) : 0.0f;
        s_bezel_accum_angle = 0.0f;
        faculty175_touch_state_update(true, cx, cy, now_ms);
        faculty175_display_touch_visual_update(cx, cy, true, now_ms);
        return;
    }

    s_last_x = cx;
    s_last_y = cy;
    s_madx = i16_max(s_madx, i16_abs((int16_t)(cx - s_x0)));
    s_mady = i16_max(s_mady, i16_abs((int16_t)(cy - s_y0)));
    (void)s_y0;
    faculty175_touch_state_update(true, cx, cy, now_ms);
    faculty175_display_touch_visual_update(cx, cy, true, now_ms);

    if (FACULTY175_ENABLE_BEZEL_TOUCH && s_bezel_down) {
        handle_bezel_move(cx, cy);
        try_long_tap(now_ms, cx, cy);
    }
    try_swipe(now_ms, cx, cy);
}

bool faculty175_gesture_consume(faculty175_gesture_t *out)
{
    if (s_queue == NULL) {
        return false;
    }
    const uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    while (true) {
        faculty175_gesture_t gesture = {};
        if (xQueueReceive(s_queue, &gesture, 0) != pdTRUE) {
            return false;
        }
        const uint32_t max_age_ms = gesture_max_age_ms(gesture.kind);
        if (gesture.queued_ms != 0 && (now_ms - gesture.queued_ms) > max_age_ms) {
            FACULTY175_LOG_STAGE(TAG,
                                 "gesture",
                                 "drop stale %s age_ms=%u",
                                 gesture_name(gesture.kind),
                                 (unsigned)(now_ms - gesture.queued_ms));
            continue;
        }
        if (out != NULL) {
            *out = gesture;
        }
        return true;
    }
}

void faculty175_gesture_flush(void)
{
    if (s_queue == NULL) {
        return;
    }
    faculty175_gesture_t dropped = {};
    while (xQueueReceive(s_queue, &dropped, 0) == pdTRUE) {
    }
}

bool faculty175_gesture_inject(faculty175_gesture_kind_t kind, int16_t x, int16_t y, int16_t value)
{
    if (s_queue == NULL || kind == FACULTY175_GESTURE_NONE) {
        return false;
    }
    queue_gesture(kind, x, y, value);
    FACULTY175_LOG_STAGE(TAG, "gesture", "inject %s x=%d y=%d value=%d", gesture_name(kind), (int)x, (int)y,
                         (int)value);
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
    if (s_task != NULL) {
        return;
    }
    if (s_queue == NULL) {
        s_queue = xQueueCreate(GESTURE_QUEUE_DEPTH, sizeof(faculty175_gesture_t));
    }
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "gesture queue alloc failed");
        return;
    }
    if (!faculty175_touch_ready()) {
        ESP_LOGI(TAG, "touch absent — gesture queue ready for synthetic input");
        return;
    }
    if (xTaskCreatePinnedToCore(gesture_task, "gesture", GESTURE_TASK_STACK, NULL,
                                GESTURE_TASK_PRIO, &s_task, GESTURE_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG, "gesture task start failed");
        s_task = NULL;
    }
}

TaskHandle_t faculty175_gesture_task_handle(void)
{
    return s_task;
}
