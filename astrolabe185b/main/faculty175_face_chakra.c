#include "faculty175_face_native.h"

void faculty175_face_chakra_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_CHAKRA,
        .title = "Chakra",
        .subtitle = "SOLFEGGIO",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 4,
        .a = "SEVEN CENTERS",
        .b = "TONE LADDER",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
