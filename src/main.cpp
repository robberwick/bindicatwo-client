// base class GxEPD2_GFX can be used to pass references or pointers to the display instance as parameter, uses ~1.2k more code
// enable or disable GxEPD2_GFX base class
#define ENABLE_GxEPD2_GFX 0

#include <ESP8266WiFi.h>

#include "ConfigManager.h"
#include "DisplayManager.h"
#include "NetworkManager.h"
#include "FirmwareManager.h"

// Firmware version
#define FIRMWARE_VERSION "0.0.1"

// Sleep intervals (in microseconds)
const uint64_t DEVELOPMENT_SLEEP_INTERVAL = 20ULL * 1000000;    // 20 seconds for development
const uint64_t PRODUCTION_SLEEP_INTERVAL = 3ULL * 60 * 60 * 1000000; // 3 hours for production

// Forward declarations
void performPeriodicUpdate();
void enterDeepSleep();

void performPeriodicUpdate() {
  Serial.println("Performing periodic update...");

  // Reconnect WiFi
  NetworkManager::getInstance().reconnectWiFi();

  // Re-sync NTP after wake for accurate time
  if (!NetworkManager::getInstance().syncNTP()) {
    Serial.println("WARNING: NTP sync failed, time may be inaccurate");
    // Continue operation - don't block on NTP failure
  }

  // Check for firmware updates (less frequently in production)
  static int updateCheckCounter = 0;
  updateCheckCounter++;

  // Check for updates every 10th wake up in production, every wake up in development
  bool shouldCheckUpdates = ConfigManager::getInstance().getOperatingMode() == PRODUCTION ? (updateCheckCounter % 10 == 0) : true;

  if (shouldCheckUpdates && FirmwareManager::getInstance().checkForUpdate()) {
    FirmwareManager::getInstance().performHTTPUpdate();
  }

  // Fetch and display updated bin schedule
  String jsonData = NetworkManager::getInstance().fetchDataFromWebService();
  if (jsonData.length() > 0) {
    DisplayManager::getInstance().showBinSchedule(
      jsonData,
      ConfigManager::getInstance().getLastUpdateString(),
      ConfigManager::getInstance().getFirmwareVersion(),
      ConfigManager::getInstance().getOperatingMode() == PRODUCTION
    );
  } else {
    DisplayManager::getInstance().showError("No data");
  }

  // Put display to sleep
  DisplayManager::getInstance().hibernate();

  Serial.println("Update complete, going back to sleep...");
}

void enterDeepSleep() {
  // Get the appropriate sleep interval based on mode
  OperatingMode mode = ConfigManager::getInstance().getOperatingMode();
  uint64_t sleepInterval = mode == PRODUCTION ? PRODUCTION_SLEEP_INTERVAL : DEVELOPMENT_SLEEP_INTERVAL;

  Serial.printf("Entering deep sleep for %llu seconds (%s mode)...\n",
                sleepInterval / 1000000,
                mode == PRODUCTION ? "Production" : "Development");

  // Ensure all serial output is sent
  Serial.flush();

  // Enter deep sleep
  ESP.deepSleep(sleepInterval);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Bin Schedule Display...");
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);

  // CRITICAL: Initialize ConfigManager first
  if (!ConfigManager::getInstance().begin()) {
    Serial.println("FATAL: Config initialization failed");
    ESP.restart();
  }

  Serial.printf("Running in %s mode\n", ConfigManager::getInstance().getOperatingMode() == PRODUCTION ? "Production" : "Development");

  // Initialize DisplayManager
  DisplayManager::getInstance().begin();

  // Set up FirmwareManager display callback
  FirmwareManager::getInstance().setDisplayCallback([](const char* msg) {
    DisplayManager::getInstance().showUpdateStatus(msg);
  });

  // Check if this is a wake up from deep sleep
  rst_info *resetInfo = ESP.getResetInfoPtr();
  if (resetInfo->reason == REASON_DEEP_SLEEP_AWAKE) {
    Serial.println("Woke up from deep sleep - performing periodic update");
    performPeriodicUpdate();
    enterDeepSleep();
  } else {
    Serial.println("Fresh boot - performing initial setup");

    // Setup WiFi connection
    NetworkManager::getInstance().setupWiFi();

    // Sync NTP time on boot
    if (!NetworkManager::getInstance().syncNTP()) {
      Serial.println("WARNING: NTP sync failed, time may be inaccurate");
      // Continue operation - don't block on NTP failure
    }

    // Setup Arduino OTA for development updates (only in development mode)
    if (ConfigManager::getInstance().getOperatingMode() == DEVELOPMENT) {
      FirmwareManager::getInstance().setupArduinoOTA();
    }

    // Check for HTTP OTA updates
    if (FirmwareManager::getInstance().checkForUpdate()) {
      FirmwareManager::getInstance().performHTTPUpdate();
    }

    // Fetch and display the bin schedule
    String jsonData = NetworkManager::getInstance().fetchDataFromWebService();
    if (jsonData.length() > 0) {
      DisplayManager::getInstance().showBinSchedule(
        jsonData,
        ConfigManager::getInstance().getLastUpdateString(),
        ConfigManager::getInstance().getFirmwareVersion(),
        ConfigManager::getInstance().getOperatingMode() == PRODUCTION
      );
    } else {
      DisplayManager::getInstance().showError("No data");
    }

    DisplayManager::getInstance().hibernate();

    // Enter deep sleep cycle
    enterDeepSleep();
  }
}

void loop() {
  // This should never be reached when using deep sleep
  // Only used in development mode when deep sleep might be disabled for debugging
  if (ConfigManager::getInstance().getOperatingMode() == DEVELOPMENT) {
    // Handle Arduino OTA in development mode
    FirmwareManager::getInstance().handleArduinoOTA();
    delay(1000);
  } else {
    // In production, we should never reach here due to deep sleep
    Serial.println("Unexpected loop execution in production mode, entering deep sleep...");
    enterDeepSleep();
  }
}
