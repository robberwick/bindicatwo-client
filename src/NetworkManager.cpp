#include "NetworkManager.h"
#include "ConfigManager.h"
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <time.h>

NetworkManager& NetworkManager::getInstance() {
  static NetworkManager instance;
  return instance;
}

void NetworkManager::setupWiFi() {
  Serial.println("NetworkManager: Setting up WiFi...");

  WiFiManager wifiManager;

  // Set timeout for configuration portal
  wifiManager.setConfigPortalTimeout(120); // 2 minutes

  // Try to connect to saved WiFi or start configuration portal
  if (!wifiManager.autoConnect("BinScheduleAP", "binschedule")) {
    Serial.println("NetworkManager: Failed to connect to WiFi and hit timeout");
    ESP.restart();
  }

  Serial.println("NetworkManager: WiFi connected successfully!");
  Serial.print("NetworkManager: IP address: ");
  Serial.println(WiFi.localIP());
}

void NetworkManager::reconnectWiFi() {
  Serial.println("NetworkManager: Reconnecting to WiFi after wake up...");

  // Check if WiFi is already connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("NetworkManager: WiFi already connected");
    Serial.print("NetworkManager: IP address: ");
    Serial.println(WiFi.localIP());
    return;
  }

  // Try to reconnect with stored credentials first
  WiFi.begin();

  // Wait up to 10 seconds for connection
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("NetworkManager: WiFi reconnected successfully!");
    Serial.print("NetworkManager: IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("NetworkManager: Failed to reconnect with stored credentials, starting WiFi manager...");
    setupWiFi(); // Fall back to WiFiManager
  }
}

String NetworkManager::fetchDataFromWebService() {
  // Build URL with API key from ConfigManager
  String url = ConfigManager::getInstance().buildWebServiceURL();
  if (url.length() == 0) {
    Serial.println("NetworkManager: Cannot build URL - API key not configured");
    return "";
  }

  Serial.println("NetworkManager: Fetching data from web service...");

  // Use retry logic with up to 3 retries
  HttpResult result = httpGetWithRetry(url, 3);

  if (result.error == HttpError::SUCCESS) {
    Serial.println("NetworkManager: Data fetched successfully");

    // Store current time in DD-MM HH:MM format
    time_t now = time(nullptr);
    String timestamp = formatTime(now);
    Serial.printf("NetworkManager: Storing last update time: %s\n", timestamp.c_str());
    ConfigManager::getInstance().setLastUpdateString(timestamp);

    return result.data;
  } else {
    Serial.printf("NetworkManager: Failed to fetch data after retries (error: %d, status: %d)\n",
                  static_cast<int>(result.error), result.statusCode);
    return "";
  }
}

bool NetworkManager::syncNTP() {
  Serial.println("NetworkManager: Syncing NTP time...");

  // Set timezone to GMT (UTC)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  // Wait up to ~10 seconds for time to be set
  const time_t MIN_VALID_TIME = 1609459200; // 2021-01-01 as a sanity threshold
  int retries = 100; // 100 * 100ms = 10s
  while (retries-- > 0) {
    time_t now = time(nullptr);
    if (now >= MIN_VALID_TIME) {
      Serial.printf("NetworkManager: NTP sync successful: %ld\n", (long)now);
      return true;
    }
    delay(100);
  }

  Serial.println("NetworkManager: NTP sync failed or timed out");
  return false;
}

String NetworkManager::formatTime(time_t t) {
  struct tm timeinfo;
  gmtime_r(&t, &timeinfo);
  char buf[20];
  // Format as DD-MM HH:MM GMT
  strftime(buf, sizeof(buf), "%d-%m %H:%M GMT", &timeinfo);
  return String(buf);
}

HttpResult NetworkManager::performHttpGet(const String& url) {
  HttpResult result;
  result.error = HttpError::UNKNOWN;
  result.statusCode = -1;
  result.shouldRetry = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("NetworkManager: WiFi not connected");
    result.error = HttpError::CONNECTION_FAILED;
    result.shouldRetry = true;
    return result;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.printf("NetworkManager: GET %s\n", url.c_str());

  if (!http.begin(client, url)) {
    Serial.println("NetworkManager: Failed to begin HTTP connection (invalid URL or DNS failure)");
    result.error = HttpError::DNS_FAILED;
    result.shouldRetry = true;
    return result;
  }

  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000); // 10 second timeout

  int httpCode = http.GET();
  result.statusCode = httpCode;

  if (httpCode > 0) {
    Serial.printf("NetworkManager: HTTP response code: %d\n", httpCode);

    if (httpCode == 200) {
      result.data = http.getString();
      result.error = HttpError::SUCCESS;
      result.shouldRetry = false;
    } else if (httpCode >= 400 && httpCode < 500) {
      // Client errors (4xx) - don't retry
      result.error = HttpError::CLIENT_ERROR;
      result.shouldRetry = false;
      Serial.printf("NetworkManager: Client error %d - will not retry\n", httpCode);
    } else if (httpCode >= 500 && httpCode < 600) {
      // Server errors (5xx) - retry
      result.error = HttpError::SERVER_ERROR;
      result.shouldRetry = true;
      Serial.printf("NetworkManager: Server error %d - will retry\n", httpCode);
    } else {
      result.error = HttpError::UNKNOWN;
      result.shouldRetry = false;
    }
  } else {
    // HTTP error codes are negative for library errors
    Serial.printf("NetworkManager: HTTP error: %s\n", http.errorToString(httpCode).c_str());

    if (httpCode == HTTPC_ERROR_CONNECTION_FAILED ||
        httpCode == HTTPC_ERROR_CONNECTION_LOST) {
      result.error = HttpError::CONNECTION_FAILED;
      result.shouldRetry = true;
    } else if (httpCode == HTTPC_ERROR_READ_TIMEOUT) {
      result.error = HttpError::TIMEOUT;
      result.shouldRetry = true;
    } else {
      result.error = HttpError::UNKNOWN;
      result.shouldRetry = true;
    }
  }

  http.end();
  return result;
}

HttpResult NetworkManager::httpGetWithRetry(const String& url, int maxRetries) {
  HttpResult result;
  int attempt = 0;

  while (attempt <= maxRetries) {
    stats.totalRequests++;

    if (attempt > 0) {
      stats.retried++;
      Serial.printf("NetworkManager: Retry attempt %d/%d\n", attempt, maxRetries);
      delay(1000 * attempt); // Exponential backoff: 1s, 2s, 3s
    }

    result = performHttpGet(url);

    if (result.error == HttpError::SUCCESS) {
      stats.successful++;
      Serial.println("NetworkManager: Request successful");
      return result;
    }

    if (!result.shouldRetry) {
      stats.failed++;
      Serial.printf("NetworkManager: Request failed with non-retryable error\n");
      return result;
    }

    attempt++;
  }

  // All retries exhausted
  stats.failed++;
  Serial.printf("NetworkManager: All %d retry attempts exhausted\n", maxRetries);
  return result;
}

void NetworkManager::printStats() {
  Serial.println("=== NetworkManager Statistics ===");
  Serial.printf("Total requests: %d\n", stats.totalRequests);
  Serial.printf("Successful: %d\n", stats.successful);
  Serial.printf("Failed: %d\n", stats.failed);
  Serial.printf("Retried: %d\n", stats.retried);
  Serial.println("================================");
}
