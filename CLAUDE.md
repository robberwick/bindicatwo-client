# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Bindicator Client is an ESP8266-based e-paper display showing upcoming bin collection schedules for North Herts Council. The device fetches data from a web service and displays it on a 2.9" tri-color e-ink display with automatic firmware updates and deep sleep for low power consumption.

**Hardware:** ESP8266 (Wemos D1 Mini) + 2.9" E-Paper Display (GDEM029C90, 128x296, tri-color)

**Key Technologies:**
- Platform: ESP8266 (Arduino framework)
- Build System: PlatformIO
- Display: GxEPD2 library for e-paper control
- Configuration: LittleFS filesystem with JSON config
- Networking: WiFiManager for setup, HTTP OTA updates
- Deep Sleep: Power-saving mode with configurable intervals

## Build Commands

### Building and Uploading

```bash
# Build the project
pio run

# Upload firmware to device
pio run --target upload

# Upload filesystem (data/ folder containing config.json)
pio run --target uploadfs

# Monitor serial output (115200 baud)
pio run --target monitor

# Build and upload in one command
pio run --target upload --target monitor
```

### OTA Updates

```bash
# Arduino OTA upload (development mode only)
# Using hostname:
pio run --target upload --upload-port bindicator.local

# Using IP address (more reliable):
pio run --target upload --upload-port 192.168.1.100
```

**Note:** Arduino OTA is only available when `production_mode: false` in config.json and the device is awake (not in deep sleep).

## Configuration System

### LittleFS Configuration File

The project uses `data/config.json` stored on the ESP8266's LittleFS filesystem. This file is the source of truth for all runtime configuration.

**Important:** Always upload the filesystem (`pio run --target uploadfs`) before uploading firmware, especially on first boot.

### Configuration Structure

```json
{
  "api_key": "<YOUR_API_KEY>",
  "uprn": "<YOUR_UPRN>",
  "firmware_version": "0.0.0",
  "production_mode": true,
  "last_update": "24-12 14:23 GMT"
}
```

**Fields:**
- `api_key`: API key for bin collection web service
- `uprn`: Unique Property Reference Number for address lookup
- `firmware_version`: Current firmware version (auto-updated by OTA)
- `production_mode`: `true` for production (3h sleep), `false` for development (20s sleep)
- `last_update`: Timestamp of last data fetch (auto-updated, format: `DD-MM HH:MM GMT`)

### Production vs Development Mode

Toggle via `production_mode` in config.json:

| Feature | Development (`false`) | Production (`true`) |
|---------|----------------------|-------------------|
| Deep Sleep Interval | 20 seconds | 3 hours |
| Firmware Update Check | Every wake | Every 10th wake |
| Arduino OTA | Enabled | Disabled |
| Display Footer | "Mode: DEV" | "Version: X.X.X" |

**Default:** Production mode if field missing or unreadable.

## Code Architecture

### Main Execution Flow

The device operates in a deep sleep cycle with two execution paths:

1. **Fresh Boot** (`setup()` when not waking from deep sleep):
   - Initialize LittleFS and read config.json
   - Connect to WiFi (or start WiFiManager portal)
   - Sync NTP time for accurate timestamps
   - Enable Arduino OTA (development mode only)
   - Check for firmware updates and perform OTA if needed
   - Fetch bin collection data from web service
   - Display schedule on e-paper
   - Enter deep sleep

2. **Wake from Deep Sleep** (`setup()` when `REASON_DEEP_SLEEP_AWAKE`):
   - Initialize config from LittleFS
   - Reconnect WiFi (faster, uses saved credentials)
   - Re-sync NTP time
   - Check for firmware updates (every 10th wake in production)
   - Fetch and display updated schedule
   - Return to deep sleep

**Important:** The `loop()` function is never reached in normal operation due to deep sleep. It only runs briefly in development mode to handle Arduino OTA requests.

### Key Components

**Configuration Management** (src/main.cpp:624-976):
- `initializeConfig()`: Mount LittleFS, create/read config.json
- `getCurrentFirmwareVersion()`: Read version from config (source of truth)
- `setCurrentFirmwareVersion()`: Update version after successful OTA
- `getLastUpdateString()/setLastUpdateString()`: Track last data fetch time
- `getApiKey()/setApiKey()`, `getUprn()/setUprn()`: Configuration accessors
- `buildWebServiceURL()`: Construct API endpoint from config values

**WiFi Management** (src/main.cpp:89-110, 514-546):
- `setupWiFi()`: Full WiFiManager setup with captive portal (AP: "BinScheduleAP", password: "binschedule")
- `reconnectWiFi()`: Fast reconnect using saved credentials, fallback to full setup

**Firmware Updates** (src/main.cpp:185-346):
- `checkForFirmwareUpdate()`: Compare config.json version with server version
- `performOTAUpdate()`: Download and install new firmware, update config.json before restart
- `setupArduinoOTA()`: Enable Arduino OTA for development (hostname: "bindicator", password: "binschedule")
- Update URLs:
  - Version check: `http://bindicator.berwick.me.uk/firmware/version.txt`
  - Firmware download: `http://bindicator.berwick.me.uk/firmware/bindicator.bin`

**Version Management:**
- `FIRMWARE_VERSION` define (line 22) is a compile-time constant but NOT used for comparison
- config.json `firmware_version` field is the source of truth
- Version "0.0.0" is a sentinel value triggering automatic update
- OTA updates config.json with new version BEFORE restart to prevent update loops

**Display Management** (src/main.cpp:348-512):
- `displayBinSchedule()`: Main display logic
  - Fetches data from web service
  - Parses JSON array of bin collections
  - Groups bins by `days_until` field
  - Renders inverted display (red background, white text) for today/tomorrow collections
  - Uses FreeMonoBold9pt7b for next collection, default font for future collections
  - Shows last update time and mode/version in footer
- `displayError()`: Show error messages on display
- `displayUpdateStatus()`: Show OTA progress messages

**Data Flow:**
1. `fetchDataFromWebService()`: HTTP GET to `http://bindicator.berwick.me.uk/schedule/{uprn}/?api_key={key}`
2. Parse JSON array with structure:
   ```json
   [
     {
       "type": "Recycling",
       "bin": "Blue bin - Paper, card, cans, plastic bottles",
       "next": true,
       "days_until": 0
     }
   ]
   ```
3. Group by `days_until`, render with visual hierarchy (inverted for urgent, bold for next)

**Display Rendering Logic:**
- Groups bins by days until collection
- Inverted display (red background) for today/tomorrow if marked as "next"
- Bold 9pt font for next collection, default font for future collections
- Spacing adjusts based on font size and urgency
- Text color: white for inverted, red/black for normal

**Deep Sleep** (src/main.cpp:608-621):
- `enterDeepSleep()`: Put ESP8266 in deep sleep
- **Hardware requirement:** D0 must be connected to RST to enable wake-up
- Sleep intervals defined as constants (lines 38-39)

**Time Management** (src/main.cpp:548-577):
- `syncNTP()`: Sync time from NTP servers (pool.ntp.org, time.nist.gov, time.google.com)
- Timezone: GMT (UTC)
- `formatUpdateTime()`: Format time as "DD-MM HH:MM GMT"
- Re-syncs on every wake for accuracy

### Hardware Pin Configuration (src/main.cpp:41-58)

```cpp
#define CS_PIN (15)    // D8 (Blue)
#define BUSY_PIN (12)  // D6 (Purple)
#define RES_PIN (5)    // D1 (Orange)
#define DC_PIN (4)     // D2 (White)
// SCK: D5/GPIO14 (Green)
// MOSI: D7/GPIO13 (Yellow)
// GND: Black
// VCC: Red (3.3V)
```

**Deep Sleep Wake:** D0 must be jumpered to RST.

### Display Configuration (src/main.cpp:61)

```cpp
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(...)
```
- Model: GDEM029C90
- Resolution: 128x296
- Colors: Black, White, Red
- Rotation: 3 (180-degree rotation enforced in setup)

## Development Workflow

### Initial Setup

1. Edit `data/config.json` with your API key and UPRN
2. Set `"firmware_version": "0.0.0"` to trigger automatic update on first boot
3. Set `"production_mode": false` for development
4. Upload filesystem: `pio run --target uploadfs`
5. Upload firmware: `pio run --target upload`
6. Monitor serial: `pio run --target monitor` (115200 baud)

### Iterative Development

In development mode (20-second wake cycles):
1. Wait for device to wake from sleep
2. Use Arduino OTA for fast updates: `pio run --target upload --upload-port bindicator.local`
3. Monitor serial output for debugging
4. Device will wake every 20 seconds for testing

### Making Code Changes

**When modifying firmware version handling:**
- Remember config.json is the source of truth, not the `FIRMWARE_VERSION` define
- `getCurrentFirmwareVersion()` reads from config.json
- `setCurrentFirmwareVersion()` updates config.json
- Never use `FIRMWARE_VERSION` define for version comparison

**When modifying configuration:**
- All config changes must go through LittleFS read/write functions
- Always check `LittleFS.begin()` return value
- Handle JSON deserialization errors gracefully
- Config persists across firmware updates but not filesystem uploads

**When modifying display code:**
- Display rotation is set globally in setup() (line 985)
- Use `display.setFullWindow()` before rendering
- Call `display.hibernate()` after rendering to save power
- Inverted display requires tri-color support check: `display.epd2.hasColor`

**When modifying deep sleep:**
- Ensure D0-RST jumper is documented if changing wake mechanism
- Update both DEVELOPMENT_SLEEP_INTERVAL and PRODUCTION_SLEEP_INTERVAL if needed
- Test deep sleep wake in both modes

### Testing OTA Updates

1. Set `"firmware_version": "0.0.0"` in config.json
2. Upload filesystem
3. Device will detect version mismatch and download latest firmware
4. Verify config.json is updated with new version after restart

### Switching Between Modes

1. Edit `data/config.json`
2. Change `"production_mode"` value
3. Re-upload filesystem: `pio run --target uploadfs`
4. Restart device

## Important Implementation Details

### Version Management Gotcha

The `FIRMWARE_VERSION` define (line 22) exists for logging but is NOT used for update decisions. The config.json `firmware_version` field is the only source of truth. This design allows version tracking to persist across firmware updates.

### Deep Sleep Wake Detection

The device differentiates between fresh boot and deep sleep wake using `ESP.getResetInfoPtr()->reason == REASON_DEEP_SLEEP_AWAKE` (line 992). This enables faster reconnection on wake (WiFi credentials cached) vs. full setup on fresh boot.

### Config Persistence

LittleFS filesystem persists across firmware OTA updates but is ERASED when uploading filesystem via `uploadfs`. Always back up config.json before filesystem uploads.

### Display Rendering Strategy

The display groups bins by `days_until` and applies visual hierarchy:
1. Inverted (red background) for today/tomorrow collections marked as "next"
2. Bold font for next collection
3. Default font for future collections
4. Dynamic spacing based on urgency

This creates clear visual priority on the e-paper display.

### WiFiManager Behavior

WiFiManager (lines 89-110) only shows the captive portal if:
1. No saved WiFi credentials exist, OR
2. Connection to saved network fails

Once credentials are saved to ESP8266 flash, the device will reconnect automatically on subsequent boots.

## Web Service Integration

**Endpoint:** `http://bindicator.berwick.me.uk/schedule/{uprn}/?api_key={api_key}`

**Expected Response:** JSON array of bin collections:
```json
[
  {
    "type": "Recycling",
    "bin": "Blue bin - Paper, card, cans, plastic bottles",
    "next": true,
    "days_until": 0
  }
]
```

**Fields:**
- `type`: Collection type (e.g., "Recycling", "Refuse")
- `bin`: Human-readable description
- `next`: Boolean indicating if this is the next collection
- `days_until`: Days until collection (0 = today)

The client groups by `days_until` and uses `next` flag for visual emphasis.
