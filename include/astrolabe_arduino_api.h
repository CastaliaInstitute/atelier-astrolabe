#pragma once

// Arduino-ESP32 3.x (IDF component) vs 2.x (PlatformIO espressif32@6.x) API differences.

#include <Arduino.h>
#include <esp_arduino_version.h>

#if defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED) || __has_include(<BLEDevice.h>)
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <BLECharacteristic.h>
#endif

#if ESP_ARDUINO_VERSION_MAJOR >= 3

using PmBleBlob = String;

inline PmBleBlob pm_ble_get_manufacturer_data(BLEAdvertisedDevice &dev) {
  return dev.getManufacturerData();
}

inline PmBleBlob pm_ble_characteristic_value(BLECharacteristic *ch) {
  return ch ? ch->getValue() : String();
}

inline void pm_ble_set_manufacturer_data(BLEAdvertisementData &adv, const uint8_t *bytes, size_t len) {
  adv.setManufacturerData(String(reinterpret_cast<const char *>(bytes), len));
}

#else  // Arduino-ESP32 2.x

#include <string>

using PmBleBlob = std::string;

inline PmBleBlob pm_ble_get_manufacturer_data(BLEAdvertisedDevice &dev) {
  return dev.getManufacturerData();
}

inline PmBleBlob pm_ble_characteristic_value(BLECharacteristic *ch) {
  return ch ? ch->getValue() : std::string();
}

inline void pm_ble_set_manufacturer_data(BLEAdvertisementData &adv, const uint8_t *bytes, size_t len) {
  adv.setManufacturerData(std::string(reinterpret_cast<const char *>(bytes), len));
}

#endif

inline size_t pm_ble_blob_len(const PmBleBlob &blob) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return blob.length();
#else
  return blob.size();
#endif
}

inline const char *pm_ble_blob_data(const PmBleBlob &blob) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  return blob.c_str();
#else
  return blob.data();
#endif
}

inline bool pm_ble_blob_empty(const PmBleBlob &blob) {
  return pm_ble_blob_len(blob) == 0;
}
