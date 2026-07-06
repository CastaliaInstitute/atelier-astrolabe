#include "faculty175_face_native.h"

void faculty175_face_phenology_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_PHENOLOGY,
        .title = "Phenology",
        .subtitle = "LIVING ALMANAC",
        .style = FACULTY175_NATIVE_CELESTIAL,
        .hue = 2,
        .a = "PLANT / ANIMAL",
        .b = "SEASONAL ACTION",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
