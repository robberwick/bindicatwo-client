#include "OTARecovery.h"

// RTC memory address for our data (block 64-96 are user-available)
#define RTC_MEMORY_BLOCK 64

OTARecovery& OTARecovery::getInstance() {
  static OTARecovery instance;
  return instance;
}

void OTARecovery::begin() {
  if (initialized) {
    return;
  }

  Serial.println("OTARecovery: Initializing...");
  validateAndLoadRTCMemory();
  initialized = true;
  printStatus();
}

void OTARecovery::initializeRTCMemory() {
  Serial.println("OTARecovery: Initializing fresh RTC memory");

  memset(&data, 0, sizeof(data));
  data.magic = OTA_RECOVERY_MAGIC;
  data.bootCount = 0;
  data.updateCheckCounter = 0;
  data.updateInProgress = false;
  data.failedBootCount = 0;
  data.lastNTPSync = 0;

  saveRTCMemory();
}

void OTARecovery::validateAndLoadRTCMemory() {
  // Calculate number of 4-byte blocks needed
  uint32_t rtcSize = (sizeof(OTARecoveryData) + 3) / 4;

  // Read from RTC memory
  if (ESP.rtcUserMemoryRead(RTC_MEMORY_BLOCK, (uint32_t*)&data, rtcSize)) {
    // Validate magic number
    if (data.magic == OTA_RECOVERY_MAGIC) {
      Serial.println("OTARecovery: Valid RTC memory found");
      return;
    } else {
      Serial.printf("OTARecovery: Invalid magic (0x%08X), expected 0x%08X\n",
                    data.magic, OTA_RECOVERY_MAGIC);
    }
  } else {
    Serial.println("OTARecovery: Failed to read RTC memory");
  }

  // If we get here, RTC memory is invalid - initialize fresh
  initializeRTCMemory();
}

void OTARecovery::saveRTCMemory() {
  uint32_t rtcSize = (sizeof(OTARecoveryData) + 3) / 4;

  if (!ESP.rtcUserMemoryWrite(RTC_MEMORY_BLOCK, (uint32_t*)&data, rtcSize)) {
    Serial.println("OTARecovery: WARNING - Failed to write RTC memory");
  }
}

// Boot tracking
void OTARecovery::incrementBootCount() {
  data.bootCount++;
  saveRTCMemory();
  Serial.printf("OTARecovery: Boot count incremented to %u\n", data.bootCount);
}

void OTARecovery::resetBootCount() {
  data.bootCount = 0;
  saveRTCMemory();
  Serial.println("OTARecovery: Boot count reset");
}

uint32_t OTARecovery::getBootCount() const {
  return data.bootCount;
}

// Failed boot tracking
void OTARecovery::incrementFailedBootCount() {
  data.failedBootCount++;
  saveRTCMemory();
  Serial.printf("OTARecovery: Failed boot count incremented to %u\n", data.failedBootCount);
}

void OTARecovery::resetFailedBootCount() {
  data.failedBootCount = 0;
  saveRTCMemory();
  Serial.println("OTARecovery: Failed boot count reset");
}

uint32_t OTARecovery::getFailedBootCount() const {
  return data.failedBootCount;
}

bool OTARecovery::hasExceededMaxBootFailures() const {
  return data.failedBootCount >= MAX_BOOT_FAILURES;
}

// Update tracking
void OTARecovery::markUpdateStart(const String& currentVersion, const String& targetVersion) {
  Serial.printf("OTARecovery: Marking update start: %s -> %s\n",
                currentVersion.c_str(), targetVersion.c_str());

  strncpy(data.previousVersion, currentVersion.c_str(), sizeof(data.previousVersion) - 1);
  data.previousVersion[sizeof(data.previousVersion) - 1] = '\0';

  strncpy(data.targetVersion, targetVersion.c_str(), sizeof(data.targetVersion) - 1);
  data.targetVersion[sizeof(data.targetVersion) - 1] = '\0';

  data.updateInProgress = true;
  data.bootCount = 0;
  data.failedBootCount = 0;

  saveRTCMemory();
}

void OTARecovery::markUpdateComplete() {
  Serial.println("OTARecovery: Marking update complete");

  data.updateInProgress = false;
  data.bootCount = 0;
  data.failedBootCount = 0;
  memset(data.previousVersion, 0, sizeof(data.previousVersion));
  memset(data.targetVersion, 0, sizeof(data.targetVersion));

  saveRTCMemory();
}

bool OTARecovery::isUpdateInProgress() const {
  return data.updateInProgress;
}

String OTARecovery::getPreviousVersion() const {
  return String(data.previousVersion);
}

String OTARecovery::getTargetVersion() const {
  return String(data.targetVersion);
}

// Update check counter
void OTARecovery::incrementUpdateCheckCounter() {
  data.updateCheckCounter++;
  saveRTCMemory();
}

void OTARecovery::resetUpdateCheckCounter() {
  data.updateCheckCounter = 0;
  saveRTCMemory();
}

uint32_t OTARecovery::getUpdateCheckCounter() const {
  return data.updateCheckCounter;
}

// NTP sync tracking
void OTARecovery::setLastNTPSync(time_t timestamp) {
  data.lastNTPSync = timestamp;
  saveRTCMemory();
}

time_t OTARecovery::getLastNTPSync() const {
  return data.lastNTPSync;
}

// Debug
void OTARecovery::printStatus() const {
  Serial.println("=== OTA Recovery Status ===");
  Serial.printf("Magic: 0x%08X (valid: %s)\n", data.magic,
                data.magic == OTA_RECOVERY_MAGIC ? "YES" : "NO");
  Serial.printf("Boot Count: %u\n", data.bootCount);
  Serial.printf("Failed Boot Count: %u\n", data.failedBootCount);
  Serial.printf("Update Check Counter: %u\n", data.updateCheckCounter);
  Serial.printf("Update In Progress: %s\n", data.updateInProgress ? "YES" : "NO");

  if (data.updateInProgress) {
    Serial.printf("Previous Version: %s\n", data.previousVersion);
    Serial.printf("Target Version: %s\n", data.targetVersion);
  }

  if (data.lastNTPSync > 0) {
    Serial.printf("Last NTP Sync: %ld\n", data.lastNTPSync);
  } else {
    Serial.println("Last NTP Sync: Never");
  }

  Serial.println("===========================");
}

void OTARecovery::reset() {
  Serial.println("OTARecovery: COMPLETE RESET - all tracking data cleared");
  initializeRTCMemory();
}
