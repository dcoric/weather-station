// Simple TFT test to verify display is working
// Temporarily rename main.cpp and rename this to main.cpp to test

#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  Serial.println("TFT Test Starting...");

  tft.init();
  tft.setRotation(0);

  Serial.println("Filling screen red...");
  tft.fillScreen(TFT_RED);
  delay(1000);

  Serial.println("Filling screen green...");
  tft.fillScreen(TFT_GREEN);
  delay(1000);

  Serial.println("Filling screen blue...");
  tft.fillScreen(TFT_BLUE);
  delay(1000);

  Serial.println("Drawing white text...");
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Display Works!");

  Serial.println("Test complete!");
}

void loop() {
  // Nothing
}
