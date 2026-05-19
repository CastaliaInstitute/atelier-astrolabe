#pragma once

#include <cstdint>

/** Sub-pages on [ClockFace::Settings] (swipe left/right). */
enum class SettingsPage : uint8_t {
  WiFi = 0,
  Castalia,
  kCount,
};

SettingsPage pm_settings_page(void);
void pm_settings_set_page(SettingsPage page);
void pm_settings_cycle(int delta);

void pm_settings_draw(void);
