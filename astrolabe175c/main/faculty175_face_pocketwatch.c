#include "faculty175_board.h"
#include "faculty175_lvgl.h"

void faculty175_face_pocketwatch_draw(uint32_t anim_ms)
{
    if (faculty175_lvgl_draw_face(FACULTY175_FACE_POCKETWATCH, anim_ms)) {
        return;
    }
    faculty175_display_draw_pocketwatch("READY", anim_ms, false);
}
