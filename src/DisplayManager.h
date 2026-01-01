#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include <Arduino.h>
#include <GxEPD2_3C.h>

/**
 * DisplayManager - Singleton class for managing e-paper display
 *
 * Encapsulates all display operations including bin schedule rendering,
 * error messages, and firmware update status.
 *
 * Usage:
 *   DisplayManager::getInstance().begin();
 *   DisplayManager::getInstance().showBinSchedule(jsonData, lastUpdate, version, isProd);
 *   DisplayManager::getInstance().hibernate();
 */
class DisplayManager {
public:
  // Get singleton instance
  static DisplayManager& getInstance();

  // Initialize display hardware
  void begin();

  // Display functions
  void showBinSchedule(const String& jsonData, const String& lastUpdate, const String& version, bool isProduction);
  void showError(const char* errorMessage);
  void showUpdateStatus(const char* message);

  // Power management
  void hibernate();

  // Get display dimensions (for layout calculations)
  int16_t getWidth() const;
  int16_t getHeight() const;

  // Debug: Dump framebuffer to serial as hex (for remote layout debugging)
  void dumpFramebufferToSerial();

  // Prevent copying
  DisplayManager(const DisplayManager&) = delete;
  DisplayManager& operator=(const DisplayManager&) = delete;

private:
  // Private constructor for singleton
  DisplayManager();

  // Helper methods
  String truncateText(const String& text, int maxWidth, const GFXfont* font = nullptr);

  // Display hardware instance (created in begin())
  GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>* display = nullptr;

  // Pin definitions (ESP32-S3)
  // Using safe GPIOs that don't conflict with strapping pins or USB
  static constexpr uint8_t CS_PIN = 10;    // GPIO10 - SPI CS
  static constexpr uint8_t DC_PIN = 11;    // GPIO11 - Data/Command
  static constexpr uint8_t RES_PIN = 12;   // GPIO12 - Reset
  static constexpr uint8_t BUSY_PIN = 13;  // GPIO13 - Busy (input)
  // SPI pins: SCK=GPIO7, MOSI=GPIO6 (hardware SPI)

  // Helper struct for bin information
  struct BinInfo {
    String type;
    String binType;
    bool isNext;
  };
};

#endif // DISPLAYMANAGER_H
