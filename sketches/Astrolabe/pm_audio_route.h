#pragma once

#include <stddef.h>
#include <stdbool.h>

#include "faces/pm_faces.h"
#include "pm_gesture.h"

/** Where clock-face / host audio is routed (UAC builds only). */
enum class PmAudioRoute : uint8_t {
  Onboard = 0,
  Usb = 1,
};

/** Load persisted route; safe on all builds. */
void pm_audio_route_begin(void);

/** True when firmware includes USB Audio Class speaker gadget. */
bool pm_audio_route_uac_available(void);

PmAudioRoute pm_audio_route_get(void);

/** Apply route (releases PCM / restarts mic consumers as needed). */
void pm_audio_route_set(PmAudioRoute route);

bool pm_audio_route_output_usb(void);
bool pm_audio_route_input_usb(void);

/** Swipe up → USB, swipe down → onboard (skips faces that use vertical swipes). */
bool pm_audio_route_handle_gesture(PmGestureKind kind, ClockFace face, char *banner, size_t banner_len);

void pm_audio_route_label(char *buf, size_t len);
