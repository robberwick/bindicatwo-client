#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

#define CONFIG_FILE "/config.json"
#define DEFAULT_FIRMWARE_VERSION "0.0.0"

ConfigManager& ConfigManager::getInstance() {
  static ConfigManager instance;
  return instance;
}

bool ConfigManager::begin() {
  Serial.println("ConfigManager: Initializing...");

  // Mount LittleFS with recovery on failure
  if (!LittleFS.begin()) {
    Serial.println("ConfigManager: Failed to mount LittleFS");
    Serial.println("ConfigManager: Attempting filesystem format and recovery...");

    // Try to format the filesystem
    if (LittleFS.format()) {
      Serial.println("ConfigManager: Format successful, retrying mount...");

      // Retry mount after format
      if (LittleFS.begin()) {
        Serial.println("ConfigManager: Mount successful after format");
        // Create default config since filesystem was wiped
        createDefaultConfig();

        Serial.println("ConfigManager: Recovery complete - filesystem formatted and default config created");
        Serial.printf("  API Key: %s\n", cache.apiKey.c_str());
        Serial.printf("  UPRN: %s\n", cache.uprn.c_str());
        Serial.printf("  Firmware Version: %s\n", cache.firmwareVersion.c_str());
        Serial.printf("  Operating Mode: %s\n", cache.productionMode ? "Production" : "Development");

        return cacheValid;
      } else {
        Serial.println("ConfigManager: FATAL - Mount failed even after format");
        return false;
      }
    } else {
      Serial.println("ConfigManager: FATAL - Format failed, filesystem may be hardware failure");
      return false;
    }
  }

  // Normal path - filesystem mounted successfully
  Serial.println("ConfigManager: LittleFS mounted successfully");

  // Check if config file exists
  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("ConfigManager: Config file not found, creating default");
    createDefaultConfig();
  }

  // Load config into cache
  loadFromFilesystem();

  Serial.println("ConfigManager: Initialization complete");
  Serial.printf("  API Key: %s\n", cache.apiKey.c_str());
  Serial.printf("  UPRN: %s\n", cache.uprn.c_str());
  Serial.printf("  Firmware Version: %s\n", cache.firmwareVersion.c_str());
  Serial.printf("  Operating Mode: %s\n", cache.productionMode ? "Production" : "Development");

  return cacheValid;
}

void ConfigManager::loadFromFilesystem() {
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("ConfigManager: Failed to open config file for reading");
    cacheValid = false;
    return;
  }

  // Use StaticJsonDocument with 256 bytes (config is ~150 bytes, 70% headroom)
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.printf("ConfigManager: Failed to parse config file: %s\n", error.c_str());
    cacheValid = false;
    return;
  }

  // Load values into cache
  cache.apiKey = doc["api_key"] | "";
  cache.uprn = doc["uprn"] | "";
  cache.firmwareVersion = doc["firmware_version"] | DEFAULT_FIRMWARE_VERSION;
  cache.lastUpdate = doc["last_update"] | "never";
  cache.productionMode = doc["production_mode"] | true;  // Default to production for safety

  cacheValid = true;
  Serial.println("ConfigManager: Config loaded into cache");
}

void ConfigManager::saveToFilesystem() {
  if (!cacheValid) {
    Serial.println("ConfigManager: Cache invalid, cannot save");
    return;
  }

  // Use StaticJsonDocument with 256 bytes
  StaticJsonDocument<256> doc;

  // Populate JSON from cache
  doc["api_key"] = cache.apiKey;
  doc["uprn"] = cache.uprn;
  doc["firmware_version"] = cache.firmwareVersion;
  doc["last_update"] = cache.lastUpdate;
  doc["production_mode"] = cache.productionMode;

  // Write to file
  File configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("ConfigManager: Failed to open config file for writing");
    return;
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("ConfigManager: Failed to write config file");
  } else {
    Serial.println("ConfigManager: Config saved to filesystem");
  }

  configFile.close();
}

void ConfigManager::createDefaultConfig() {
  // Set default values in cache
  cache.apiKey = "your_api_key_here";
  cache.uprn = "100081258147";
  cache.firmwareVersion = DEFAULT_FIRMWARE_VERSION;
  cache.lastUpdate = "never";
  cache.productionMode = false;  // Default to development for initial setup

  cacheValid = true;

  // Save to filesystem
  saveToFilesystem();

  Serial.println("ConfigManager: Default config created");
}

// Getters - all read from in-memory cache (fast!)

String ConfigManager::getApiKey() const {
  return cache.apiKey;
}

String ConfigManager::getUprn() const {
  return cache.uprn;
}

String ConfigManager::getFirmwareVersion() const {
  return cache.firmwareVersion;
}

String ConfigManager::getLastUpdateString() const {
  return cache.lastUpdate;
}

OperatingMode ConfigManager::getOperatingMode() const {
  return cache.productionMode ? PRODUCTION : DEVELOPMENT;
}

String ConfigManager::buildWebServiceURL() const {
  if (cache.apiKey.length() == 0) {
    Serial.println("ConfigManager: Error - API key not configured");
    return "";
  }

  if (cache.uprn.length() == 0) {
    Serial.println("ConfigManager: Error - UPRN not configured");
    return "";
  }

  // Base URL with UPRN in the path
  String baseUrl = "http://bindicator.berwick.me.uk/schedule/";

  // Build the complete URL: baseUrl + uprn + "/?api_key=" + apiKey
  String url = baseUrl + cache.uprn + "/?api_key=" + cache.apiKey;

  return url;
}

// Setters - update cache and save to filesystem

void ConfigManager::setApiKey(const String& key) {
  Serial.printf("ConfigManager: Setting API key to: %s\n", key.c_str());
  cache.apiKey = key;
  saveToFilesystem();
}

void ConfigManager::setUprn(const String& uprn) {
  Serial.printf("ConfigManager: Setting UPRN to: %s\n", uprn.c_str());
  cache.uprn = uprn;
  saveToFilesystem();
}

void ConfigManager::setFirmwareVersion(const String& version) {
  Serial.printf("ConfigManager: Setting firmware version to: %s\n", version.c_str());
  cache.firmwareVersion = version;
  saveToFilesystem();
}

void ConfigManager::setLastUpdateString(const String& timestamp) {
  Serial.printf("ConfigManager: Setting last update to: %s\n", timestamp.c_str());
  cache.lastUpdate = timestamp;
  saveToFilesystem();
}
