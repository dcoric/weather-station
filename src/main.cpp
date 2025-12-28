#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "config.h"
#include "weather_images.h"

// Display and LVGL objects
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[240 * 10];

// UI elements
lv_obj_t *temp_label;
lv_obj_t *weather_label;
lv_obj_t *humidity_label;
lv_obj_t *city_label;
lv_obj_t *update_label;
lv_obj_t *status_label;
lv_obj_t *weather_icon;
lv_obj_t *forecast_container;

struct ForecastUI {
    lv_obj_t *day_label;
    lv_obj_t *icon;
    lv_obj_t *temp_label;
};

ForecastUI forecast_items[3];

struct IconEntry {
    const char *code;
    const lv_img_dsc_t *image;
};

static const IconEntry ICON_MAP[] = {
    {"01d", &image_weather_icon_01d},
    {"01n", &image_weather_icon_01n},
    {"02d", &image_weather_icon_02d},
    {"02n", &image_weather_icon_02n},
    {"03d", &image_weather_icon_03d},
    {"03n", &image_weather_icon_03n},
    {"04d", &image_weather_icon_04d},
    {"04n", &image_weather_icon_04n},
    {"09d", &image_weather_icon_09d},
    {"09n", &image_weather_icon_09n},
    {"10d", &image_weather_icon_10d},
    {"10n", &image_weather_icon_10n},
    {"11d", &image_weather_icon_11d},
    {"11n", &image_weather_icon_11n},
    {"13d", &image_weather_icon_13d},
    {"13n", &image_weather_icon_13n},
    {"50d", &image_weather_icon_50d},
    {"50n", &image_weather_icon_50n},
};

static const char *DAY_NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const uint16_t WEATHER_ICON_SOURCE_SIZE = 100;
static const uint16_t FORECAST_ICON_SIZE = 44;

// Weather data
struct WeatherData {
    float temperature;
    float feels_like;
    int humidity;
    String description;
    String icon;
    String city;
    String last_update_time;
};

WeatherData weather;
struct ForecastEntry {
    String day;
    float temp_min;
    float temp_max;
    String icon;
    bool valid;
};

ForecastEntry forecast_data[3];
unsigned long lastUpdate = 0;

// Display flushing callback
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(disp);
}

const lv_img_dsc_t *get_icon_for_code(const String &icon_code) {
    for (const auto &entry : ICON_MAP) {
        if (icon_code == entry.code) {
            return entry.image;
        }
    }
    return &image_weather_icon_01d;
}

void set_icon_size(lv_obj_t *img_obj, uint16_t size_px) {
    if (!img_obj || WEATHER_ICON_SOURCE_SIZE == 0) {
        return;
    }
    uint32_t zoom = (static_cast<uint32_t>(size_px) * 256U) / WEATHER_ICON_SOURCE_SIZE;
    lv_img_set_zoom(img_obj, zoom);
}

void set_icon_size_with_crop(lv_obj_t *img_obj, uint16_t size_px, float crop_factor) {
    if (!img_obj || WEATHER_ICON_SOURCE_SIZE == 0) {
        return;
    }
    // Apply crop_factor to zoom in and crop the transparent padding
    uint32_t zoom = (static_cast<uint32_t>(size_px * crop_factor) * 256U) / WEATHER_ICON_SOURCE_SIZE;
    lv_img_set_zoom(img_obj, zoom);
}

void hide_status_message() {
    if (!status_label) {
        return;
    }
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void show_status_message(const char *message, uint32_t color) {
    if (!status_label) {
        return;
    }
    lv_label_set_text(status_label, message);
    lv_obj_set_style_text_color(status_label, lv_color_hex(color), 0);
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);
}

void update_forecast_ui() {
    for (int i = 0; i < 3; ++i) {
        if (!forecast_items[i].day_label) {
            continue;
        }

        if (forecast_data[i].valid) {
            char temp_buffer[16];
            snprintf(temp_buffer, sizeof(temp_buffer), "%.0f°/%.0f°", forecast_data[i].temp_max, forecast_data[i].temp_min);
            lv_label_set_text(forecast_items[i].day_label, forecast_data[i].day.c_str());
            lv_label_set_text(forecast_items[i].temp_label, temp_buffer);
            lv_img_set_src(forecast_items[i].icon, get_icon_for_code(forecast_data[i].icon));
        } else {
            lv_label_set_text(forecast_items[i].day_label, "--");
            lv_label_set_text(forecast_items[i].temp_label, "--°/--°");
            lv_img_set_src(forecast_items[i].icon, &image_weather_icon_01d);
        }
        // set_icon_size_with_crop(forecast_items[i].icon, FORECAST_ICON_SIZE, 1.8);

        set_icon_size(forecast_items[i].icon, FORECAST_ICON_SIZE);
        lv_obj_set_style_translate_y(forecast_items[i].icon, -10, 0);
        lv_obj_set_style_translate_y(forecast_items[i].day_label, -35, 0);
        lv_obj_set_style_translate_y(forecast_items[i].temp_label, -35, 0);

        // lv_obj_set_height(forecast_items[i].icon, FORECAST_ICON_SIZE);
        // lv_obj_set_style_transform_pivot_y(forecast_items[i].icon, 0, 0);
    }
}

void reset_forecast_data() {
    for (auto &entry : forecast_data) {
        entry.valid = false;
        entry.day = "";
        entry.icon = "01d";
        entry.temp_max = 0.0f;
        entry.temp_min = 0.0f;
    }
}

String format_update_time(long epoch_seconds, long timezone_offset_seconds) {
    if (epoch_seconds <= 0) {
        return String();
    }

    time_t adjusted_time = static_cast<time_t>(epoch_seconds + timezone_offset_seconds);
    struct tm timeinfo;
    if (!gmtime_r(&adjusted_time, &timeinfo)) {
        return String();
    }

    char buffer[16];
    size_t written = strftime(buffer, sizeof(buffer), "%H:%M", &timeinfo);
    if (written == 0) {
        return String();
    }

    return String(buffer);
}

// Initialize LVGL display
void lvgl_init() {
    lv_init();

    // Turn on backlight
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    lv_disp_draw_buf_init(&draw_buf, buf, NULL, 240 * 10);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = 240;
    disp_drv.ver_res = 320;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

// Create UI
void create_ui() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1E1E1E), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF5555), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    city_label = lv_label_create(scr);
    lv_label_set_text(city_label, "Loading...");
    lv_obj_set_style_text_color(city_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(city_label, &lv_font_montserrat_20, 0);
    lv_obj_align(city_label, LV_ALIGN_TOP_MID, 0, 16);

    weather_icon = lv_img_create(scr);
    lv_img_set_src(weather_icon, &image_weather_icon_01d);
    lv_obj_align(weather_icon, LV_ALIGN_TOP_MID, 0, 10);
    set_icon_size(weather_icon, 72);

    temp_label = lv_label_create(scr);
    lv_label_set_text(temp_label, "--,-°C");
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_36, 0);
    lv_obj_align_to(temp_label, weather_icon, LV_ALIGN_TOP_MID, 0, 80);

    weather_label = lv_label_create(scr);
    lv_label_set_text(weather_label, "--------- ------");
    lv_obj_set_style_text_color(weather_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_align(weather_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(weather_label, 240);
    lv_obj_align(weather_label, LV_ALIGN_TOP_MID, 0, 142);

    humidity_label = lv_label_create(scr);
    lv_label_set_text(humidity_label, "Humidity: --%");
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_16, 0);
    lv_obj_align_to(humidity_label, weather_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    forecast_container = lv_obj_create(scr);
    lv_obj_set_width(forecast_container, 220);
    lv_obj_set_height(forecast_container, 100); // LV_SIZE_CONTENT
    lv_obj_align(forecast_container, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_color(forecast_container, lv_color_hex(0x2A2A2A), 0);
    lv_obj_set_style_border_width(forecast_container, 0, 0);
    lv_obj_set_style_radius(forecast_container, 12, 0);
    lv_obj_set_style_pad_all(forecast_container, 4, 0);
    lv_obj_set_style_pad_row(forecast_container, 0, 0);
    lv_obj_set_style_pad_column(forecast_container, 4, 0);
    lv_obj_set_flex_flow(forecast_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(forecast_container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(forecast_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(forecast_container, LV_DIR_NONE);
    lv_obj_set_scrollbar_mode(forecast_container, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < 3; ++i) {
        lv_obj_t *item = lv_obj_create(forecast_container);
        lv_obj_set_width(item, 64);
        lv_obj_set_height(item, LV_SIZE_CONTENT); // LV_SIZE_CONTENT
        lv_obj_set_style_bg_color(item, lv_color_hex(0x1F1F1F), 0);
        lv_obj_set_style_border_width(item, 0, 0);
        lv_obj_set_style_radius(item, 10, 0);
        lv_obj_set_style_pad_all(item, 2, 0);
        lv_obj_set_style_pad_row(item, 0, 0);
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(item, LV_DIR_NONE);
        lv_obj_set_scrollbar_mode(item, LV_SCROLLBAR_MODE_OFF);

        forecast_items[i].icon = lv_img_create(item);
        lv_img_set_src(forecast_items[i].icon, &image_weather_icon_01d);
        set_icon_size(forecast_items[i].icon, FORECAST_ICON_SIZE);
        lv_obj_set_style_pad_all(forecast_items[i].icon, 0, 0);
        lv_obj_add_flag(forecast_items[i].icon, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_style_transform_pivot_y(forecast_items[i].icon, 0, 0);
        lv_obj_set_style_translate_y(forecast_items[i].icon, -5, 0);

        forecast_items[i].day_label = lv_label_create(item);
        lv_label_set_text(forecast_items[i].day_label, "---");
        lv_obj_set_style_text_color(forecast_items[i].day_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(forecast_items[i].day_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(forecast_items[i].day_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(forecast_items[i].day_label, 0, 0);
        lv_obj_set_style_translate_y(forecast_items[i].day_label, -10, 0);

        forecast_items[i].temp_label = lv_label_create(item);
        lv_label_set_text(forecast_items[i].temp_label, "--°/--°");
        lv_obj_set_style_text_color(forecast_items[i].temp_label, lv_color_hex(0xBBBBBB), 0);
        lv_obj_set_style_text_font(forecast_items[i].temp_label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(forecast_items[i].temp_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_all(forecast_items[i].temp_label, 0, 0);
        lv_obj_set_style_translate_y(forecast_items[i].temp_label, -10, 0);
    }

    update_label = lv_label_create(scr);
    lv_label_set_text(update_label, "Last update: --:--");
    lv_obj_set_style_text_color(update_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(update_label, &lv_font_montserrat_10, 0);
    lv_obj_align_to(update_label, forecast_container, LV_ALIGN_OUT_TOP_MID, 0, -6);

    update_forecast_ui();
}

// Update weather icon based on OpenWeatherMap icon code
void update_weather_icon(const String &icon_code) {
    lv_img_set_src(weather_icon, get_icon_for_code(icon_code));
}

// Update UI with weather data
void update_ui() {
    char temp_str[32];
    char humidity_str[32];
    char update_str[64];

    sprintf(temp_str, "%.1f°C", weather.temperature);
    lv_label_set_text(temp_label, temp_str);

    lv_label_set_text(city_label, weather.city.c_str());

    String desc = weather.description;
    if (desc.length() > 0) {
        desc[0] = toupper(desc[0]);
    }
    lv_label_set_text(weather_label, desc.c_str());

    // Update weather icon
    update_weather_icon(weather.icon);

    sprintf(humidity_str, "Humidity: %d%%", weather.humidity);
    lv_label_set_text(humidity_label, humidity_str);

    if (weather.last_update_time.length() > 0) {
        snprintf(update_str, sizeof(update_str), "Last update: %s", weather.last_update_time.c_str());
    } else {
        snprintf(update_str, sizeof(update_str), "Last update: --:--");
    }
    lv_label_set_text(update_label, update_str);

    hide_status_message();
}

// Connect to WiFi
bool connect_wifi() {
    Serial.println("Starting WiFi connection...");
    Serial.print("SSID: ");
    Serial.println(WIFI_SSID);

    // Disconnect any previous connection
    WiFi.disconnect(true);
    delay(100);

    // Set WiFi mode to station
    WiFi.mode(WIFI_STA);
    delay(100);

    // Begin WiFi connection
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
        lv_timer_handler();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        Serial.print("Signal strength (RSSI): ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        hide_status_message();
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
        show_status_message("WiFi Failed!", 0xFF0000);
        return false;
    }
}

// Fetch weather data
bool fetch_weather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        show_status_message("No WiFi", 0xFF0000);
        return false;
    }

    HTTPClient http;
    String url = "http://";
    url += WEATHER_API_HOST;
    url += WEATHER_CURRENT_API_URL;

    Serial.println("Fetching weather data...");
    lv_timer_handler();

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();

        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            weather.temperature = doc["main"]["temp"];
            weather.feels_like = doc["main"]["feels_like"];
            weather.humidity = doc["main"]["humidity"];
            weather.description = doc["weather"][0]["description"].as<String>();
            weather.icon = doc["weather"][0]["icon"].as<String>();
            weather.city = doc["name"].as<String>();
            long update_epoch = doc["dt"] | 0L;
            long timezone_offset = doc["timezone"] | 0L;
            weather.last_update_time = format_update_time(update_epoch, timezone_offset);

            Serial.println("Weather data updated successfully");
            Serial.printf("Temperature: %.1f°C\n", weather.temperature);
            Serial.printf("Humidity: %d%%\n", weather.humidity);
            Serial.printf("Description: %s\n", weather.description.c_str());

            update_ui();
            http.end();
            return true;
        } else {
            Serial.println("JSON parsing failed");
            show_status_message("Parse Error", 0xFF0000);
        }
    } else {
        Serial.printf("HTTP error: %d\n", httpCode);
        show_status_message("API Error", 0xFF0000);
    }

    http.end();
    return false;
}

bool fetch_forecast() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        show_status_message("No WiFi", 0xFF0000);
        return false;
    }

    HTTPClient http;
    String url = "http://";
    url += WEATHER_API_HOST;
    url += WEATHER_FORECAST_API_URL;

    Serial.println("Fetching forecast data...");
    lv_timer_handler();

    http.begin(url);
    int httpCode = http.GET();

    if (httpCode == 200) {
        String payload = http.getString();
        DynamicJsonDocument doc(32768);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            reset_forecast_data();
            JsonArray list = doc["list"].as<JsonArray>();
            int saved_days = 0;
            int last_day = -1;
            int reference_day = -1;

            if (!list.isNull()) {
                for (JsonVariant value : list) {
                    time_t timestamp = static_cast<time_t>(value["dt"].as<long>());
                    struct tm timeinfo;
                    if (!gmtime_r(&timestamp, &timeinfo)) {
                        continue;
                    }

                    if (reference_day == -1) {
                        reference_day = timeinfo.tm_mday;
                    }

                    if (timeinfo.tm_mday == reference_day) {
                        continue;
                    }

                    if (timeinfo.tm_mday == last_day) {
                        continue;
                    }

                    ForecastEntry &entry = forecast_data[saved_days];
                    entry.day = DAY_NAMES[timeinfo.tm_wday];
                    entry.temp_min = value["main"]["temp_min"] | 0.0f;
                    entry.temp_max = value["main"]["temp_max"] | 0.0f;
                    String icon = value["weather"][0]["icon"].as<String>();
                    if (icon.length() == 0) {
                        icon = "01d";
                    }
                    entry.icon = icon;
                    entry.valid = true;

                    last_day = timeinfo.tm_mday;
                    saved_days++;

                    if (saved_days >= 3) {
                        break;
                    }
                }
            }

            update_forecast_ui();
            http.end();
            return true;
        } else {
            Serial.println("Forecast JSON parsing failed");
            show_status_message("Forecast Parse Error", 0xFF0000);
        }
    } else {
        Serial.printf("Forecast HTTP error: %d\n", httpCode);
        show_status_message("Forecast API Error", 0xFF0000);
    }

    http.end();
    return false;
}

void setup() {
    Serial.begin(115200);
    Serial.println("ESP32 Weather Station Starting...");

    lvgl_init();
    create_ui();

    if (connect_wifi()) {
        if (fetch_weather()) {
            fetch_forecast();
        }
    }
}

void loop() {
    lv_timer_handler();
    delay(5);

    if (millis() - lastUpdate > UPDATE_INTERVAL) {
        if (fetch_weather()) {
            fetch_forecast();
        }
        lastUpdate = millis();
    }
}
