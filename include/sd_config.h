#ifndef SD_CONFIG_H
#define SD_CONFIG_H

#include <Arduino.h>
#include "FS.h"
#include "SD.h"
#include "config.h"

// Configuration structure to hold all settings
struct AppConfig {
    // WiFi settings
    char wifi_ssid[64];
    char wifi_password[64];

    // OpenWeatherMap API settings
    char weather_api_key[64];
    char weather_city[64];
    char weather_country_code[8];
    char weather_units[16];

    // Update interval
    unsigned long update_interval;
};

// Global configuration instance
extern AppConfig appConfig;

// Function declarations
bool sd_config_init();
bool sd_config_load();
void sd_config_set_defaults();
String sd_config_trim(String str);

// SD card initialization
bool sd_config_init() {
    Serial.println("Initializing SD card...");

    // Use custom pins for SD card
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

    if (!SD.begin(SD_CS)) {
        Serial.println("SD card initialization failed!");
        Serial.println("Using default config.h values");
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("No SD card attached!");
        Serial.println("Using default config.h values");
        return false;
    }

    Serial.print("SD Card Type: ");
    if (cardType == CARD_MMC) {
        Serial.println("MMC");
    } else if (cardType == CARD_SD) {
        Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    return true;
}

// Trim whitespace from string
String sd_config_trim(String str) {
    str.trim();
    // Remove quotes if present
    if (str.startsWith("\"") && str.endsWith("\"")) {
        str = str.substring(1, str.length() - 1);
    }
    return str;
}

// Set default configuration values from config.h
void sd_config_set_defaults() {
    strncpy(appConfig.wifi_ssid, WIFI_SSID, sizeof(appConfig.wifi_ssid) - 1);
    strncpy(appConfig.wifi_password, WIFI_PASSWORD, sizeof(appConfig.wifi_password) - 1);
    strncpy(appConfig.weather_api_key, WEATHER_API_KEY, sizeof(appConfig.weather_api_key) - 1);
    strncpy(appConfig.weather_city, WEATHER_CITY, sizeof(appConfig.weather_city) - 1);
    strncpy(appConfig.weather_country_code, WEATHER_COUNTRY_CODE, sizeof(appConfig.weather_country_code) - 1);
    strncpy(appConfig.weather_units, WEATHER_UNITS, sizeof(appConfig.weather_units) - 1);
    appConfig.update_interval = UPDATE_INTERVAL;
}

// Load configuration from SD card
bool sd_config_load() {
    // First set defaults from config.h
    sd_config_set_defaults();

    // Try to initialize SD card
    if (!sd_config_init()) {
        Serial.println("SD card not available, using config.h defaults");
        return false;
    }

    // Check if conf.txt exists
    if (!SD.exists("/conf.txt")) {
        Serial.println("conf.txt not found on SD card");
        Serial.println("\nListing files in root directory:");
        File root = SD.open("/");
        if (root) {
            File file = root.openNextFile();
            int fileCount = 0;
            while (file) {
                if (!file.isDirectory()) {
                    Serial.printf("  - %s (%lu bytes)\n", file.name(), (unsigned long)file.size());
                    fileCount++;
                }
                file = root.openNextFile();
            }
            root.close();
            if (fileCount == 0) {
                Serial.println("  (SD card root is empty)");
            }
        }
        Serial.println("\nUsing default config.h values");
        return false;
    }

    // Open and read conf.txt
    File configFile = SD.open("/conf.txt");
    if (!configFile) {
        Serial.println("Failed to open conf.txt");
        Serial.println("Using default config.h values");
        return false;
    }

    Serial.println("Reading configuration from SD card...");
    Serial.println("=========================================");

    int linesRead = 0;
    int settingsFound = 0;

    // Parse the configuration file
    while (configFile.available()) {
        String line = configFile.readStringUntil('\n');
        linesRead++;

        // Debug: show raw line
        Serial.printf("Line %d (raw): [%s]\n", linesRead, line.c_str());

        line.trim();

        // Skip empty lines and comments
        if (line.length() == 0 || line.startsWith("#") || line.startsWith("//")) {
            Serial.printf("  -> Skipped (empty or comment)\n");
            continue;
        }

        // Parse key=value pairs
        int equalPos = line.indexOf('=');
        if (equalPos > 0) {
            String key = line.substring(0, equalPos);
            String value = line.substring(equalPos + 1);

            key.trim();
            value = sd_config_trim(value);

            Serial.printf("  -> Parsed: key='%s', value='%s'\n", key.c_str(), value.c_str());

            // Map keys to config structure
            if (key == "wifi_ssid") {
                strncpy(appConfig.wifi_ssid, value.c_str(), sizeof(appConfig.wifi_ssid) - 1);
                Serial.printf("  ✓ wifi_ssid set to: %s\n", appConfig.wifi_ssid);
                settingsFound++;
            }
            else if (key == "wifi_password") {
                strncpy(appConfig.wifi_password, value.c_str(), sizeof(appConfig.wifi_password) - 1);
                Serial.println("  ✓ wifi_password: ******** (hidden)");
                settingsFound++;
            }
            else if (key == "weather_api_key") {
                strncpy(appConfig.weather_api_key, value.c_str(), sizeof(appConfig.weather_api_key) - 1);
                Serial.println("  ✓ weather_api_key: ******** (hidden)");
                settingsFound++;
            }
            else if (key == "weather_city") {
                strncpy(appConfig.weather_city, value.c_str(), sizeof(appConfig.weather_city) - 1);
                Serial.printf("  ✓ weather_city set to: %s\n", appConfig.weather_city);
                settingsFound++;
            }
            else if (key == "weather_country_code") {
                strncpy(appConfig.weather_country_code, value.c_str(), sizeof(appConfig.weather_country_code) - 1);
                Serial.printf("  ✓ weather_country_code set to: %s\n", appConfig.weather_country_code);
                settingsFound++;
            }
            else if (key == "weather_units") {
                strncpy(appConfig.weather_units, value.c_str(), sizeof(appConfig.weather_units) - 1);
                Serial.printf("  ✓ weather_units set to: %s\n", appConfig.weather_units);
                settingsFound++;
            }
            else if (key == "update_interval") {
                appConfig.update_interval = value.toInt();
                Serial.printf("  ✓ update_interval set to: %lu ms\n", appConfig.update_interval);
                settingsFound++;
            }
            else {
                Serial.printf("  ✗ Unknown key: %s\n", key.c_str());
            }
        } else {
            Serial.printf("  ✗ Invalid line (no '=' found)\n");
        }
    }

    configFile.close();

    Serial.println("=========================================");
    Serial.printf("Configuration loaded from SD card!\n");
    Serial.printf("Lines read: %d, Settings found: %d\n", linesRead, settingsFound);
    Serial.println("=========================================");

    // Print final configuration summary
    Serial.println("\nFinal Configuration:");
    Serial.printf("  WiFi SSID: %s\n", appConfig.wifi_ssid);
    Serial.printf("  Weather City: %s\n", appConfig.weather_city);
    Serial.printf("  Country Code: %s\n", appConfig.weather_country_code);
    Serial.printf("  Units: %s\n", appConfig.weather_units);
    Serial.printf("  Update Interval: %lu ms\n", appConfig.update_interval);
    Serial.println("=========================================\n");

    return true;
}

#endif
