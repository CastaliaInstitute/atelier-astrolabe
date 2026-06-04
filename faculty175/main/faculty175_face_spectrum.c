#include "faculty175_face_native.h"

void faculty175_face_spectrum_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_SPECTRUM,
        .title = "Spectrum",
        .subtitle = "AUDIO FIELD",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 3,
        .a = "MIC SPECTRUM",
        .b = "LIVE INPUT",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
