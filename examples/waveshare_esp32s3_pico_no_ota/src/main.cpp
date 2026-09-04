#include <Arduino.h>
#include <esp_heap_caps.h>
#include <Adafruit_NeoPixel.h>

#define LED_PIN    21      // GPIO21
#define LED_COUNT  1       // Number of RGB LEDs

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  Serial.begin(115200);
  Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
  strip.begin();
  strip.show(); // Turns RGB LED off
  strip.setBrightness(30); // Set brightness: 0-255
}

void loop() {
  // RGB LED lights red
  strip.setPixelColor(0, strip.Color(255, 0, 0));
  strip.show();
  Serial.printf("RGB LED - red\n");
  delay(1000);

  // RGB LED lights green
  strip.setPixelColor(0, strip.Color(0, 255, 0));
  strip.show();
  Serial.printf("RGB LED - green\n");
  delay(1000);

  // RGB LED lights blue
  strip.setPixelColor(0, strip.Color(0, 0, 255));
  strip.show();
  Serial.printf("RGB LED - blue\n");
  delay(1000);

  // RGB LED lights white
  strip.setPixelColor(0, strip.Color(255, 255, 255));
  strip.show();
  Serial.printf("RGB LED - white\n");
  delay(1000);

  // Print free PSRAM to serial console
  Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
}