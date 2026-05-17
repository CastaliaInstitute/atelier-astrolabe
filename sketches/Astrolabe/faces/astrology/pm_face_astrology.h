#pragma once

#include <cstddef>
#include <cstdint>

struct tm;

const char *pm_face_zodiac_abbr(double lon_deg);
int pm_face_astrology_sign_label_radius(int r_outer);
int pm_face_astrology_planet_radius(int r_outer);
void pm_face_astrology_draw(const struct tm *tm_local, bool valid_local, int highlight_body,
                                int highlight_sign, bool pulse_chart);
void pm_face_astrology_draw_voice_screen(const char *status, int highlight_body, int highlight_sign,
                                    bool pulse_chart, float thinking_progress = -1.f);
bool pm_face_astrology_build_voice_message(char *buf, size_t cap);
bool pm_face_astrology_build_system_prompt_impl();
bool pm_face_astrology_build_system_prompt(char *voice_msg, size_t voice_cap, char *sys_out, size_t sys_cap);

