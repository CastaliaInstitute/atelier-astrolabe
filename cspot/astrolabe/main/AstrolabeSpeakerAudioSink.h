#pragma once

#include <cstddef>
#include <cstdint>

#include "AudioSink.h"
#include "driver/i2s.h"
#include "esp_err.h"

class AstrolabeSpeakerAudioSink : public AudioSink {
 public:
  AstrolabeSpeakerAudioSink();
  ~AstrolabeSpeakerAudioSink() override;

  void feedPCMFrames(const uint8_t *buffer, size_t bytes) override;
  void volumeChanged(uint16_t volume) override;
  bool setParams(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth) override;

 private:
  esp_err_t beginAudio(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth);
  esp_err_t beginCodec(uint32_t sampleRate);
  esp_err_t beginI2s(uint32_t sampleRate, uint8_t channelCount, uint8_t bitDepth);
  void stopI2s();

  uint32_t sampleRate_ = 0;
  uint8_t channelCount_ = 0;
  uint8_t bitDepth_ = 0;
  bool i2sInstalled_ = false;
  bool codecReady_ = false;
  int volume_ = 70;
};

enum class AstrolabeAudioRole : uint8_t {
  Stereo = 0,
  Left = 1,
  Right = 2,
};

void astrolabe_audio_set_role(AstrolabeAudioRole role);
void astrolabe_instrument_note_on(int note, uint8_t velocity);
