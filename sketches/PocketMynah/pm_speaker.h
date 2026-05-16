#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

bool pm_speaker_play_mp3(const uint8_t *mp3, size_t mp3_len);
