#include "pm_variant.h"

#include <Preferences.h>

static constexpr const char *kNvsNs = "mynah";
static constexpr const char *kNvsKey = "variant";

static PmDeviceVariant s_variant = PmDeviceVariant::Pocket;

static bool valid_variant(uint8_t value) {
  return value < static_cast<uint8_t>(PmDeviceVariant::kCount);
}

static bool face_is_astrolabe(ClockFace face) {
  switch (face) {
    case ClockFace::ClassicAnalog:
    case ClockFace::DigitalLocal:
    case ClockFace::Apocalypso:
    case ClockFace::CalciferCountdown:
    case ClockFace::FocusTimer:
    case ClockFace::Weather:
    case ClockFace::Globe:
    case ClockFace::Radar:
    case ClockFace::Biometrics:
    case ClockFace::Level:
    case ClockFace::Rocket:
      return true;
    default:
      return false;
  }
}

static bool face_is_lunasay(ClockFace face) {
  switch (face) {
    case ClockFace::Moon:
    case ClockFace::Astrology:
    case ClockFace::LiveTransits:
    case ClockFace::Sky:
    case ClockFace::Synastry:
    case ClockFace::Tarot:
    case ClockFace::Lenormand:
    case ClockFace::Runes:
    case ClockFace::Alethiometer:
      return true;
    default:
      return false;
  }
}

static bool face_is_ocarina(ClockFace face) {
  switch (face) {
    case ClockFace::Ocarina:
    case ClockFace::Tuning:
    case ClockFace::Spectrum:
    case ClockFace::Chakra:
    case ClockFace::TibetanBowl:
    case ClockFace::Bongo:
    case ClockFace::Piano:
    case ClockFace::PanDrum:
      return true;
    default:
      return false;
  }
}

static bool face_is_cameo(ClockFace face) {
  switch (face) {
    case ClockFace::Faculty:
    case ClockFace::Quotes:
    case ClockFace::Notes:
    case ClockFace::QuestionOfDay:
      return true;
    default:
      return false;
  }
}

static bool face_is_luopan(ClockFace face) {
  switch (face) {
    case ClockFace::Orientation:
    case ClockFace::Luopan:
      return true;
    default:
      return false;
  }
}

void pm_variant_begin(void) {
  Preferences pref;
  if (!pref.begin(kNvsNs, false)) {
    s_variant = PmDeviceVariant::Pocket;
    return;
  }
  const uint8_t value = pref.getUChar(kNvsKey, static_cast<uint8_t>(PmDeviceVariant::Pocket));
  if (valid_variant(value)) {
    s_variant = static_cast<PmDeviceVariant>(value);
  } else {
    s_variant = PmDeviceVariant::Pocket;
    pref.putUChar(kNvsKey, static_cast<uint8_t>(s_variant));
  }
  pref.end();
}

PmDeviceVariant pm_variant_get(void) { return s_variant; }

void pm_variant_set(PmDeviceVariant variant) {
  if (!valid_variant(static_cast<uint8_t>(variant))) {
    return;
  }
  s_variant = variant;
  Preferences pref;
  if (pref.begin(kNvsNs, false)) {
    pref.putUChar(kNvsKey, static_cast<uint8_t>(variant));
    pref.end();
  }
}

PmDeviceVariant pm_variant_cycle(int delta) {
  const int n = static_cast<int>(PmDeviceVariant::kCount);
  int v = static_cast<int>(s_variant) + delta;
  v = (v % n + n) % n;
  pm_variant_set(static_cast<PmDeviceVariant>(v));
  return s_variant;
}

const char *pm_variant_label(PmDeviceVariant variant) {
  switch (variant) {
    case PmDeviceVariant::Pocket:
      return "Pocket";
    case PmDeviceVariant::Astrolabe:
      return "Astrolabe";
    case PmDeviceVariant::Lunasay:
      return "Lunasay";
    case PmDeviceVariant::Ocarina:
      return "Ocarina";
    case PmDeviceVariant::Cameo:
      return "Cameo";
    case PmDeviceVariant::Luopan:
      return "Luopan";
    default:
      return "Unknown";
  }
}

const char *pm_variant_summary(PmDeviceVariant variant) {
  switch (variant) {
    case PmDeviceVariant::Pocket:
      return "development: all faces";
    case PmDeviceVariant::Astrolabe:
      return "time / presence / launch";
    case PmDeviceVariant::Lunasay:
      return "moon / astrology / divination";
    case PmDeviceVariant::Ocarina:
      return "breath / music / sound";
    case PmDeviceVariant::Cameo:
      return "memory / companion";
    case PmDeviceVariant::Luopan:
      return "orientation / feng shui";
    default:
      return "";
  }
}

ClockFace pm_variant_home_face(void) {
  switch (s_variant) {
    case PmDeviceVariant::Pocket:
    case PmDeviceVariant::Astrolabe:
      return ClockFace::ClassicAnalog;
    case PmDeviceVariant::Lunasay:
      return ClockFace::Moon;
    case PmDeviceVariant::Ocarina:
      return ClockFace::Ocarina;
    case PmDeviceVariant::Cameo:
      return ClockFace::Faculty;
    case PmDeviceVariant::Luopan:
      return ClockFace::Orientation;
    default:
      return ClockFace::ClassicAnalog;
  }
}

bool pm_variant_face_allowed(ClockFace face) {
  if (face == ClockFace::Settings || face == ClockFace::Castalia) {
    return false;
  }
  switch (s_variant) {
    case PmDeviceVariant::Pocket:
      return true;
    case PmDeviceVariant::Astrolabe:
      return face_is_astrolabe(face);
    case PmDeviceVariant::Lunasay:
      return face_is_lunasay(face);
    case PmDeviceVariant::Ocarina:
      return face_is_ocarina(face);
    case PmDeviceVariant::Cameo:
      return face_is_cameo(face);
    case PmDeviceVariant::Luopan:
      return face_is_luopan(face);
    default:
      return true;
  }
}
