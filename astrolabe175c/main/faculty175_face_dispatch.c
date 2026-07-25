#include "faculty175_face_dispatch.h"

#include "faculty175_face_alethiometer.h"
#include "faculty175_face_crystal_ball.h"
#include "faculty175_face_incidents.h"
#include "faculty175_face_wifilab.h"
#include "faculty175_ble.h"
#include "faculty175_lvgl.h"
#include "faculty175_face_native.h"
#include "faculty175_face_notes.h"
#include "faculty175_face_psych_state.h"
#include "faculty175_face_runes.h"
#include "faculty175_usb_screen.h"

void faculty175_face_classic_draw(uint32_t anim_ms);
void faculty175_face_apocalypso_draw(uint32_t anim_ms);
void faculty175_face_digital_draw(uint32_t anim_ms);
void faculty175_face_spotify_draw(uint32_t anim_ms);
bool faculty175_face_spotify_action(void);
void faculty175_face_moon_draw(uint32_t anim_ms);
void faculty175_face_calcifer_draw(uint32_t anim_ms);
void faculty175_face_castalia_draw(uint32_t anim_ms);
void faculty175_face_astrology_draw(uint32_t anim_ms);
void faculty175_face_synastry_draw(uint32_t anim_ms);
void faculty175_face_partner_wellness_draw(uint32_t anim_ms);
void faculty175_face_tarot_draw(uint32_t anim_ms);
void faculty175_face_tarot_draw_card(uint32_t seed_ms);
void faculty175_face_inq_draw(uint32_t anim_ms);
void faculty175_face_rocket_draw(uint32_t anim_ms);
void faculty175_face_radar_draw(uint32_t anim_ms);
void faculty175_face_weather_draw(uint32_t anim_ms);
void faculty175_face_globe_draw(uint32_t anim_ms);
void faculty175_face_sky_draw(uint32_t anim_ms);
void faculty175_face_quotes_draw(uint32_t anim_ms);
void faculty175_face_transits_draw(uint32_t anim_ms);
void faculty175_face_solar_draw(uint32_t anim_ms);
void faculty175_face_qday_draw(uint32_t anim_ms);
void faculty175_face_focus_draw(uint32_t anim_ms);
void faculty175_face_bio_draw(uint32_t anim_ms);
void faculty175_face_ironman_draw(uint32_t anim_ms);
bool faculty175_face_ironman_action(uint32_t now_ms);
void faculty175_face_watcher_draw(uint32_t anim_ms);
void faculty175_face_lenormand_draw(uint32_t anim_ms);
void faculty175_face_hid_draw(uint32_t anim_ms);
void faculty175_face_babel_draw(uint32_t anim_ms);
void faculty175_face_human_design_draw(uint32_t anim_ms);
bool faculty175_face_human_design_action(uint32_t seed_ms);
void faculty175_face_maze_draw(uint32_t anim_ms);
void faculty175_face_deathstar_draw(uint32_t anim_ms);
void faculty175_face_tron_draw(uint32_t anim_ms);
void faculty175_face_tron_reset(void);
bool faculty175_solar_image_action(uint32_t seed_ms);
void faculty175_face_settings_draw(uint32_t anim_ms);
void faculty175_face_pocketwatch_draw(uint32_t anim_ms);
void faculty175_face_battery_draw(uint32_t anim_ms);
void faculty175_face_journal_draw(uint32_t anim_ms);
void faculty175_face_conversation_draw(uint32_t anim_ms);
void faculty175_face_cycle_draw(uint32_t anim_ms);
void faculty175_face_jyotish_draw(uint32_t anim_ms);
void faculty175_face_bazi_draw(uint32_t anim_ms);

bool faculty175_face_dispatch_draw(faculty175_face_id_t id, uint32_t anim_ms)
{
    if (faculty175_lvgl_draw_face(id, anim_ms)) {
        return true;
    }

    switch (id) {
        case FACULTY175_FACE_NOTES: faculty175_face_notes_draw(anim_ms); return true;
        case FACULTY175_FACE_RUNES: faculty175_face_runes_draw(anim_ms); return true;
        case FACULTY175_FACE_ALETHIOMETER: faculty175_face_alethiometer_draw(anim_ms); return true;
        case FACULTY175_FACE_CRYSTAL_BALL: faculty175_face_crystal_ball_draw(anim_ms); return true;
        case FACULTY175_FACE_CLASSIC: faculty175_face_classic_draw(anim_ms); return true;
        case FACULTY175_FACE_APOCALYPSO: faculty175_face_apocalypso_draw(anim_ms); return true;
        case FACULTY175_FACE_DIGITAL: faculty175_face_digital_draw(anim_ms); return true;
        case FACULTY175_FACE_SPOTIFY: faculty175_face_spotify_draw(anim_ms); return true;
        case FACULTY175_FACE_MOON: faculty175_face_moon_draw(anim_ms); return true;
        case FACULTY175_FACE_CALCIFER: faculty175_face_calcifer_draw(anim_ms); return true;
        case FACULTY175_FACE_CASTALIA: faculty175_face_castalia_draw(anim_ms); return true;
        case FACULTY175_FACE_ASTROLOGY: faculty175_face_astrology_draw(anim_ms); return true;
        case FACULTY175_FACE_SYNASTRY: faculty175_face_synastry_draw(anim_ms); return true;
        case FACULTY175_FACE_PARTNER_WELLNESS: faculty175_face_partner_wellness_draw(anim_ms); return true;
        case FACULTY175_FACE_TAROT: faculty175_face_tarot_draw(anim_ms); return true;
        case FACULTY175_FACE_INQ: faculty175_face_inq_draw(anim_ms); return true;
        case FACULTY175_FACE_ROCKET: faculty175_face_rocket_draw(anim_ms); return true;
        case FACULTY175_FACE_RADAR: faculty175_face_radar_draw(anim_ms); return true;
        case FACULTY175_FACE_WEATHER: faculty175_face_weather_draw(anim_ms); return true;
        case FACULTY175_FACE_GLOBE: faculty175_face_globe_draw(anim_ms); return true;
        case FACULTY175_FACE_SKY: faculty175_face_sky_draw(anim_ms); return true;
        case FACULTY175_FACE_QUOTES: faculty175_face_quotes_draw(anim_ms); return true;
        case FACULTY175_FACE_TRANSITS: faculty175_face_transits_draw(anim_ms); return true;
        case FACULTY175_FACE_SOLAR: faculty175_face_solar_draw(anim_ms); return true;
        case FACULTY175_FACE_QDAY: faculty175_face_qday_draw(anim_ms); return true;
        case FACULTY175_FACE_FOCUS: faculty175_face_focus_draw(anim_ms); return true;
        case FACULTY175_FACE_BIOMETRICS: faculty175_face_bio_draw(anim_ms); return true;
        case FACULTY175_FACE_IRONMAN: faculty175_face_ironman_draw(anim_ms); return true;
        case FACULTY175_FACE_WATCHER: faculty175_face_watcher_draw(anim_ms); return true;
        case FACULTY175_FACE_LENORMAND: faculty175_face_lenormand_draw(anim_ms); return true;
        case FACULTY175_FACE_HID: faculty175_face_hid_draw(anim_ms); return true;
        case FACULTY175_FACE_USB_SCREEN: faculty175_usb_screen_draw_face(anim_ms); return true;
        case FACULTY175_FACE_BABEL: faculty175_face_babel_draw(anim_ms); return true;
        case FACULTY175_FACE_HUMAN_DESIGN: faculty175_face_human_design_draw(anim_ms); return true;
        case FACULTY175_FACE_MAZE: faculty175_face_maze_draw(anim_ms); return true;
        case FACULTY175_FACE_DEATHSTAR: faculty175_face_deathstar_draw(anim_ms); return true;
        case FACULTY175_FACE_TRON: faculty175_face_tron_draw(anim_ms); return true;
        case FACULTY175_FACE_WSCAN:
        case FACULTY175_FACE_DEAUTH:
        case FACULTY175_FACE_EVILTWIN:
        case FACULTY175_FACE_HANDSHAKE:
            faculty175_face_wifilab_draw(id, anim_ms);
            return true;
        case FACULTY175_FACE_INCIDENTS:
            faculty175_face_incidents_draw(anim_ms);
            return true;
#if !defined(ASTROLABE_FORCE_VARIANT_LUNASAY)
        case FACULTY175_FACE_PSYCH_STATE:
            faculty175_face_psych_state_draw(anim_ms);
            return true;
#endif
        case FACULTY175_FACE_SETTINGS: faculty175_face_settings_draw(anim_ms); return true;
        case FACULTY175_FACE_POCKETWATCH: faculty175_face_pocketwatch_draw(anim_ms); return true;
        case FACULTY175_FACE_JYOTISH: faculty175_face_jyotish_draw(anim_ms); return true;
        case FACULTY175_FACE_BAZI: faculty175_face_bazi_draw(anim_ms); return true;
        case FACULTY175_FACE_BATTERY: faculty175_face_battery_draw(anim_ms); return true;
        case FACULTY175_FACE_JOURNAL: faculty175_face_journal_draw(anim_ms); return true;
        case FACULTY175_FACE_CONVERSATION: faculty175_face_conversation_draw(anim_ms); return true;
        case FACULTY175_FACE_CYCLE: faculty175_face_cycle_draw(anim_ms); return true;
        case FACULTY175_FACE_FACULTY:
        case FACULTY175_FACE_COUNT:
        default:
            return false;
    }
}

bool faculty175_face_dispatch_action(faculty175_face_id_t id, uint32_t seed_ms)
{
    switch (id) {
        case FACULTY175_FACE_RUNES:
            faculty175_face_runes_cast();
            return true;
        case FACULTY175_FACE_ALETHIOMETER:
            faculty175_face_alethiometer_cast(seed_ms);
            return true;
        case FACULTY175_FACE_CRYSTAL_BALL:
            faculty175_face_crystal_ball_cast(seed_ms);
            return true;
        case FACULTY175_FACE_SPOTIFY:
            return faculty175_face_spotify_action();
        case FACULTY175_FACE_TAROT:
            faculty175_face_tarot_draw_card(seed_ms);
            return true;
        case FACULTY175_FACE_SYNASTRY:
            return faculty175_faces_set_runtime(FACULTY175_FACE_PARTNER_WELLNESS) == ESP_OK;
        case FACULTY175_FACE_PARTNER_WELLNESS:
            return faculty175_faces_set_runtime(FACULTY175_FACE_SYNASTRY) == ESP_OK;
        case FACULTY175_FACE_HUMAN_DESIGN:
            return faculty175_face_human_design_action(seed_ms);
        case FACULTY175_FACE_PSYCH_STATE:
            return faculty175_face_psych_state_action(seed_ms);
        case FACULTY175_FACE_TRON:
            faculty175_face_tron_reset();
            return true;
        case FACULTY175_FACE_SOLAR:
            return faculty175_solar_image_action(seed_ms);
        case FACULTY175_FACE_IRONMAN:
            return faculty175_face_ironman_action(seed_ms);
        case FACULTY175_FACE_BIOMETRICS: {
            uint16_t ring_id = 0;
            if (faculty175_ble_nearby_unpaired_ring(&ring_id, NULL)) {
                return faculty175_ble_ring_pair(ring_id) == ESP_OK;
            }
            /* With no candidate in range, the Ring face's TAP action cycles
             * the persisted near threshold. This keeps calibration possible
             * before a ring is brought close to the device. */
            static const int8_t k_thresholds[] = {-55, -60, -65, -70, -75, -80};
            const int8_t current = faculty175_ble_near_rssi_threshold();
            int next = 0;
            for (size_t i = 0; i < sizeof(k_thresholds) / sizeof(k_thresholds[0]); ++i) {
                if (k_thresholds[i] == current) {
                    next = (int)((i + 1u) % (sizeof(k_thresholds) / sizeof(k_thresholds[0])));
                    break;
                }
            }
            return faculty175_ble_set_near_rssi_threshold(k_thresholds[next]) == ESP_OK;
        }
        case FACULTY175_FACE_WSCAN:
        case FACULTY175_FACE_DEAUTH:
        case FACULTY175_FACE_EVILTWIN:
        case FACULTY175_FACE_HANDSHAKE:
            return faculty175_face_wifilab_action(id, seed_ms);
        case FACULTY175_FACE_INCIDENTS:
            return faculty175_face_incidents_action(seed_ms);
        case FACULTY175_FACE_FACULTY:
        case FACULTY175_FACE_NOTES:
        case FACULTY175_FACE_BATTERY:
        case FACULTY175_FACE_JOURNAL:
        case FACULTY175_FACE_CONVERSATION:
        case FACULTY175_FACE_COUNT:
            return false;
        default:
            return faculty175_face_native_action(id, seed_ms);
    }
}
