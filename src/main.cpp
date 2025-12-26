#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include "config.h"

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

// Weather data
struct WeatherData {
    float temperature;
    float feels_like;
    int humidity;
    String description;
    String icon;
    String city;
    unsigned long last_update;
};

WeatherData weather;
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

    // Title bar
    lv_obj_t *title_bar = lv_obj_create(scr);
    lv_obj_set_size(title_bar, 240, 50);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_hex(0x2196F3), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);

    lv_obj_t *title = lv_label_create(title_bar);
    lv_label_set_text(title, "Weather Station");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    // City label
    city_label = lv_label_create(scr);
    lv_label_set_text(city_label, "Loading...");
    lv_obj_set_style_text_color(city_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(city_label, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(city_label, 10, 60);

    // Weather icon - using simple text representation
    weather_icon = lv_label_create(scr);
    lv_label_set_text(weather_icon, "---");  // Placeholder
    lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xFFDD00), 0);
    lv_obj_set_style_text_font(weather_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(weather_icon, 10, 95);

    // Temperature label
    temp_label = lv_label_create(scr);
    lv_label_set_text(temp_label, "--°C");
    lv_obj_set_style_text_color(temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_48, 0);
    lv_obj_set_pos(temp_label, 10, 130);

    // Weather description
    weather_label = lv_label_create(scr);
    lv_label_set_text(weather_label, "");
    lv_obj_set_style_text_color(weather_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(weather_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(weather_label, 10, 200);

    // Humidity label
    humidity_label = lv_label_create(scr);
    lv_label_set_text(humidity_label, "Humidity: --%");
    lv_obj_set_style_text_color(humidity_label, lv_color_hex(0xBBBBBB), 0);
    lv_obj_set_style_text_font(humidity_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(humidity_label, 10, 230);

    // Update time label
    update_label = lv_label_create(scr);
    lv_label_set_text(update_label, "Last update: Never");
    lv_obj_set_style_text_color(update_label, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(update_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(update_label, 10, 270);

    // Status label
    status_label = lv_label_create(scr);
    lv_label_set_text(status_label, "Connecting to WiFi...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(status_label, 10, 295);
}

// Update weather icon based on OpenWeatherMap icon code
void update_weather_icon(String icon_code) {
    // OpenWeatherMap icon codes:
    // 01d = clear sky day, 01n = clear sky night
    // 02d/02n = few clouds, 03d/03n = scattered clouds, 04d/04n = broken clouds
    // 09d/09n = shower rain, 10d/10n = rain, 11d/11n = thunderstorm
    // 13d/13n = snow, 50d/50n = mist

    // Using simple text icons that will render properly
    if (icon_code == "01d") {
        lv_label_set_text(weather_icon, "SUN");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xFFDD00), 0);
    } else if (icon_code == "01n") {
        lv_label_set_text(weather_icon, "MOON");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xCCCCFF), 0);
    } else if (icon_code == "02d" || icon_code == "02n" ||
               icon_code == "03d" || icon_code == "03n" ||
               icon_code == "04d" || icon_code == "04n") {
        lv_label_set_text(weather_icon, "CLOUD");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xCCCCCC), 0);
    } else if (icon_code == "09d" || icon_code == "09n" ||
               icon_code == "10d" || icon_code == "10n") {
        lv_label_set_text(weather_icon, "RAIN");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0x6699FF), 0);
    } else if (icon_code == "11d" || icon_code == "11n") {
        lv_label_set_text(weather_icon, "STORM");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xFFDD00), 0);
    } else if (icon_code == "13d" || icon_code == "13n") {
        lv_label_set_text(weather_icon, "SNOW");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xDDDDFF), 0);
    } else if (icon_code == "50d" || icon_code == "50n") {
        lv_label_set_text(weather_icon, "FOG");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xAAAAAA), 0);
    } else {
        lv_label_set_text(weather_icon, "---");
        lv_obj_set_style_text_color(weather_icon, lv_color_hex(0xCCCCCC), 0);
    }
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
    desc[0] = toupper(desc[0]);
    lv_label_set_text(weather_label, desc.c_str());

    // Update weather icon
    update_weather_icon(weather.icon);

    sprintf(humidity_str, "Humidity: %d%%", weather.humidity);
    lv_label_set_text(humidity_label, humidity_str);

    unsigned long minutes_ago = (millis() - weather.last_update) / 60000;
    if (minutes_ago == 0) {
        sprintf(update_str, "Last update: Just now");
    } else {
        sprintf(update_str, "Last update: %lu min ago", minutes_ago);
    }
    lv_label_set_text(update_label, update_str);

    lv_label_set_text(status_label, "Status: OK");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x00FF00), 0);
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
        return true;
    } else {
        Serial.println("\nWiFi connection failed!");
        Serial.print("WiFi status: ");
        Serial.println(WiFi.status());
        lv_label_set_text(status_label, "WiFi Failed!");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return false;
    }
}

// Fetch weather data
bool fetch_weather() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected");
        lv_label_set_text(status_label, "No WiFi");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        return false;
    }

    HTTPClient http;
    String url = "http://";
    url += WEATHER_API_HOST;
    url += WEATHER_API_URL;

    Serial.println("Fetching weather data...");
    lv_label_set_text(status_label, "Updating...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFAA00), 0);
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
            weather.last_update = millis();

            Serial.println("Weather data updated successfully");
            Serial.printf("Temperature: %.1f°C\n", weather.temperature);
            Serial.printf("Humidity: %d%%\n", weather.humidity);
            Serial.printf("Description: %s\n", weather.description.c_str());

            update_ui();
            http.end();
            return true;
        } else {
            Serial.println("JSON parsing failed");
            lv_label_set_text(status_label, "Parse Error");
            lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
        }
    } else {
        Serial.printf("HTTP error: %d\n", httpCode);
        lv_label_set_text(status_label, "API Error");
        lv_obj_set_style_text_color(status_label, lv_color_hex(0xFF0000), 0);
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
        fetch_weather();
    }
}

void loop() {
    lv_timer_handler();
    delay(5);

    if (millis() - lastUpdate > UPDATE_INTERVAL) {
        fetch_weather();
        lastUpdate = millis();
    }
}
