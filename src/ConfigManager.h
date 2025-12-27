#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <Arduino.h>

// Operating mode enumeration
enum OperatingMode {
  DEVELOPMENT = 0,
  PRODUCTION = 1
};

/**
 * ValidationResult - Result of configuration validation
 *
 * Contains validation status and error message if invalid.
 */
struct ValidationResult {
  bool valid;
  String errorMessage;
};

/**
 * ConfigManager - Singleton class for managing device configuration
 *
 * Provides in-memory caching of configuration to minimize filesystem I/O.
 * All getters read from cache (fast), all setters update cache and persist to flash.
 *
 * Usage:
 *   ConfigManager::getInstance().begin();  // Load config from filesystem
 *   String key = ConfigManager::getInstance().getApiKey();  // Fast cache read
 *   ConfigManager::getInstance().setApiKey("new_key");      // Update cache + flash
 */
class ConfigManager {
public:
  // Get singleton instance
  static ConfigManager& getInstance();

  // Initialize config manager (must be called before any other methods)
  bool begin();

  // Getters - read from in-memory cache (fast, no filesystem access)
  String getApiKey() const;
  String getUprn() const;
  String getFirmwareVersion() const;
  String getLastUpdateString() const;
  OperatingMode getOperatingMode() const;
  String getPreviousVersion() const;
  bool isUpdatePending() const;

  // Utility method to build web service URL from config
  String buildWebServiceURL() const;

  // Validation methods
  ValidationResult validateConfig() const;
  bool isApiKeyValid() const;
  bool isUprnValid() const;

  // Setters - update cache and save to filesystem
  void setApiKey(const String& key);
  void setUprn(const String& uprn);
  void setFirmwareVersion(const String& version);
  void setLastUpdateString(const String& timestamp);
  void setPreviousVersion(const String& version);
  void setUpdatePending(bool pending);

  // Prevent copying
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;

private:
  // Private constructor for singleton
  ConfigManager() = default;

  // Configuration data structure (cached in memory)
  struct ConfigData {
    String apiKey;
    String uprn;
    String firmwareVersion;
    String lastUpdate;
    bool productionMode;
    String previousVersion;
    bool updatePending;
  } cache;

  bool cacheValid = false;

  // Internal methods
  void loadFromFilesystem();
  void saveToFilesystem();
  void createDefaultConfig();
};

#endif // CONFIGMANAGER_H
