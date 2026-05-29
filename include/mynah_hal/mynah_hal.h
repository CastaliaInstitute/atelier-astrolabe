#pragma once

/**
 * Mynah hardware abstraction — one include for the active backend.
 *
 * Build flags (mutually exclusive target selection):
 *   MYNAH_TARGET_WAVESHARE   — real Waveshare 1.75C (default on device)
 *   MYNAH_SIM_HOST           — SDL / host viewer (see sim/host_round/)
 *   MYNAH_SIM_QEMU           — ESP-IDF QEMU mocks (see sim/qemu/)
 *
 * Face and shell code should depend only on mynah::Display, mynah::Touch, etc.
 */

#include "board.h"
#include "display.h"
#include "touch.h"
#include "imu.h"
#include "audio.h"
#include "storage.h"
#include "network.h"

#if defined(MYNAH_SIM_QEMU)
#include "mynah_hal_qemu.h"
#elif defined(MYNAH_SIM_HOST)
#include "mynah_hal_host.h"
#else
#include "mynah_hal_waveshare.h"
#endif

namespace mynah {

/** Active drivers for this build (defined in backend-specific header). */
extern Display* hal_display();
extern Touch* hal_touch();
extern Imu* hal_imu();
extern AudioIn* hal_audio_in();
extern AudioOut* hal_audio_out();
extern Storage* hal_storage();
extern Network* hal_network();

}  // namespace mynah
