#pragma once

#include <stdbool.h>

/** Start USB Audio Class gadget (speaker M1; mic optional later). Returns false if init fails. */
bool pm_usb_uac_begin(void);

/** True after successful UAC init (host may enumerate audio device). */
bool pm_usb_uac_ready(void);

/** Drop host speaker PCM when switching to onboard route. */
void pm_usb_uac_release_speaker(void);

/** True when host speaker data should drive ES8311 (route + callback active). */
bool pm_usb_uac_speaker_active(void);

/** True once when the host opens or starts sending to the speaker stream. */
bool pm_usb_uac_consume_speaker_stream_event(void);
