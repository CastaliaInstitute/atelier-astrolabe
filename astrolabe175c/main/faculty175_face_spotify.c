#include "faculty175_face_native.h"
#include "faculty175_spotify.h"

void faculty175_face_spotify_draw(uint32_t anim_ms)
{
    faculty175_spotify_poll();
    faculty175_spotify_status_t status = {};
    faculty175_spotify_status(&status);
    const faculty175_native_face_t face = {
        .id = FACULTY175_FACE_SPOTIFY,
        .title = "Spotify",
        .subtitle = status.configured ? (status.busy ? "CONNECTING" : (status.is_playing ? "NOW PLAYING" : "PAUSED"))
                                      : "PAIR AT /SKYPE",
        .style = FACULTY175_NATIVE_STATUS,
        .hue = 2,
        .a = status.track[0] != '\0' ? status.track : (status.configured ? "OPEN SPOTIFY" : "PAIR SPOTIFY"),
        .b = status.artist[0] != '\0' ? status.artist : status.error,
        .c = status.device,
    };
    faculty175_face_native_draw(&face, anim_ms);
}

bool faculty175_face_spotify_action(void)
{
    return faculty175_spotify_toggle();
}
