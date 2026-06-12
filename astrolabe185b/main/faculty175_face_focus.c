#include "faculty175_face_native.h"

void faculty175_face_focus_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_FOCUS,
        .title = "Focus Timer",
        .subtitle = "POMODORO",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 1,
        .a = "TAP START PAUSE",
        .b = "PRESET TIMER",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
