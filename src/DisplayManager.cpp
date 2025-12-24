#include "DisplayManager.h"
#include <Fonts/FreeMonoBold9pt7b.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>

DisplayManager& DisplayManager::getInstance() {
  static DisplayManager instance;
  return instance;
}

DisplayManager::DisplayManager() {
  // Constructor - display created in begin()
}

void DisplayManager::begin() {
  Serial.println("DisplayManager: Initializing...");

  // Create display instance
  display = new GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>(
    GxEPD2_290_C90c(CS_PIN, DC_PIN, RES_PIN, BUSY_PIN)
  );

  // Initialize display hardware
  display->init(115200, true, 50, false);
  display->setRotation(3); // 180-degree rotation

  Serial.println("DisplayManager: Initialization complete");
}

void DisplayManager::showError(const char* errorMessage) {
  if (!display) return;

  display->setFullWindow();
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setFont(&FreeMonoBold9pt7b);
    display->setTextColor(GxEPD_BLACK);
    display->setCursor(10, 40);
    display->print("Error:");
    display->setCursor(10, 70);
    display->print(errorMessage);
    display->setCursor(10, 100);
    display->print("Check connection");
  } while (display->nextPage());
}

void DisplayManager::showUpdateStatus(const char* message) {
  if (!display) return;

  display->setFullWindow();
  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);
    display->setFont(&FreeMonoBold9pt7b);
    display->setTextColor(GxEPD_BLACK);
    display->setCursor(10, 40);
    display->print("Firmware Update");
    display->setCursor(10, 70);
    display->print(message);
  } while (display->nextPage());
}

void DisplayManager::showBinSchedule(const String& jsonData, const String& lastUpdate, const String& version, bool isProduction) {
  if (!display) return;

  display->setFullWindow();

  // Parse JSON with StaticJsonDocument<512> (API response ~400 bytes typical)
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  if (error) {
    Serial.printf("DisplayManager: JSON parse error: %s\n", error.c_str());
    showError("JSON Parse Error");
    return;
  }

  // Group bins by days_until
  std::map<int, std::vector<BinInfo>> binsByDays;

  for (JsonObject bin : doc.as<JsonArray>()) {
    BinInfo info;
    info.type = bin["type"].as<String>();
    info.binType = bin["bin"].as<String>();
    info.isNext = bin["next"];
    int daysUntil = bin["days_until"];

    binsByDays[daysUntil].push_back(info);
  }

  display->firstPage();
  do {
    display->fillScreen(GxEPD_WHITE);

    int yPos = 5;

    // Display bins grouped by days
    for (const auto& entry : binsByDays) {
      int daysUntil = entry.first;
      const std::vector<BinInfo>& bins = entry.second;

      // Check if we have space for at least the header and one bin
      if (yPos > display->height() - 30) break;

      // Determine if any bin in this group is marked as "next"
      bool groupIsNext = false;
      for (const auto& bin : bins) {
        if (bin.isNext) {
          groupIsNext = true;
          break;
        }
      }

      // Set spacing and fonts based on whether this is the next collection
      int lineHeight = groupIsNext ? 18 : 14;        // Larger spacing for 9pt font
      int headerSpacing = groupIsNext ? 18 : 5;      // Spacing after header underline
      int binDescSpacing = groupIsNext ? 3 : 8;      // Spacing between bin type and description
      int groupSpacing = groupIsNext ? 8 : 3;        // Less spacing after subsequent groups

      // Check if we should use inverted display (TODAY or TOMORROW for next collection)
      bool useInverted = groupIsNext && (daysUntil <= 1) && display->epd2.hasColor;

      // If inverted, calculate the height of the entire group and draw background rectangle
      int groupStartY = yPos;
      if (useInverted) {
        // Calculate total height needed for this group
        int headerHeight = 15 + 2 + headerSpacing;
        int binsHeight = bins.size() * (binDescSpacing + 8);
        if (bins.size() > 1) {
          binsHeight += (bins.size() - 1) * (lineHeight + binDescSpacing);
        }
        int totalHeight = headerHeight + binsHeight - 4;

        // Draw red filled rectangle as background
        display->fillRect(5, groupStartY, (int16_t)(display->width() - 10), totalHeight, GxEPD_RED);
      }

      // Display days header
      auto textColour = useInverted ? GxEPD_WHITE : (groupIsNext && display->epd2.hasColor ? GxEPD_RED : GxEPD_BLACK);
      auto headerFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      auto binFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      display->setFont(headerFont);
      display->setTextColor(textColour);

      // Adjust yPos before setting cursor to prevent text clashing
      if (groupIsNext) {
        yPos += 15;  // Space before first group header (custom font needs space above baseline)
      } else {
        yPos += 8;   // Space before subsequent group headers (default font, less needed)
      }

      display->setCursor(10, (int16_t)yPos);

      if (daysUntil == 0) {
        display->print("TODAY");
      } else if (daysUntil == 1) {
        display->print("TOMORROW");
      } else {
        display->print(String(daysUntil) + " DAYS");
      }

      // Adjust accordingly for underline placement
      if (groupIsNext) {
        yPos = yPos + 2;  // Custom font: small gap after baseline
      } else {
        yPos = yPos + 8 + 2;  // Default font: font height (8px) + small gap
      }

      display->drawLine(10, yPos, (int16_t)(display->width() - 10), yPos, textColour);

      yPos += headerSpacing;

      // Display all bins for this day
      for (size_t i = 0; i < bins.size(); i++) {
        const auto& bin = bins[i];

        // Check if we're running out of space
        if (yPos > display->height() - 20) break;

        display->setFont(binFont);

        display->setCursor(14, (int16_t)yPos);
        display->print(bin.type);
        yPos += binDescSpacing;

        // Display bin description (smaller font)
        display->setFont();
        display->setCursor(15, (int16_t)yPos);
        display->print(bin.binType);

        // Only add bin spacing if this is not the last bin in the group
        if (i < bins.size() - 1) {
          yPos += lineHeight + binDescSpacing;
        }
      }

      // Add spacing between day groups
      yPos += groupSpacing;
    }

    // Footer with last update info and version/mode
    display->setFont();
    display->setTextColor(GxEPD_BLACK);
    display->setCursor(10, (int16_t)(display->height() - 10));
    display->print("Updated: ");
    display->print(lastUpdate);
    display->print(" | ");
    if (isProduction) {
      display->print("Version: ");
      display->print(version);
    } else {
      display->print("Mode: DEV");
    }

  } while (display->nextPage());
}

void DisplayManager::hibernate() {
  if (display) {
    display->hibernate();
  }
}

int16_t DisplayManager::getWidth() const {
  return display ? display->width() : 0;
}

int16_t DisplayManager::getHeight() const {
  return display ? display->height() : 0;
}
