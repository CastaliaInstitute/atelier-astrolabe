#pragma once

#include <stdbool.h>

/** Start USB Audio Class gadget (speaker M1; mic optional later). Returns false if init fails. */
bool pm_usb_uac_begin(void);

/** True after successful UAC init (host may enumerate audio device). */
bool pm_usb_uac_ready(void);
