#pragma once

class Arduino_GFX;

/** Compiled-in recovery UI — never depends on face-pack partition. */
void pm_face_safe_draw(Arduino_GFX *gfx, const char *status_line);
