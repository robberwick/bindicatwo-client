# Bindicator Client

An ESP32-S3-based e-paper display for showing upcoming [North Herts Council](https://www.north-herts.gov.uk/find-your-bin-collection-day) bin collection schedules.
The device connects to a web service to fetch bin collection data and displays it on a 2.9" tri-color e-ink display, with automatic firmware updates and deep sleep for low power consumption.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)
![PlatformIO](https://img.shields.io/badge/PlatformIO-compatible-orange.svg)

## Features

- **E-Paper Display**: Shows bin collection schedule on a 2.9" tri-color (black/white/red) e-ink display
- **WiFi Configuration**: Easy WiFi setup using WiFiManager captive portal
- **Low Power**: Deep sleep mode with configurable intervals (3 hours production, 20 seconds development)
- **Automatic Updates**: OTA firmware updates via HTTP with version tracking
- **LittleFS Configuration**: JSON-based configuration stored on filesystem
- **NTP Time Sync**: Automatic time synchronization for accurate update timestamps
- **Production/Development Modes**: Runtime toggle between modes via config.json
- **Persistent State**: Last update time and firmware version tracked in config.json

## Hardware Requirements

- **ESP32-S3 Board** (Wemos S3 Mini or similar)
- **2.9" E-Paper Display Module** (GDEM029C90, 128x296, tri-color)
- **Power Supply** (USB or battery)

### Pin Connections

| E-Paper Pin | Color (Harness) | ESP32-S3 Pin | GPIO |
|-------------|-----------------|--------------|------|
| BUSY        | Purple          | GPIO13       | 13   |
| RST         | Orange          | GPIO12       | 12   |
| DC          | White           | GPIO11       | 11   |
| CS          | Blue            | GPIO10       | 10   |
| SCL/SCK     | Green           | GPIO7        | 7    |
| SDA/MOSI    | Yellow          | GPIO6        | 6    |
| GND         | Black           | GND          | -    |
| VCC         | Red             | 3.3V         | -    |

**Note**: ESP32-S3 uses internal timer wake for deep sleep (no external jumper required). The GPIOs are carefully selected to avoid strapping pins and USB conflicts.

## Software Requirements

- [PlatformIO](https://platformio.org/) (recommended) or Arduino IDE
- USB drivers for your ESP32-S3 board (typically USB-C native, no external driver needed)

## Quick Start

### 1. Prepare Configuration File

Edit `data/config.json` with your settings:

```json
{
  "api_key": "your_actual_api_key_here",
  "search": "your address here",
  "firmware_version": "0.0.0",
  "production_mode": false
}
```

**Configuration Fields:**
- `api_key`: Your API key for the bin collection service
- `search`: Your address for bin collection lookup
- `firmware_version`: Set to `"0.0.0"` for first boot (triggers automatic firmware update)
- `production_mode`: `false` for development (20s sleep), `true` for production (3h sleep)

### 2. Upload Filesystem to ESP32-S3

Using PlatformIO:
```bash
pio run --target uploadfs
```

This uploads the `data/` folder (containing `config.json`) to the ESP32-S3's LittleFS filesystem.

**Important**:
- The filesystem must be uploaded before the firmware, as the device reads configuration on boot.
- For ESP32-S3, ensure the device is in download mode by holding the BOOT button while pressing RESET when connecting via USB.

### 3. Upload Firmware

Using PlatformIO:
```bash
pio run --target upload
```

### 4. First Boot

On first boot, the device will:

1. **Initialize filesystem** and read `config.json`
2. **Detect firmware version** `"0.0.0"` (sentinel value)
3. **Create WiFi access point** named `BinScheduleAP` (password: `binschedule`)
4. **Wait for WiFi configuration**:
   - Connect to the access point from your phone/computer
   - A captive portal will open automatically
   - Select your WiFi network and enter the password
   - The device will save credentials and connect
5. **Sync time via NTP** for accurate timestamps
6. **Check for firmware updates**:
   - Compares `"0.0.0"` with server version
   - Automatically downloads and installs latest firmware
   - Updates `firmware_version` in config.json
   - Restarts with new firmware
7. **Fetch bin collection data** from the web service
8. **Save last update time** to config.json
9. **Display the schedule** on the e-paper display
10. **Enter deep sleep** (interval depends on `production_mode` setting)

### Important Notes

- **Firmware version `"0.0.0"` is a sentinel value** that ensures the device updates to the latest firmware on first boot
- The device will only attempt WiFi configuration if credentials aren't already saved
- WiFi credentials persist in ESP32-S3 flash memory (separate from LittleFS)
- If no `config.json` exists on the filesystem, the device creates a default one with placeholder values

## Configuration

### LittleFS Configuration

The project uses LittleFS filesystem to store configuration in JSON format. Key benefits:

✅ No hardcoded credentials in source code  
✅ Human-readable JSON configuration  
✅ Easy to edit and version control  
✅ Configuration persists across firmware updates  
✅ Runtime state tracking (firmware version, last update time)  

### Configuration File Structure

After the device runs, `config.json` will be automatically updated with additional fields:

```json
{
  "api_key": "your_actual_api_key_here",
  "search": "your_address_here",
  "firmware_version": "0.0.1",
  "production_mode": false,
  "last_update": "last_update_time_here"
}
```

**Runtime Fields (automatically managed):**
- `firmware_version`: Updated after successful OTA updates
- `last_update`: Timestamp of last successful data fetch (format: `DD-MM HH:MM GMT`)

### Production vs Development Mode

Toggle modes by editing `production_mode` in `config.json`:

```json
{
  "production_mode": false  // false for development, true for production
}
```

**Mode Comparison:**

| Feature | Development Mode | Production Mode |
|---------|-----------------|-----------------|
| Deep Sleep Interval | 20 seconds | 3 hours |
| Firmware Update Check | Every wake | Every 10th wake |
| Arduino OTA | Enabled | Disabled |
| Display Footer | "Mode: DEV" | "Version: X.X.X" |

**To switch modes:**
1. Edit `config.json` in the `data/` folder
2. Re-upload filesystem: `pio run --target uploadfs`
3. Restart the device

**Default Mode**: If `production_mode` is missing or cannot be read, the device defaults to **Production mode** for safety.

### Firmware Version Tracking

The device uses config.json as the source of truth for firmware version:

- **On first boot**: Set to `"0.0.0"` (sentinel value) to trigger automatic update
- **After OTA update**: Automatically updated to the new version
- **Version comparison**: Device checks server version against config.json version
- **Update trigger**: Downloads and installs firmware when versions differ

**Sentinel Value Behavior:**
- Version `"0.0.0"` indicates "needs update"
- If config.json is missing, corrupted, or version field is absent, defaults to `"0.0.0"`
- Ensures device always gets latest firmware even after filesystem issues

### Last Update Tracking

The device automatically tracks when it last successfully fetched data:

- **Initial state**: Shows "Updated: never" on display
- **After first fetch**: Stores timestamp in config.json
- **Format**: `DD-MM HH:MM GMT` (e.g., "19-10 14:23 GMT")
- **Persistence**: Survives deep sleep cycles and reboots
- **Display location**: Footer of the e-paper display

## Verifying Configuration

Open the serial monitor at 115200 baud. You should see output similar to the following:

```
Starting Bin Schedule Display...
Firmware version: 0.0.1
Running in Development mode
Initializing configuration...
Config file found, reading config
API Key: your_actual_api_key_here
Search: your_address_here
Firmware Version: 0.0.1
Operating mode: Development
```

## OTA Updates

The device supports two types of over-the-air firmware updates:

### 1. HTTP-based OTA (Both Modes)

The device automatically checks for and installs firmware updates from a remote HTTP server.

**How It Works:**

1. **Version Check**: Device reads its current firmware version from `config.json`
2. **Server Query**: Fetches the latest version number from the remote server
3. **Comparison**: Compares current version with server version using simple string matching
4. **Download**: If versions differ, downloads the new firmware binary
5. **Installation**: Installs the new firmware to flash memory
6. **Version Update**: Updates `firmware_version` in `config.json` before restarting
7. **Restart**: Device automatically reboots with the new firmware

**Update Frequency:**

- **Development Mode**: Checks for updates on **every wake cycle** (every 20 seconds)
- **Production Mode**: Checks for updates **every 10th wake cycle** (~30 hours with 3-hour sleep)

**First Boot Behavior:**

When `firmware_version` is set to `"0.0.0"` (the sentinel value), the device:
- Immediately detects a version mismatch
- Downloads and installs the latest firmware from the server
- Updates `config.json` with the new version
- Restarts automatically

This ensures fresh devices always get the latest firmware on first boot.

**Update URLs:**

The device connects to these endpoints (currently hardcoded in firmware):
- **Version check**: `http://bindicator.berwick.me.uk/firmware/version.txt`
- **Firmware download**: `http://bindicator.berwick.me.uk/firmware/bindicator.bin`

**Monitoring Updates:**

Watch the serial monitor during an update:
```
Checking for firmware updates...
Current version: 0.0.0
Latest version: 0.0.1
New firmware version available!
Getting target version to store in config.json...
Target version for update: 0.0.1
Starting HTTP OTA update...
OTA Update started
OTA Progress: 25%
OTA Progress: 50%
OTA Progress: 75%
OTA Progress: 100%
OTA Update finished successfully
Updating config.json with new version: 0.0.1
Firmware version updated to 0.0.1 in config.json
Config.json updated successfully before restart
OTA: update successful, restarting...
```

**Update Display:**

During an HTTP OTA update, the e-paper display shows:
- "Downloading..." - While fetching firmware
- "Installing..." - During firmware installation
- "Update complete!" - When finished
- "Restarting..." - Before automatic reboot

**Important Notes:**

- Updates require ~300KB of free flash space
- The device must have WiFi connectivity
- Failed updates do not brick the device - it continues with current firmware
- Version in `config.json` is only updated after successful installation
- Updates preserve the LittleFS filesystem (config.json persists)

### 2. Arduino OTA (Development Mode Only)

When `production_mode: false`, the device enables Arduino OTA for rapid development updates.

**Connection Details:**
- **Hostname**: `bindicator` (resolves as `bindicator.local` on most networks)
- **Password**: `binschedule`
- **IP Address**: Shown in serial monitor on boot
- **Port**: 8266 (default)

**Usage:**

Upload firmware directly from PlatformIO:
```bash
# Using hostname (mDNS)
pio run --target upload --upload-port bindicator.local

# Using IP address (more reliable)
pio run --target upload --upload-port 192.168.1.100
```

**When to Use Arduino OTA:**

✅ **Good for:**
- Quick code iterations during development
- Testing small changes without USB cable
- Remote debugging in development mode
- Updating devices that are physically inaccessible

❌ **Not available in:**
- Production mode (`production_mode: true`)
- When device is in deep sleep (must be awake)

**Arduino OTA Display:**

During an Arduino OTA upload, the display shows:
- "OTA Upload..." - Receiving firmware
- "OTA Complete!" - Upload finished

**Security:**

Arduino OTA requires the password `binschedule` to prevent unauthorized updates. Change this in the source code if needed for additional security.

### Update Troubleshooting

**HTTP OTA Updates Not Working:**

1. Check serial monitor for error messages
2. Verify firmware URLs are accessible from the device's network
3. Ensure server has valid `version.txt` and `bindicator.bin` files
4. Confirm device has sufficient free space (~300KB)
5. Check WiFi connectivity is stable during update

**Arduino OTA Not Connecting:**

1. Verify device is in development mode (`production_mode: false`)
2. Ensure device is not in deep sleep (wait for wake cycle)
3. Try using IP address instead of hostname
4. Check that device and computer are on same network
5. Verify password is correct (`binschedule`)

**Version Not Updating in config.json:**
1. 
2. Check that LittleFS is mounted successfully (serial monitor)
2. Verify config.json is not corrupted (re-upload filesystem if needed)
3. Ensure update completes successfully before device restarts
4. Check filesystem has write permissions

**Forcing a Firmware Update:**

To force the device to re-download firmware:
1. Edit `data/config.json` and set `"firmware_version": "0.0.0"`
2. Upload filesystem: `pio run --target uploadfs`
3. Reset the device
4. Device will detect mismatch and download latest firmware
