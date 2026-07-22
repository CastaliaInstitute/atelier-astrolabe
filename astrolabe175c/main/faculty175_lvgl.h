#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "faculty175_faces.h"
#include "faculty175_face_native.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t faculty175_lvgl_init(void);
bool faculty175_lvgl_preload_moon_texture(void);
bool faculty175_lvgl_ready(void);
void faculty175_lvgl_service(uint32_t now_ms);
bool faculty175_lvgl_face_supported(faculty175_face_id_t id);
bool faculty175_lvgl_draw_face(faculty175_face_id_t id, uint32_t anim_ms);
void faculty175_lvgl_force_full_refresh(void);
/** Present the Solar face as a sunrise: the lower disk is occluded by Earth. */
void faculty175_lvgl_set_sunrise_horizon(bool enabled);
bool faculty175_lvgl_draw_native_face(const faculty175_native_face_t *face, uint32_t anim_ms);
bool faculty175_lvgl_draw_nav(const faculty175_face_desc_t *center,
                              const faculty175_face_desc_t *left,
                              const faculty175_face_desc_t *right,
                              const faculty175_face_desc_t *up,
                              const faculty175_face_desc_t *down,
                              uint32_t anim_ms);
bool faculty175_lvgl_transition_nav(const faculty175_face_desc_t *center,
                                    bool vertical,
                                    int delta,
                                    uint32_t duration_ms);
bool faculty175_lvgl_transition_face(faculty175_face_id_t from_id,
                                     faculty175_face_id_t to_id,
                                     uint32_t anim_ms,
                                     bool vertical,
                                     int delta,
                                     uint32_t duration_ms,
                                     bool *animated_out);
bool faculty175_lvgl_faces_share_transition_screen(faculty175_face_id_t a, faculty175_face_id_t b);
bool faculty175_lvgl_animate_frames(const uint16_t *from,
                                    const uint16_t *to,
                                    bool vertical,
                                    int delta,
                                    uint32_t duration_ms);

#ifdef __cplusplus
}
#endif
