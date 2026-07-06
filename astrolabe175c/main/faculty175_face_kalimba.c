#include "faculty175_face_native.h"

void faculty175_face_kalimba_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_KALIMBA,
        .title = "Kalimba",
        .subtitle = "TINES",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 2,
        .a = "PENTATONIC",
        .b = "PLUCK FIELD",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
