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

// Firmware version
#define FIRMWARE_VERSION "0.0.1"

// LittleFS configuration file path
#define CONFIG_FILE "/config.json"

// Operating mode enumeration
enum OperatingMode {
  DEVELOPMENT = 0,
  PRODUCTION = 1
};

// Production/Development mode configuration
// This will be read from config.json at runtime, defaulting to PRODUCTION mode
OperatingMode OPERATING_MODE = PRODUCTION;

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
String getCurrentFirmwareVersion();
void setCurrentFirmwareVersion(const String& version);
void initializeConfig();
String getLastUpdateString();
void setLastUpdateString(const String& value);
String getApiKey();
void setApiKey(const String& apiKey);
String getUprn();
void setUprn(const String& uprn);
String buildWebServiceURL();
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

  // Build URL with API key from EEPROM
  String url = buildWebServiceURL();
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
    setLastUpdateString(timestamp);
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

    // Get the current running version (compile-time constant)
    String currentVersion = getCurrentFirmwareVersion();

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
      setCurrentFirmwareVersion(targetVersion);
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

    // Title
    // display.setFont(&FreeMonoBold9pt7b);
    // display.setTextColor(GxEPD_BLACK);
    // display.setCursor(10, 20);
    // display.print("Bin Collection Schedule");
    //
    // // Draw line under title
    // display.drawLine(10, 25, (int16_t)(display.width() - 10), 25, GxEPD_BLACK);

    // int yPos = 45;
    int yPos = 20;
    int lineHeight = 18;
    int groupSpacing = 5;

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

      // Display days header
      auto textColour = groupIsNext && display.epd2.hasColor ? GxEPD_RED : GxEPD_BLACK;
      auto headerFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      auto binFont = groupIsNext ? &FreeMono9pt7b : nullptr;
      display.setFont(headerFont);
      display.setTextColor(textColour);

      display.setCursor(10, (int16_t)yPos);

      if (daysUntil == 0) {
        display.print("TODAY");
      } else if (daysUntil == 1) {
        display.print("TOMORROW");
      } else {
        display.print(String(daysUntil) + " DAYS");
      }
      yPos = yPos + 2;
      display.drawLine(10, yPos, (int16_t)(display.width() - 10), yPos, textColour);

      yPos += lineHeight;

      // Display all bins for this day
      for (const auto& bin : bins) {
        // Check if we're running out of space
        if (yPos > display.height() - 30) break;

        display.setFont(binFont);

        display.setCursor(14, (int16_t)yPos);
        display.print(bin.type);
        yPos += 3;

        // Display bin description (smaller font)
        display.setFont();
        display.setCursor(15, (int16_t)yPos);
        display.print(bin.binType);

        yPos += lineHeight + 3;
      }

      // Add spacing between day groups
      yPos += groupSpacing;
    }

    // Footer with last update info and WiFi status
    display.setFont();
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, (int16_t)(display.height() - 10));
    display.print("Updated: ");
    display.print(getLastUpdateString());
    display.print(" | ");
    if (OPERATING_MODE == PRODUCTION) {
      display.print("Version: ");
      display.print(getCurrentFirmwareVersion());
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
  bool shouldCheckUpdates = OPERATING_MODE == PRODUCTION ? (updateCheckCounter % 10 == 0) : true;

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
  uint64_t sleepInterval = OPERATING_MODE == PRODUCTION ? PRODUCTION_SLEEP_INTERVAL : DEVELOPMENT_SLEEP_INTERVAL;

  Serial.printf("Entering deep sleep for %llu seconds (%s mode)...\n",
                sleepInterval / 1000000,
                OPERATING_MODE == PRODUCTION ? "Production" : "Development");

  // Ensure all serial output is sent
  Serial.flush();

  // Enter deep sleep
  ESP.deepSleep(sleepInterval);
}

// Configuration management functions
void initializeConfig() {
  Serial.println("Initializing configuration...");

  // Check if the config file exists
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS");
    return;
  }

  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("Config file not found, creating default config");
    File configFile = LittleFS.open(CONFIG_FILE, "w");
    if (!configFile) {
      Serial.println("Failed to create config file");
      return;
    }

    // Write default config
    JsonDocument doc;
    doc["api_key"] = "your_api_key_here";
    doc["uprn"] = "100081258147";
    doc["firmware_version"] = FIRMWARE_VERSION;
    doc["production_mode"] = false; // Default to development mode

    // Serialize JSON to file
    if (serializeJson(doc, configFile) == 0) {
      Serial.println("Failed to write config file");
    } else {
      Serial.println("Default config written, please update API key");
    }
    configFile.close();
  } else {
    Serial.println("Config file found, reading config");
    File configFile = LittleFS.open(CONFIG_FILE, "r");
    if (!configFile) {
      Serial.println("Failed to open config file");
      return;
    }

    // Deserialize JSON from file
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    if (error) {
      Serial.println("Failed to parse config file");
      configFile.close();
      return;
    }

    // Read values from JSON
    String apiKey = doc["api_key"];
    String uprn = doc["uprn"];
    String firmwareVersion = doc["firmware_version"];

    Serial.printf("API Key: %s\n", apiKey.c_str());
    Serial.printf("UPRN: %s\n", uprn.c_str());
    Serial.printf("Firmware Version: %s\n", firmwareVersion.c_str());

    // Read production mode setting
    OPERATING_MODE = doc["production_mode"] ? PRODUCTION : DEVELOPMENT; // Default to PRODUCTION if not present

    Serial.printf("Operating mode: %s\n", OPERATING_MODE == PRODUCTION ? "Production" : "Development");

    configFile.close();
  }
}

String getCurrentFirmwareVersion() {
  // Read firmware version from config.json
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS in getCurrentFirmwareVersion");
    return "0.0.0"; // Sentinel value
  }

  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file in getCurrentFirmwareVersion");
    return "0.0.0"; // Sentinel value
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file in getCurrentFirmwareVersion");
    return "0.0.0"; // Sentinel value
  }

  // Get firmware version from config, default to sentinel value if not present
  String version = doc["firmware_version"] | "0.0.0";
  return version;
}

void setCurrentFirmwareVersion(const String& version) {
  Serial.printf("Updating firmware version in config to: %s\n", version.c_str());

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS in setCurrentFirmwareVersion");
    return;
  }

  // Read the existing config
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file in setCurrentFirmwareVersion");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file in setCurrentFirmwareVersion");
    return;
  }

  // Update firmware version
  doc["firmware_version"] = version;

  // Write the updated config
  configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing in setCurrentFirmwareVersion");
    return;
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write config file in setCurrentFirmwareVersion");
  } else {
    Serial.printf("Firmware version updated to %s in config.json\n", version.c_str());
  }

  configFile.close();
}

String getLastUpdateString() {
  // Read last update time from config.json
  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS in getLastUpdateString");
    return "never";
  }

  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file in getLastUpdateString");
    return "never";
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file in getLastUpdateString");
    return "never";
  }

  // Get last_update from config, default to "never" if not present
  String lastUpdate = doc["last_update"] | "never";
  return lastUpdate;
}

void setLastUpdateString(const String& value) {
  Serial.printf("Storing last update time: %s\n", value.c_str());

  if (!LittleFS.begin()) {
    Serial.println("Failed to mount LittleFS in setLastUpdateString");
    return;
  }

  // Read the existing config
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file in setLastUpdateString");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file in setLastUpdateString");
    return;
  }

  // Update last update time
  doc["last_update"] = value;

  // Write the updated config
  configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing in setLastUpdateString");
    return;
  }

  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write config file in setLastUpdateString");
  } else {
    Serial.printf("Last update time saved to config.json: %s\n", value.c_str());
  }

  configFile.close();
}

String getApiKey() {
  // Return the API key from the config file
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return "";
  }

  // Deserialize JSON from file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if (error) {
    Serial.println("Failed to parse config file");
    configFile.close();
    return "";
  }

  String apiKey = doc["api_key"];
  configFile.close();
  return apiKey;
}

void setApiKey(const String& apiKey) {
  Serial.printf("Storing API key in config: %s\n", apiKey.c_str());

  // Read the existing config
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  // Deserialize JSON from file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file");
    return;
  }

  // Update API key
  doc["api_key"] = apiKey;

  // Write the updated config
  configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  // Serialize JSON to file
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write config file");
  } else {
    Serial.println("API key updated successfully");
  }

  configFile.close();
}

String getUprn() {
  // Return the UPRN from the config file
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return "";
  }

  // Deserialize JSON from file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if (error) {
    Serial.println("Failed to parse config file");
    configFile.close();
    return "";
  }

  String uprn = doc["uprn"];
  configFile.close();
  return uprn;
}

void setUprn(const String& uprn) {
  Serial.printf("Storing UPRN in config: %s\n", uprn.c_str());

  // Read the existing config
  File configFile = LittleFS.open(CONFIG_FILE, "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  // Deserialize JSON from file
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, configFile);
  configFile.close();

  if (error) {
    Serial.println("Failed to parse config file");
    return;
  }

  // Update UPRN
  doc["uprn"] = uprn;

  // Write the updated config
  configFile = LittleFS.open(CONFIG_FILE, "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  // Serialize JSON to file
  if (serializeJson(doc, configFile) == 0) {
    Serial.println("Failed to write config file");
  } else {
    Serial.println("UPRN updated successfully");
  }

  configFile.close();
}

String buildWebServiceURL() {
  // Get API key and UPRN from config
  String apiKey = getApiKey();
  String uprn = getUprn();

  if (apiKey.length() == 0) {
    Serial.println("Error: API key not configured");
    return "";
  }

  if (uprn.length() == 0) {
    Serial.println("Error: UPRN not configured");
    return "";
  }

  // Base URL with UPRN in the path
  String baseUrl = "http://bindicator.berwick.me.uk/schedule/";

  // Build the complete URL: baseUrl + uprn + "/?api_key=" + apiKey
  String url = baseUrl + uprn + "/?api_key=" + apiKey;

  return url;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Bin Schedule Display...");
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
  Serial.printf("Running in %s mode\n", OPERATING_MODE == PRODUCTION ? "Production" : "Development");

  display.init(115200, true, 50, false);
  display.setRotation(3); // enforce 180-degree rotation globally

  // Initialize configuration
  initializeConfig();

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
    if (OPERATING_MODE == DEVELOPMENT) {
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
  if (OPERATING_MODE == DEVELOPMENT) {
    // Handle Arduino OTA in development mode
    ArduinoOTA.handle();
    delay(1000);
  } else {
    // In production, we should never reach here due to deep sleep
    Serial.println("Unexpected loop execution in production mode, entering deep sleep...");
    enterDeepSleep();
  }
}
