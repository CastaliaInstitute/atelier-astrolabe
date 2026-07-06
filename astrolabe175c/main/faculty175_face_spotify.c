#include "faculty175_face_native.h"

void faculty175_face_spotify_draw(uint32_t anim_ms)
{
    static const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_SPOTIFY,
        .title = "Spotify",
        .subtitle = "REMOTE MEDIA",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 2,
        .a = "NOW PLAYING",
        .b = "CONNECTOR READY",
        .c = "",
    };
    faculty175_face_native_draw(&face, anim_ms);
}
