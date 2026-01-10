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

String DisplayManager::truncateText(const String& text, int maxWidth, const GFXfont* font) {
  if (!display) return text;

  // Set the font for measurement
  display->setFont(font);

  // Measure full text
  int16_t x1, y1;
  uint16_t w, h;
  display->getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  // If it fits, return as-is
  if (w <= maxWidth) {
    return text;
  }

  // Text is too long - truncate with ellipsis
  String ellipsis = "...";

  // Binary search for the right length
  int left = 0;
  int right = text.length();
  String result = text;

  while (left < right) {
    int mid = (left + right + 1) / 2;
    String truncated = text.substring(0, mid) + ellipsis;

    display->getTextBounds(truncated, 0, 0, &x1, &y1, &w, &h);

    if (w <= maxWidth) {
      result = truncated;
      left = mid;
    } else {
      right = mid - 1;
    }
  }

  return result;
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

      // Determine fonts for this group
      auto headerFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;
      auto binFont = groupIsNext ? &FreeMonoBold9pt7b : nullptr;

      // Check if we should use inverted display (TODAY or TOMORROW for next collection)
      bool useInverted = groupIsNext && (daysUntil <= 1) && display->epd2.hasColor;

      // If inverted, calculate the height of the entire group and draw background rectangle
      int groupStartY = yPos;
      if (useInverted) {
        // Calculate ACTUAL height by measuring text with getTextBounds()
        int16_t x1, y1;
        uint16_t w, h;

        // Measure header text
        display->setFont(groupIsNext ? &FreeMonoBold9pt7b : nullptr);
        String headerText = (daysUntil == 0) ? "TODAY" : (daysUntil == 1) ? "TOMORROW" : String(daysUntil) + " DAYS";
        display->getTextBounds(headerText, 0, 0, &x1, &y1, &w, &h);
        int headerTextHeight = h;

        // Calculate total height with proper spacing
        int totalHeight = 0;
        totalHeight += (groupIsNext ? 15 : 8);  // Space before header
        totalHeight += headerTextHeight;         // Header text height
        totalHeight += 2;                        // Gap before underline
        totalHeight += 2;                        // Underline thickness
        totalHeight += headerSpacing;            // Space after header

        // Measure each bin's text
        for (size_t i = 0; i < bins.size(); i++) {
          // Measure bin type (bold font)
          display->setFont(binFont);
          display->getTextBounds(bins[i].type, 0, 0, &x1, &y1, &w, &h);
          totalHeight += h;
          totalHeight += binDescSpacing;

          // Measure bin description (default font)
          display->setFont();
          display->getTextBounds(bins[i].binType, 0, 0, &x1, &y1, &w, &h);
          totalHeight += h;

          // Add spacing between bins (except after last bin)
          if (i < bins.size() - 1) {
            totalHeight += lineHeight + binDescSpacing;
          }
        }

        totalHeight += groupSpacing;  // Space after group

        // Draw red filled rectangle with calculated height
        display->fillRect(5, groupStartY, (int16_t)(display->width() - 10), totalHeight, GxEPD_RED);
      }

      // Display days header
      auto textColour = useInverted ? GxEPD_WHITE : (groupIsNext && display->epd2.hasColor ? GxEPD_RED : GxEPD_BLACK);
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

        // Calculate space needed for this bin
        int16_t x1, y1;
        uint16_t w, h;
        display->setFont(binFont);
        display->getTextBounds(bin.type, 0, 0, &x1, &y1, &w, &h);
        int binTypeHeight = h;

        display->setFont();
        display->getTextBounds(bin.binType, 0, 0, &x1, &y1, &w, &h);
        int binDescHeight = h;

        int totalBinHeight = binTypeHeight + binDescSpacing + binDescHeight;
        if (i < bins.size() - 1) {
          totalBinHeight += lineHeight + binDescSpacing;
        }

        // Check if we have space (leave room for footer)
        if (yPos + totalBinHeight > display->height() - 25) {
          // Out of space - show truncation indicator
          display->setFont();
          display->setTextColor(textColour);
          display->setCursor(14, (int16_t)yPos);
          display->print("...more");
          break;
        }

        // Render bin type (truncate if needed)
        display->setFont(binFont);
        display->setTextColor(textColour);  // Ensure text color is set
        int maxTextWidth = display->width() - 20;  // Leave margins
        String truncatedType = truncateText(bin.type, maxTextWidth, binFont);

        display->setCursor(14, (int16_t)yPos);
        display->print(truncatedType);

        // Increment yPos by actual text height to match calculation
        yPos += binTypeHeight;
        yPos += binDescSpacing;

        // Display bin description (truncate if needed)
        display->setFont();
        display->setTextColor(textColour);  // Ensure text color is set
        String truncatedDesc = truncateText(bin.binType, maxTextWidth, nullptr);

        display->setCursor(15, (int16_t)yPos);
        display->print(truncatedDesc);

        // Increment yPos by actual text height to match calculation
        yPos += binDescHeight;

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

  // Debug: Dump framebuffer to serial in development mode
  #ifdef DEBUG_DISPLAY_DUMP
  if (!isProduction) {
    dumpFramebufferToSerial();
  }
  #endif
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

void DisplayManager::dumpFramebufferToSerial() {
  if (!display) {
    Serial.println("ERROR: Display not initialized");
    return;
  }

  // NOTE: GxEPD2 doesn't expose framebuffers directly in its public API
  // As a workaround, we'll render a debug grid with coordinates

  Serial.println("\n========== DISPLAY DEBUG INFO ==========");
  Serial.printf("Display: %dx%d pixels\n", display->width(), display->height());
  Serial.printf("Color support: %s\n", display->epd2.hasColor ? "YES (tri-color)" : "NO (mono)");
  Serial.println("==========================================");
  Serial.println("NOTE: Direct framebuffer access not available in GxEPD2 public API");
  Serial.println("To view layout:");
  Serial.println("  1. Take photo of physical display");
  Serial.println("  2. Or use screenshot capability if added to GxEPD2");
  Serial.println("  3. Or implement custom buffer tracking");
  Serial.println("==========================================\n");

  // TODO: Implement custom buffer tracking by intercepting draw calls
  // This would require wrapping GxEPD2 methods to capture drawing operations
}
