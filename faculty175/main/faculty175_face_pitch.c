#include "faculty175_face_native.h"

void faculty175_face_pitch_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_PITCH,
        .title = "Pitch Pipe",
        .subtitle = "REFERENCE",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 1,
        .a = "A4 / ROOT",
        .b = "TAP TONE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
