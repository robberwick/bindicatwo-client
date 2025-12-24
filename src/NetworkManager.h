#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include <Arduino.h>

/**
 * HTTP error classification for retry logic
 */
enum class HttpError {
  SUCCESS = 0,
  TIMEOUT,
  CONNECTION_FAILED,
  DNS_FAILED,
  CLIENT_ERROR,      // 4xx - don't retry
  SERVER_ERROR,      // 5xx - retry
  UNKNOWN
};

/**
 * HTTP request result with error classification
 */
struct HttpResult {
  HttpError error;
  int statusCode;
  String data;
  bool shouldRetry;
};

/**
 * NetworkManager - Singleton class for managing network operations
 *
 * Handles WiFi connectivity, HTTP requests with retry logic, and NTP time synchronization.
 *
 * Usage:
 *   NetworkManager::getInstance().setupWiFi();
 *   NetworkManager::getInstance().syncNTP();
 *   HttpResult result = NetworkManager::getInstance().httpGetWithRetry(url);
 *   if (result.error == HttpError::SUCCESS) {
 *     // Use result.data
 *   }
 */
class NetworkManager {
public:
  // Get singleton instance
  static NetworkManager& getInstance();

  // WiFi management
  void setupWiFi();
  void reconnectWiFi();

  // HTTP operations
  String fetchDataFromWebService();
  HttpResult httpGetWithRetry(const String& url, int maxRetries = 3);

  // Time synchronization
  bool syncNTP();
  String formatTime(time_t t);

  // Statistics
  void printStats();

  // Prevent copying
  NetworkManager(const NetworkManager&) = delete;
  NetworkManager& operator=(const NetworkManager&) = delete;

private:
  // Private constructor for singleton
  NetworkManager() = default;

  // HTTP helper methods
  HttpResult performHttpGet(const String& url);

  // Request statistics
  struct {
    int totalRequests = 0;
    int successful = 0;
    int failed = 0;
    int retried = 0;
  } stats;
};

#endif // NETWORKMANAGER_H
