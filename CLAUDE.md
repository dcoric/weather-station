# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32 weather station displaying real-time weather data on a 2.8" ILI9341 TFT LCD (240x320) using the LVGL graphics library. The application fetches data from OpenWeatherMap API and displays current conditions plus a 3-day forecast with touchscreen-controlled backlight brightness.

**Target Hardware:** ESP32-2432S028 series with ILI9341 display and XPT2046 touch controller on HSPI bus.

## Build Commands

```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio run --target monitor

# Build, upload, and monitor in one command
pio run --target upload --target monitor

# Clean build files
pio run --target clean
```

**Important:** Update the upload port in [platformio.ini](platformio.ini) if your device appears on a different serial port (default: `/dev/cu.usbserial-2110`).

## Configuration

The weather station supports two configuration methods:

### Method 1: SD Card Configuration (Recommended)

1. Format a microSD card as FAT32 (32GB or less recommended)
2. Copy [conf.txt.example](conf.txt.example) to the root of your SD card and rename it to `conf.txt`
3. Edit `conf.txt` with your settings:
   - `wifi_ssid` / `wifi_password`: WiFi credentials (2.4GHz only)
   - `weather_api_key`: OpenWeatherMap API key (free tier at openweathermap.org/api)
   - `weather_city` / `weather_country_code`: Location for weather data
   - `weather_units`: "metric" for Celsius, "imperial" for Fahrenheit
   - `update_interval`: Weather refresh interval in milliseconds (default: 900000 = 15 minutes)
4. Insert the SD card into the ESP32-2432S028 slot
5. The configuration will be loaded automatically on boot

**Benefits:** No recompilation needed to change settings. Simply edit `conf.txt` on the SD card and restart the device.

### Method 2: Hardcoded Configuration (Fallback)

If no SD card is present or `conf.txt` is not found, the device will use values from [include/config.h](include/config.h):

1. Copy [include/config.h.example](include/config.h.example) to [include/config.h](include/config.h)
2. Edit the `#define` values with your settings
3. Recompile and upload the firmware

**Note:** [config.h](include/config.h) is gitignored to protect credentials. This method requires recompilation for any configuration changes.

## Architecture

### Main Application Flow

[src/main.cpp](src/main.cpp) contains the entire application as a single-file implementation:

1. **Initialization (`setup()`):**
   - Load configuration from SD card or use [config.h](include/config.h) defaults
   - Initialize LVGL graphics library with TFT_eSPI display driver
   - Initialize XPT2046 touchscreen on HSPI bus
   - Configure PWM backlight control (GPIO 21)
   - Create UI with LVGL widgets
   - Connect to WiFi
   - Fetch initial weather and forecast data

2. **Main Loop (`loop()`):**
   - Process LVGL timer events (5ms intervals)
   - Check for weather data refresh based on configured update interval

### Display & Graphics

- **LVGL Integration:** Uses a 240x10 line buffer for drawing. Display flushing handled by `my_disp_flush()` which interfaces with TFT_eSPI.
- **UI Structure:** Single screen with layered objects (no scrolling):
  - City name and current weather icon at top
  - Large temperature display
  - Weather description and humidity
  - 3-day forecast container at bottom with flex layout
  - Transparent full-screen touch layer for backlight control

### Touchscreen & Backlight

- **Touch Calibration:** Constants `TOUCH_RAW_MIN_*`, `TOUCH_RAW_MAX_*`, `TOUCH_SWAP_XY`, and `TOUCH_INVERT_*` in [main.cpp](src/main.cpp) map raw coordinates to screen pixels. Adjust if taps are misaligned.
- **Brightness Control:** Tapping anywhere cycles backlight through `BRIGHTNESS_LEVELS` array (33%, 60%, 100% PWM). Default is 60% on boot.

### Weather Data Fetching

Two HTTP endpoints called sequentially:

1. **Current Weather (`fetch_weather()`):** Fetches `/data/2.5/weather` API endpoint. Parses temperature, humidity, description, icon code, and timezone. Updates current weather UI.

2. **Forecast (`fetch_forecast()`):** Fetches `/data/2.5/forecast` API endpoint (40 data points over 5 days). Uses `DailyAccumulator` struct to:
   - Skip current day entries
   - Accumulate min/max temperatures per day
   - Select most representative icon (closest to noon)
   - Extract next 3 days' data only

Both functions return `bool` for success/failure and update status messages on errors.

### SD Card Configuration System

Configuration management is handled by [include/sd_config.h](include/sd_config.h):

- **Structure:** `AppConfig` struct holds all runtime configuration (WiFi, API keys, location, update interval)
- **Initialization:** `sd_config_init()` mounts the SD card using VSPI pins (CS=5, MOSI=23, MISO=19, SCK=18)
- **Loading:** `sd_config_load()` reads `/conf.txt` from SD card root directory. File format is simple key=value pairs, one per line. Comments start with `#` or `//`.
- **Fallback:** If SD card is unavailable or `conf.txt` is missing, defaults from [config.h](include/config.h) are used automatically
- **Parsing:** Line-based parser with string trimming, quote removal, and type conversion

**Benefits:** Change WiFi credentials, API keys, or location without recompiling firmware. Ideal for deploying to multiple devices or sharing the project.

### Icon Mapping

Weather icons are embedded in [include/weather_images.h](include/weather_images.h) (3.4MB file with 18 PNG images converted to C arrays). The `ICON_MAP` array maps OpenWeatherMap icon codes (e.g., "01d", "10n") to `lv_img_dsc_t` structures. Icons are scaled using `set_icon_size()` with LVGL zoom functions.

### Pin Configuration

All pins defined in [platformio.ini](platformio.ini) as build flags:

- **TFT Display (SPI):** MISO=12, MOSI=13, SCLK=14, CS=15, DC=2, RST=4, BL=21
- **Touchscreen (HSPI):** CS=33, MOSI=32, MISO=39, CLK=25, IRQ=36
- **SD Card (VSPI):** CS=5, MOSI=23, MISO=19, SCK=18

### LVGL Configuration

[include/lv_conf.h](include/lv_conf.h) defines:
- 16-bit color depth
- 48KB memory allocation
- Enabled Montserrat fonts (sizes 10-36)
- Custom tick using Arduino `millis()`
- PNG decoder enabled for weather icons

## Development Notes

- **WiFi**: ESP32 only supports 2.4GHz networks. Connection timeout is 20 seconds (40 attempts × 500ms).
- **API Rate Limits**: OpenWeatherMap free tier allows 60 calls/minute. Default 15-minute refresh interval stays well under limits.
- **Memory**: LVGL uses 48KB heap. Weather JSON responses use DynamicJsonDocument (2KB for current, 32KB for forecast).
- **Timezone Handling**: OpenWeatherMap provides timezone offset in API response. All timestamps are adjusted using this offset before display.
- **Temperature Units**: Controlled by `WEATHER_UNITS` in [config.h](include/config.h). Use "metric" for Celsius or "imperial" for Fahrenheit (update display format in [main.cpp](src/main.cpp) accordingly).

## Troubleshooting

- **Display blank:** Verify pin definitions match your hardware in [platformio.ini](platformio.ini)
- **WiFi fails:** Check SSID/password, ensure 2.4GHz network, monitor serial output for detailed errors
- **Touch misaligned:** Adjust calibration constants in [main.cpp](src/main.cpp)
- **API errors:** Verify API key is active, check city name format, monitor serial output
