#pragma once

/** Round AMOLED viewport for Waveshare ESP32-S3-Touch-AMOLED-1.75C class boards. */
namespace mynah::board {

constexpr int kWidth = 466;
constexpr int kHeight = 466;
constexpr int kCenterX = kWidth / 2;
constexpr int kCenterY = kHeight / 2;

/** Usable radius inside the physical bezel (conservative safe area). */
constexpr int kSafeRadius = 218;

/** RGB565 bytes per full frame. */
constexpr unsigned kFrameBytes = static_cast<unsigned>(kWidth) * kHeight * 2u;

}  // namespace mynah::board
