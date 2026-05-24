#include "p4_settings.h"

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "astrolabe_settings";
static const char *NVS_NS = "p4_settings";
static const char *NVS_HOME_FACE = "home_face";
static const char *PROFILE = "LunaSay";
static const astrolabe_ui_face_t DEFAULT_HOME_FACE = ASTROLABE_UI_FACE_MOON;

const char *astrolabe_p4_settings_profile(void) { return PROFILE; }

astrolabe_ui_face_t astrolabe_p4_settings_home_face(void) {
  int32_t face = DEFAULT_HOME_FACE;
  nvs_handle_t nvs;
  if (nvs_open(NVS_NS, NVS_READONLY, &nvs) == ESP_OK) {
    (void)nvs_get_i32(nvs, NVS_HOME_FACE, &face);
    nvs_close(nvs);
  }
  if (face < 0 || face >= ASTROLABE_UI_FACE_COUNT) {
    return DEFAULT_HOME_FACE;
  }
  return (astrolabe_ui_face_t)face;
}

bool astrolabe_p4_settings_set_home_face(astrolabe_ui_face_t face) {
  if (face < 0 || face >= ASTROLABE_UI_FACE_COUNT) {
    ESP_LOGW(TAG, "invalid home face=%d", (int)face);
    return false;
  }

  nvs_handle_t nvs;
  if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) != ESP_OK) {
    ESP_LOGE(TAG, "failed to open settings NVS");
    return false;
  }
  esp_err_t ret = nvs_set_i32(nvs, NVS_HOME_FACE, (int32_t)face);
  if (ret == ESP_OK) {
    ret = nvs_commit(nvs);
  }
  nvs_close(nvs);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "failed to save home face: %s", esp_err_to_name(ret));
    return false;
  }

  ESP_LOGI(TAG, "home face saved: %d %s", (int)face, astrolabe_ui_face_name(face));
  return true;
}

void astrolabe_p4_settings_log_status(void) {
  astrolabe_ui_face_t home = astrolabe_p4_settings_home_face();
  ESP_LOGI(TAG, "settings: profile=%s home_face=%d home_name=%s", PROFILE, (int)home,
           astrolabe_ui_face_name(home));
}

