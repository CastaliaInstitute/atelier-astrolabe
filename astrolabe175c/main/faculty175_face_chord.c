#include "faculty175_face_native.h"

void faculty175_face_chord_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_CHORD,
        .title = "Chord",
        .subtitle = "AUTOHARP",
        .style = FACULTY175_NATIVE_INSTRUMENT,
        .hue = 1,
        .a = "CHORD PADS",
        .b = "MIDI HARMONY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
