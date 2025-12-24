#ifndef FIRMWAREMANAGER_H
#define FIRMWAREMANAGER_H

#include <Arduino.h>
#include <functional>

/**
 * FirmwareManager - Singleton class for managing firmware updates
 *
 * Handles both HTTP OTA updates (production) and Arduino OTA (development).
 *
 * Usage:
 *   FirmwareManager::getInstance().setDisplayCallback(...);
 *   if (FirmwareManager::getInstance().checkForUpdate()) {
 *     FirmwareManager::getInstance().performHTTPUpdate();
 *   }
 *   FirmwareManager::getInstance().setupArduinoOTA();  // Dev mode only
 *   FirmwareManager::getInstance().handleArduinoOTA();  // In loop()
 */
class FirmwareManager {
public:
  // Callback type for display updates
  using DisplayCallback = std::function<void(const char*)>;

  // Get singleton instance
  static FirmwareManager& getInstance();

  // Set callback for display status messages
  void setDisplayCallback(DisplayCallback callback);

  // HTTP OTA (production/automatic updates)
  bool checkForUpdate();
  void performHTTPUpdate();

  // Arduino OTA (development/manual updates)
  void setupArduinoOTA();
  void handleArduinoOTA();

  // Prevent copying
  FirmwareManager(const FirmwareManager&) = delete;
  FirmwareManager& operator=(const FirmwareManager&) = delete;

private:
  // Private constructor for singleton
  FirmwareManager() = default;

  // Display callback for status messages
  DisplayCallback displayCallback = nullptr;

  // Firmware update URLs
  static constexpr const char* FIRMWARE_VERSION_URL = "http://bindicator.berwick.me.uk/firmware/version.txt";
  static constexpr const char* FIRMWARE_DOWNLOAD_URL = "http://bindicator.berwick.me.uk/firmware/bindicator.bin";
};

#endif // FIRMWAREMANAGER_H
