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

  // Prevent copying
  DisplayManager(const DisplayManager&) = delete;
  DisplayManager& operator=(const DisplayManager&) = delete;

private:
  // Private constructor for singleton
  DisplayManager();

  // Display hardware instance (created in begin())
  GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>* display = nullptr;

  // Pin definitions
  static constexpr uint8_t CS_PIN = 15;
  static constexpr uint8_t DC_PIN = 4;
  static constexpr uint8_t RES_PIN = 5;
  static constexpr uint8_t BUSY_PIN = 12;

  // Helper struct for bin information
  struct BinInfo {
    String type;
    String binType;
    bool isNext;
  };
};

#endif // DISPLAYMANAGER_H
