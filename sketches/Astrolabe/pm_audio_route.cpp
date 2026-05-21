#include "pm_audio_route.h"

#include <Preferences.h>
#include <stdio.h>

#include "pm_speaker_pcm.h"
#include "pm_usb_uac.h"
#include "pm_log.h"
#include "sdkconfig.h"

static PmAudioRoute s_route = PmAudioRoute::Onboard;

static constexpr PmAudioRoute default_route() {
#if defined(ASTROLABE_UAC_DEFAULT_ROUTE_USB) && ASTROLABE_UAC_DEFAULT_ROUTE_USB
  return PmAudioRoute::Usb;
#else
  return PmAudioRoute::Onboard;
#endif
}

static bool uac_compiled(void) {
#if defined(CONFIG_UAC_SPEAKER_CHANNEL_NUM) && CONFIG_UAC_SPEAKER_CHANNEL_NUM > 0
  return true;
#else
  return false;
#endif
}

void pm_audio_route_begin(void) {
  s_route = default_route();
  pm_log_printf(false, "audio-route: begin default=%s uac=%d",
                s_route == PmAudioRoute::Usb ? "USB" : "onboard", uac_compiled() ? 1 : 0);
  if (!uac_compiled()) {
    return;
  }
#if defined(ASTROLABE_UAC_DEFAULT_ROUTE_USB) && ASTROLABE_UAC_DEFAULT_ROUTE_USB
  pm_log_printf(false, "audio-route: using build default USB");
  return;
#endif
  Preferences pref;
  if (!pref.begin("audio_rt", true)) {
    return;
  }
  const uint8_t v = pref.getUChar("route", static_cast<uint8_t>(default_route()));
  pref.end();
  s_route = (v == static_cast<uint8_t>(PmAudioRoute::Usb)) ? PmAudioRoute::Usb : PmAudioRoute::Onboard;
  pm_log_printf(false, "audio-route: loaded %s", s_route == PmAudioRoute::Usb ? "USB" : "onboard");
}

bool pm_audio_route_uac_available(void) {
  return uac_compiled();
}

PmAudioRoute pm_audio_route_get(void) {
  return s_route;
}

static void persist_route(PmAudioRoute route) {
  if (!uac_compiled()) {
    return;
  }
  Preferences pref;
  if (!pref.begin("audio_rt", false)) {
    return;
  }
  pref.putUChar("route", static_cast<uint8_t>(route));
  pref.end();
}

void pm_audio_route_set(PmAudioRoute route) {
  if (!uac_compiled()) {
    s_route = PmAudioRoute::Onboard;
    return;
  }
  if (route == s_route) {
    return;
  }
  s_route = route;
  persist_route(s_route);
  pm_log_printf(false, "audio-route: set %s", s_route == PmAudioRoute::Usb ? "USB" : "onboard");
  if (s_route == PmAudioRoute::Onboard) {
    pm_usb_uac_release_speaker();
    pm_speaker_pcm_end();
  }
}

bool pm_audio_route_output_usb(void) {
  return uac_compiled() && s_route == PmAudioRoute::Usb;
}

bool pm_audio_route_input_usb(void) {
  return pm_audio_route_output_usb();
}

static bool face_uses_vertical_swipes(ClockFace face) {
  return face == ClockFace::Spotify || face == ClockFace::Synastry || face == ClockFace::Moon ||
         face == ClockFace::Settings || face == ClockFace::Spectrum || face == ClockFace::Chakra ||
         face == ClockFace::TibetanBowl || face == ClockFace::Rocket || face == ClockFace::Faculty ||
         face == ClockFace::Tarot || face == ClockFace::Ocarina || face == ClockFace::Bongo ||
         face == ClockFace::Piano || face == ClockFace::Alethiometer;
}

bool pm_audio_route_handle_gesture(PmGestureKind kind, ClockFace face, char *banner, size_t banner_len) {
  if (!uac_compiled() || !banner || banner_len == 0) {
    return false;
  }
  if (face_uses_vertical_swipes(face)) {
    return false;
  }
  if (kind == PmGestureKind::SwipeUp) {
    pm_audio_route_set(PmAudioRoute::Usb);
    snprintf(banner, banner_len, "sound: USB");
    return true;
  }
  if (kind == PmGestureKind::SwipeDown) {
    pm_audio_route_set(PmAudioRoute::Onboard);
    snprintf(banner, banner_len, "sound: onboard");
    return true;
  }
  return false;
}

void pm_audio_route_label(char *buf, size_t len) {
  if (!buf || len == 0) {
    return;
  }
  if (!uac_compiled()) {
    snprintf(buf, len, "onboard");
    return;
  }
  snprintf(buf, len, "%s", s_route == PmAudioRoute::Usb ? "USB" : "onboard");
}
