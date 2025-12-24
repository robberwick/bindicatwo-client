// base class GxEPD2_GFX can be used to pass references or pointers to the display instance as parameter, uses ~1.2k more code
// enable or disable GxEPD2_GFX base class
#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <time.h>
#include <map>
#include <vector>

#include "ConfigManager.h"

// Firmware version
#define FIRMWARE_VERSION "0.0.1"

// Sleep intervals (in microseconds)
const uint64_t DEVELOPMENT_SLEEP_INTERVAL = 20ULL * 1000000;    // 20 seconds for development
const uint64_t PRODUCTION_SLEEP_INTERVAL = 3ULL * 60 * 60 * 1000000; // 3 hours for production

// ESP8266 CS(SS)=15,SCL(SCK)=14,SDA(MOSI)=13,BUSY=12,RES(RST)=5,DC=4
#define CS_PIN (15)
#define BUSY_PIN (12)
#define RES_PIN (5)
#define DC_PIN (4)

// epaper module harness pin map:

// BUSY (PURPLE) - 12 (D6)
// RES (ORANGE) - 5 (D1)
// DC (WHITE) - 4 (D2)
// CS (BLUE) - 15 (D8)
// SCL (GREEN) - 14 (D5)
// SDA (YELLOW) - 13 (D7)
// GND (BLACK) - 0 (GND)
// VCC (RED) - 3.3V

 // D0 - RST should be jumpered together

// 2.9'' EPD Module
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(GxEPD2_290_C90c(/*CS=5*/ CS_PIN, /*DC=*/ DC_PIN, /*RES=*/ RES_PIN, /*BUSY=*/ BUSY_PIN)); // GDEM029C90 128x296, SSD1680

// Firmware update URLs
const char* firmwareVersionURL = "http://bindicator.berwick.me.uk/firmware/version.txt";
const char* firmwareDownloadURL = "http://bindicator.berwick.me.uk/firmware/bindicator.bin";

// Forward declarations
void displayError(const char* errorMessage);
void displayUpdateStatus(const char* message);
bool checkForFirmwareUpdate();
void performOTAUpdate();
void setupArduinoOTA();
void reconnectWiFi();
void performPeriodicUpdate();
void enterDeepSleep();
String formatUpdateTime(time_t t);
bool syncNTP();

void setupWiFi() {
  Serial.println("Setting up WiFi...");

  WiFiManager wifiManager;

  // Set timeout for configuration portal
  wifiManager.setConfigPortalTimeout(120); // 2 minutes

  // Uncomment to reset saved settings (for testing)
  // wifiManager.resetSettings();

  // Try to connect to saved WiFi or start configuration portal
  if (!wifiManager.autoConnect("BinScheduleAP", "binschedule")) {
    Serial.println("Failed to connect to WiFi and hit timeout");
    displayError("WiFi connection failed");
    ESP.restart();
  }

  Serial.println("WiFi connected successfully!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

String fetchDataFromWebService() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected");
    return "";
  }

  WiFiClient client;
  HTTPClient http;

  // Build URL with API key from ConfigManager
  String url = ConfigManager::getInstance().buildWebServiceURL();
  if (url.length() == 0) {
    Serial.println("Cannot build URL - API key not configured");
    return "";
  }

  Serial.println("Fetching data from web service...");
  Serial.printf("Using URL: %s\n", url.c_str());
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.GET();
  String response = "";

  if (httpResponseCode == 200) {
    response = http.getString();
    Serial.println("Data fetched successfully");

    // Store current time in DD:MM HH:MM format
    time_t now = time(nullptr);
    String timestamp = formatUpdateTime(now);
    Serial.printf("Storing last update time: %s\n", timestamp.c_str());
    ConfigManager::getInstance().setLastUpdateString(timestamp);
  } else {
    Serial.printf("HTTP error: %d\n", httpResponseCode);
  }

  http.end();
  return response;
}

void displayError(const char* errorMessage) {
  // rotation is set globally in setup()
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 40);
    display.print("Error:");
    display.setCursor(10, 70);
    display.print(errorMessage);
    display.setCursor(10, 100);
    display.print("Check connection");
  } while (display.nextPage());
}

void displayUpdateStatus(const char* message) {
  // rotation is set globally in setup()
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 40);
    display.print("Firmware Update");
    display.setCursor(10, 70);
    display.print(message);
  } while (display.nextPage());
}

bool checkForFirmwareUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected for update check");
    return false;
  }

  WiFiClient client;
  HTTPClient http;

  Serial.println("Checking for firmware updates...");
  http.begin(client, firmwareVersionURL);

  int httpResponseCode = http.GET();
  if (httpResponseCode == 200) {
    String latestVersion = http.getString();
    latestVersion.trim();

    // Get the current running version from ConfigManager
    String currentVersion = ConfigManager::getInstance().getFirmwareVersion();

    Serial.printf("Current version: %s\n", currentVersion.c_str());
    Serial.printf("Latest version: %s\n", latestVersion.c_str());

    http.end();

    // Compare versions (simple string comparison)
    if (latestVersion != currentVersion) {
      Serial.println("New firmware version available!");
      return true;
    } else {
      Serial.println("Firmware is up to date");
      return false;
    }
  } else {
    Serial.printf("Failed to check version: HTTP %d\n", httpResponseCode);
    http.end();
    return false;
  }
}

void performOTAUpdate() {
  displayUpdateStatus("Downloading...");

  // First, get the version we're about to download so we can store it in config.json after successful update
  WiFiClient versionClient;
  HTTPClient versionHttp;
  String targetVersion = "";

  Serial.println("Getting target version to store in config.json...");
  versionHttp.begin(versionClient, firmwareVersionURL);
  int versionResponse = versionHttp.GET();
  if (versionResponse == 200) {
    targetVersion = versionHttp.getString();
    targetVersion.trim();
    Serial.printf("Target version for update: %s\n", targetVersion.c_str());
  }
  versionHttp.end();

  WiFiClient client;

  // Configure the HTTP update
  ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);
  ESPhttpUpdate.onStart([]() {
    Serial.println("OTA Update started");
    displayUpdateStatus("Installing...");
  });

  ESPhttpUpdate.onEnd([targetVersion]() {
    Serial.println("OTA Update finished successfully");

    // Update config.json with the new version before restart to prevent download loops
    if (targetVersion.length() > 0) {
      Serial.printf("Updating config.json with new version: %s\n", targetVersion.c_str());
      ConfigManager::getInstance().setFirmwareVersion(targetVersion);
      Serial.println("Config.json updated successfully before restart");
    }

    displayUpdateStatus("Update complete!");
  });

  ESPhttpUpdate.onProgress([](int cur, int total) {
    Serial.printf("OTA Progress: %u%%\n", (unsigned int)((cur * 100) / total));
  });

  ESPhttpUpdate.onError([](int error) {
    Serial.printf("OTA Error: %s\n", ESPhttpUpdate.getLastErrorString().c_str());
    displayError("Update failed");
  });

  // Perform the update
  Serial.println("Starting HTTP OTA update...");
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, firmwareDownloadURL);

  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("HTTP OTA failed (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
      displayError("Update failed");
      break;

    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("HTTP OTA: no updates available");
      break;

    case HTTP_UPDATE_OK:
      Serial.println("HTTP OTA: update successful, restarting...");
      displayUpdateStatus("Restarting...");
      delay(2000);
      ESP.restart();
      break;
  }
}

void setupArduinoOTA() {
  // Arduino OTA setup for development/emergency updates
  ArduinoOTA.setHostname("bindicator");
  ArduinoOTA.setPassword("binschedule"); // Set a password for security

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
    displayUpdateStatus("OTA Upload...");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    displayUpdateStatus("OTA Complete!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
      displayError("OTA Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
      displayError("OTA Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
      displayError("OTA Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
      displayError("OTA Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
      displayError("OTA End Failed");
    }
  });

  ArduinoOTA.begin();
  Serial.println("Arduino OTA ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void displayBinSchedule() {
  // First try to get data from web service
  String jsonData = fetchDataFromWebService();

  // rotation is set globally in setup()
  display.setFullWindow();

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    displayError("JSON Parse Error");
    return;
  }

  // Group bins by days_until
  struct BinInfo {
    String type;
    String binType;
    bool isNext;
  };

  std::map<int, std::vector<BinInfo>> binsByDays;

  for (JsonObject bin : doc.as<JsonArray>()) {
    BinInfo info;
    info.type = bin["type"].as<String>();
    info.binType = bin["bin"].as<String>();
    info.isNext = bin["next"];
    int daysUntil = bin["days_until"];

    binsByDays[daysUntil].push_back(info);
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    // int yPos = 45;
    int yPos = 5;

    // Display bins grouped by days
    for (const auto& entry : binsByDays) {
      int daysUntil = entry.first;
      const std::vector<BinInfo>& bins = entry.second;

      // Check if we have space for at least the header and one bin
      if (yPos > display.height() - 30) break;

      // Determine if any bin in this group is marked as "next"
      bool groupIsNext = false;
      for (const auto& bin : bins) {

        if (bin.isNext) {
          groupIsNext = true;
          break;
        }
      }

      // Set spacing and fonts based on whether this is the next collection
      int lineHeight = groupIsNext ? 18 : 14;        // Larger spacing for 9pt font, smaller for default font
      int headerSpacing = groupIsNext ? 18 : 5;      // Spacing after header underline (less for smaller font)
      int binDescSpacing = groupIsNext ? 3 : 8;      // Spacing between bin type and description (more for default font)
      int groupSpacing = groupIsNext ? 8 : 3;        // Less spacing after subsequent groups

      // Check if we should use inverted display (TODAY or TOMORROW for next collection)
      bool useInverted = groupIsNext && (daysUntil <= 1) && display.epd2.hasColor;

      // If inverted, calculate the height of the entire group and draw background rectangle
      int groupStartY = yPos;
      if (useInverted) {
        // Calculate total height needed for this group
        int headerHeight = 15 + 2 + headerSpacing;  // Space before + underline area + spacing after
        int binsHeight = bins.size() * (binDescSpacing + 8);  // Each bin: spacing + description height
        if (bins.size() > 1) {
          binsHeight += (bins.size() - 1) * (lineHeight + binDescSpacing);  // Add spacing between multiple bins
        }
        int totalHeight = headerHeight + binsHeight - 4;

        // Draw red filled rectangle as background
        display.fillRect(5, groupStartY, (int16_t)(display.width() - 10), totalHeight, GxEPD_RED);
      }

      // Display days header
      auto textColour = useInverted ? GxEPD_WHITE : (groupIsNext && display.epd2.hasColor ? GxEPD_RED : GxEPD_BLACK);
      auto headerFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      auto binFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      display.setFont(headerFont);
      display.setTextColor(textColour);

      // For custom fonts (9pt), yPos is baseline, so text drawn above
      // For default font, yPos is top, so text drawn below
      // Adjust yPos before setting cursor to prevent text clashing
      if (groupIsNext) {
        yPos += 15;  // Space before first group header (custom font needs space above baseline)
      } else {
        yPos += 8;   // Space before subsequent group headers (default font, less needed)
      }

      display.setCursor(10, (int16_t)yPos);

      if (daysUntil == 0) {
        display.print("TODAY");
      } else if (daysUntil == 1) {
        display.print("TOMORROW");
      } else {
        display.print(String(daysUntil) + " DAYS");
      }

      // For custom fonts, yPos is the baseline; for default font, it's the top
      // Adjust accordingly for underline placement
      if (groupIsNext) {
        yPos = yPos + 2;  // Custom font: small gap after baseline
      } else {
        yPos = yPos + 8 + 2;  // Default font: font height (8px) + small gap
      }

      display.drawLine(10, yPos, (int16_t)(display.width() - 10), yPos, textColour);

      yPos += headerSpacing;

      // Display all bins for this day
      for (size_t i = 0; i < bins.size(); i++) {
        const auto& bin = bins[i];

        // Check if we're running out of space
        if (yPos > display.height() - 20) break;

        display.setFont(binFont);

        display.setCursor(14, (int16_t)yPos);
        display.print(bin.type);
        yPos += binDescSpacing;

        // Display bin description (smaller font)
        display.setFont();
        display.setCursor(15, (int16_t)yPos);
        display.print(bin.binType);

        // Only add bin spacing if this is not the last bin in the group
        if (i < bins.size() - 1) {
          yPos += lineHeight + binDescSpacing;
        }
      }

      // Add spacing between day groups
      yPos += groupSpacing;
    }

    // Footer with last update info and WiFi status
    display.setFont();
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, (int16_t)(display.height() - 10));
    display.print("Updated: ");
    display.print(ConfigManager::getInstance().getLastUpdateString());
    display.print(" | ");
    if (ConfigManager::getInstance().getOperatingMode() == PRODUCTION) {
      display.print("Version: ");
      display.print(ConfigManager::getInstance().getFirmwareVersion());
    } else {
      display.print("Mode: DEV");
    }

  } while (display.nextPage());
}

void reconnectWiFi() {
  Serial.println("Reconnecting to WiFi after wake up...");

  // Check if WiFi is already connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi already connected");
    Serial.print("IP address: ");
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
    Serial.println("WiFi reconnected successfully!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("");
    Serial.println("Failed to reconnect with stored credentials, starting WiFi manager...");
    setupWiFi(); // Fall back to WiFiManager
  }
}

bool syncNTP() {
  Serial.println("Syncing NTP time...");

  // Set timezone to GMT (UTC)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  // Wait up to ~10 seconds for time to be set
  const time_t MIN_VALID_TIME = 1609459200; // 2021-01-01 as a sanity threshold
  int retries = 100; // 100 * 100ms = 10s
  while (retries-- > 0) {
    time_t now = time(nullptr);
    if (now >= MIN_VALID_TIME) {
      Serial.printf("NTP sync successful: %ld\n", (long)now);
      return true;
    }
    delay(100);
  }

  Serial.println("NTP sync failed or timed out");
  return false;
}

String formatUpdateTime(time_t t) {
  struct tm timeinfo;
  gmtime_r(&t, &timeinfo);
  char buf[20];
  // Format as DD-MM HH:MM GMT
  strftime(buf, sizeof(buf), "%d-%m %H:%M GMT", &timeinfo);
  return String(buf);
}

void performPeriodicUpdate() {
  Serial.println("Performing periodic update...");

  // Reconnect WiFi
  reconnectWiFi();

  // Re-sync NTP after wake for accurate time
  syncNTP();

  // Check for firmware updates (less frequently in production)
  static int updateCheckCounter = 0;
  updateCheckCounter++;

  // Check for updates every 10th wake up in production, every wake up in development
  bool shouldCheckUpdates = ConfigManager::getInstance().getOperatingMode() == PRODUCTION ? (updateCheckCounter % 10 == 0) : true;

  if (shouldCheckUpdates && checkForFirmwareUpdate()) {
    performOTAUpdate();
  }

  // Display updated bin schedule
  displayBinSchedule();

  // Put display to sleep
  display.hibernate();

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

  display.init(115200, true, 50, false);
  display.setRotation(3); // enforce 180-degree rotation globally

  // Check if this is a wake up from deep sleep
  rst_info *resetInfo = ESP.getResetInfoPtr();
  if (resetInfo->reason == REASON_DEEP_SLEEP_AWAKE) {
    Serial.println("Woke up from deep sleep - performing periodic update");
    performPeriodicUpdate();
    enterDeepSleep();
  } else {
    Serial.println("Fresh boot - performing initial setup");

    // Setup WiFi connection
    setupWiFi();

    // Sync NTP time on boot
    syncNTP();

    // Setup Arduino OTA for development updates (only in development mode)
    if (ConfigManager::getInstance().getOperatingMode() == DEVELOPMENT) {
      setupArduinoOTA();
    }

    // Check for HTTP OTA updates
    if (checkForFirmwareUpdate()) {
      performOTAUpdate();
    }

    // Display the bin schedule
    displayBinSchedule();

    display.hibernate();

    // Enter deep sleep cycle
    enterDeepSleep();
  }
}

void loop() {
  // This should never be reached when using deep sleep
  // Only used in development mode when deep sleep might be disabled for debugging
  if (ConfigManager::getInstance().getOperatingMode() == DEVELOPMENT) {
    // Handle Arduino OTA in development mode
    ArduinoOTA.handle();
    delay(1000);
  } else {
    // In production, we should never reach here due to deep sleep
    Serial.println("Unexpected loop execution in production mode, entering deep sleep...");
    enterDeepSleep();
  }
}
