// Door Sign: pins, defaults, version.
#pragma once

#define FW_VERSION "0.5.0"

// Waveshare E-Paper ESP32 Driver Board. Board-wired, NOT the ESP32 default SPI pins.
#define EPD_MOSI 14
#define EPD_SCLK 13
#define EPD_CS   15
#define EPD_DC   27
#define EPD_RST  26
#define EPD_BUSY 25
#define EPD_MISO -1  // not connected

// Tri-color refresh takes up to 26 s. GxEPD2 defaults this panel to 20 s, too short.
#define EPD_BUSY_TIMEOUT_US 40000000UL

// Waveshare "clever" reset circuit wants a short reset pulse.
#define EPD_RESET_MS 2

#define MDNS_HOST "door"
