#include "FirmwareManager.h"
#include "ConfigManager.h"
#include "OTARecovery.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>

FirmwareManager& FirmwareManager::getInstance() {
  static FirmwareManager instance;
  return instance;
}

void FirmwareManager::setDisplayCallback(DisplayCallback callback) {
  displayCallback = callback;
}

bool FirmwareManager::checkForUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("FirmwareManager: WiFi not connected for update check");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println("FirmwareManager: Checking for firmware updates...");
  http.begin(client, FIRMWARE_VERSION_URL);

  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String latestVersion = http.getString();
    latestVersion.trim();

    // Get the current running version from ConfigManager
    String currentVersion = ConfigManager::getInstance().getFirmwareVersion();

    Serial.printf("FirmwareManager: Current version: %s\n", currentVersion.c_str());
    Serial.printf("FirmwareManager: Latest version: %s\n", latestVersion.c_str());

    http.end();

    // Compare versions (simple string comparison)
    if (latestVersion != currentVersion) {
      Serial.println("FirmwareManager: New firmware version available!");
      return true;
    } else {
      Serial.println("FirmwareManager: Firmware is up to date");
      return false;
    }
  } else {
    Serial.printf("FirmwareManager: Failed to check version: HTTP %d\n", httpResponseCode);
    http.end();
    return false;
  }
}

void FirmwareManager::performHTTPUpdate() {
  if (displayCallback) {
    displayCallback("Downloading...");
  }

  // Get the current version and target version
  String currentVersion = ConfigManager::getInstance().getFirmwareVersion();

  WiFiClient versionClient;
  HTTPClient versionHttp;
  String targetVersion = "";

  Serial.println("FirmwareManager: Getting target version for OTA rollback setup...");
  versionHttp.begin(versionClient, FIRMWARE_VERSION_URL);
  int versionResponse = versionHttp.GET();
  if (versionResponse == 200) {
    targetVersion = versionHttp.getString();
    targetVersion.trim();
    Serial.printf("FirmwareManager: Target version for update: %s\n", targetVersion.c_str());
  }
  versionHttp.end();

  // CRITICAL: Prepare for OTA with rollback support
  // Mark update as in progress and save rollback information BEFORE downloading
  Serial.println("FirmwareManager: Preparing OTA rollback safety net...");
  OTARecovery::getInstance().markUpdateStart(currentVersion, targetVersion);

  // Save previous version to config for potential rollback
  ConfigManager::getInstance().setPreviousVersion(currentVersion);

  // Mark update as pending - version will only be committed after stable boots
  ConfigManager::getInstance().setUpdatePending(true);

  Serial.printf("FirmwareManager: Rollback prepared - current: %s, target: %s\n",
                currentVersion.c_str(), targetVersion.c_str());

  WiFiClient client;

  // Configure the HTTP update (ESP32 API)
  httpUpdate.setLedPin(LED_BUILTIN, LOW);

  // Store callback in variable for lambda capture
  auto callback = displayCallback;

  httpUpdate.onStart([callback]() {
    Serial.println("FirmwareManager: OTA Update started");
    if (callback) {
      callback("Installing...");
    }
  });

  httpUpdate.onEnd([]() {
    Serial.println("FirmwareManager: OTA Update finished successfully");

    // DO NOT update firmware_version here!
    // Version will be committed only after boot stability check in main.cpp
    // This prevents boot loops from buggy firmware
    Serial.println("FirmwareManager: Update pending - version will commit after stable boots");
  });

  httpUpdate.onProgress([](int cur, int total) {
    Serial.printf("FirmwareManager: OTA Progress: %u%%\n", (unsigned int)((cur * 100) / total));
  });

  httpUpdate.onError([callback](int error) {
    Serial.printf("FirmwareManager: OTA Error: %s\n", httpUpdate.getLastErrorString().c_str());
    if (callback) {
      callback("Update failed");
    }
  });

  // Perform the update
  Serial.println("FirmwareManager: Starting HTTP OTA update...");
  t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_DOWNLOAD_URL);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("FirmwareManager: HTTP OTA failed (%d): %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      if (displayCallback) {
        displayCallback("Update failed");
      }
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("FirmwareManager: HTTP OTA: no updates available");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("FirmwareManager: HTTP OTA: update successful, restarting...");
      if (displayCallback) {
        displayCallback("Restarting...");
      }
      delay(2000);
      ESP.restart();
      break;
  }
}

void FirmwareManager::setupArduinoOTA() {
  Serial.println("FirmwareManager: Setting up Arduino OTA...");

  // Arduino OTA setup for development/emergency updates
  ArduinoOTA.setHostname("bindicator");
  ArduinoOTA.setPassword("binschedule"); // Set a password for security

  // Store callback for lambda capture
  auto callback = displayCallback;

  ArduinoOTA.onStart([callback]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS (filesystem - ESP32 uses U_FS instead of U_SPIFFS)
      type = "filesystem";
    }
    Serial.println("FirmwareManager: Start updating " + type);
    if (callback) {
      callback("OTA Upload...");
    }
  });

  ArduinoOTA.onEnd([callback]() {
    Serial.println("FirmwareManager: Arduino OTA End");
    if (callback) {
      callback("OTA Complete!");
    }
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("FirmwareManager: Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([callback](ota_error_t error) {
    Serial.printf("FirmwareManager: Error[%u]: ", error);
    const char* errorMsg = "OTA Error";
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      errorMsg = "OTA Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      errorMsg = "OTA Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      errorMsg = "OTA Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      errorMsg = "OTA Receive Failed";
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      errorMsg = "OTA End Failed";
    }
    if (callback) {
      callback(errorMsg);
    }
  });

  ArduinoOTA.begin();
  Serial.println("FirmwareManager: Arduino OTA ready");
  Serial.print("FirmwareManager: IP address: ");
  Serial.println(WiFi.localIP());
}

void FirmwareManager::handleArduinoOTA() {
  ArduinoOTA.handle();
}
