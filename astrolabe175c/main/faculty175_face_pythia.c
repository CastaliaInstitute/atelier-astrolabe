#include "faculty175_face_native.h"

void faculty175_face_pythia_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_PYTHIA,
        .title = "Pythia",
        .subtitle = "DELPHI",
        .style = FACULTY175_NATIVE_TEXT,
        .hue = 5,
        .a = "OBTUSE RESPONSE",
        .b = "ASK ORACLE",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
