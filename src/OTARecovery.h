#ifndef OTARECOVERY_H
#define OTARECOVERY_H

#include <Arduino.h>

// Magic number for validating RTC memory
#define OTA_RECOVERY_MAGIC 0xBEEFCAFE

// Maximum number of consecutive boot failures before rollback
#define MAX_BOOT_FAILURES 3

// Minimum number of stable boots required to commit an update
#define MIN_STABLE_BOOTS 3

/**
 * RTC memory structure for OTA recovery and boot tracking
 * Persists across deep sleep but NOT across power loss
 */
struct OTARecoveryData {
  uint32_t magic;                // Validation marker (0xBEEFCAFE)
  uint32_t bootCount;            // Consecutive boots since last update
  uint32_t updateCheckCounter;   // Persists across sleep for production update checks
  char previousVersion[16];      // Version to rollback to if update fails
  char targetVersion[16];        // Version we're updating to
  bool updateInProgress;         // True if update downloaded but not yet stable
  uint32_t failedBootCount;      // Number of failed boots in current cycle
  time_t lastNTPSync;            // Last successful NTP sync timestamp
};

/**
 * OTA Recovery Manager - Singleton
 * Manages RTC memory for boot loop detection and automatic rollback
 */
class OTARecovery {
public:
  static OTARecovery& getInstance();

  // Initialize RTC memory (call early in setup)
  void begin();

  // Boot tracking
  void incrementBootCount();
  void resetBootCount();
  uint32_t getBootCount() const;

  // Failed boot tracking
  void incrementFailedBootCount();
  void resetFailedBootCount();
  uint32_t getFailedBootCount() const;
  bool hasExceededMaxBootFailures() const;

  // Update tracking
  void markUpdateStart(const String& currentVersion, const String& targetVersion);
  void markUpdateComplete();
  bool isUpdateInProgress() const;
  String getPreviousVersion() const;
  String getTargetVersion() const;

  // Update check counter (for production mode every-10th-wake checks)
  void incrementUpdateCheckCounter();
  void resetUpdateCheckCounter();
  uint32_t getUpdateCheckCounter() const;

  // NTP sync tracking
  void setLastNTPSync(time_t timestamp);
  time_t getLastNTPSync() const;

  // Debug
  void printStatus() const;
  void reset(); // Complete reset of RTC memory (use with caution)

private:
  OTARecovery() = default;
  OTARecovery(const OTARecovery&) = delete;
  OTARecovery& operator=(const OTARecovery&) = delete;

  void initializeRTCMemory();
  void validateAndLoadRTCMemory();
  void saveRTCMemory();

  OTARecoveryData data;
  bool initialized = false;
};

#endif // OTARECOVERY_H
