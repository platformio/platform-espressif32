# Waveshare ESP32-S3 Pico (no OTA) — Example Project

This example demonstrates basic PSRAM detection and RGB LED control.

## Board details

- PSRAM size: 2MB (detected via `ESP.getPsramSize()`)
- Flash size: 16MB
- RGB LED is connected to **GPIO21**
- Partition table: `waveshare_esp32s3_pico_16MB_no_ota.csv`
- More Waveshare ESP32-S3 Pico docuemntation can be found [here](https://www.waveshare.com/wiki/ESP32-S3-Pico)

## About the example

In this example we operate on the built in RGB LED, chceck the total size of PSRAM and the available space in the PSRAM.

## Other information

Tested with Arduino framework and PlatformIO IDE v. 3.3.4