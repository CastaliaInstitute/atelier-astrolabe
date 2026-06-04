#include "faculty175_face_native.h"

void faculty175_face_drone_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_DRONE,
        .title = "Drone",
        .subtitle = "ROOT FIFTH OCTAVE",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 4,
        .a = "SUSTAIN TOGGLE",
        .b = "ROOT SELECT",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
