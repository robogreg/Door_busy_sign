#include "display.h"

#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "app.h"
#include "config.h"

// Driver subclass only to lengthen the BUSY timeout (protected in GxEPD2_EPD).
class Epd750cZ08Long : public GxEPD2_750c_Z08 {
 public:
  Epd750cZ08Long(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750c_Z08(cs, dc, rst, busy) {
    _busy_timeout = EPD_BUSY_TIMEOUT_US;
  }
};

// Paged drawing: 160-row pages, two bit planes = 32 KB buffer (no PSRAM).
static GxEPD2_3C<Epd750cZ08Long, 160> display(Epd750cZ08Long(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;

static const int16_t W = 800;

static SemaphoreHandle_t gFrameLock;
static TaskHandle_t gTask = nullptr;
static Frame gPending;
static bool gForce = false;
static uint32_t gRequestedHash = 0;
static uint32_t gDrawnHash = 0;
static volatile bool gBusy = false;
static volatile uint32_t gLastMs = 0, gCount = 0;

uint32_t Frame::hash() const {
  uint32_t h = door::fnv1a(name);
  h = door::fnv1a("|" + title, h);
  h = door::fnv1a("|" + label, h);
  h = door::fnv1a("|" + quip, h);
  h = door::fnv1a("|" + timeLine, h);
  h = door::fnv1a("|" + footerRight, h);
  char tail[48];
  snprintf(tail, sizeof(tail), "|%d%d%d%d%d|%u-%u|%d", (int)band, muted, manual, stale, strip, dayStart, dayEnd,
           strip ? nowMin : -1);
  h = door::fnv1a(tail, h);
  if (strip) {
    for (auto& b : blocks) {
      snprintf(tail, sizeof(tail), "|%u-%u", b.first, b.second);
      h = door::fnv1a(tail, h);
    }
  }
  return h;
}

// ---------- text helpers ----------

static int16_t textW(const char* s) { return u8g2.getUTF8Width(s); }

static void centered(const char* s, int16_t cx, int16_t baseline) {
  u8g2.setCursor(cx - textW(s) / 2, baseline);
  u8g2.print(s);
}

// Pick the largest font from the list that fits maxW.
static void fitFont(const char* s, const uint8_t* const* fonts, size_t n, int16_t maxW) {
  for (size_t i = 0; i < n; i++) {
    u8g2.setFont(fonts[i]);
    if (textW(s) <= maxW) return;
  }
}

static const uint8_t* const kLabelFonts[] = {
    u8g2_font_logisoso58_tr, u8g2_font_logisoso50_tr, u8g2_font_logisoso42_tr,
    u8g2_font_logisoso34_tr, u8g2_font_logisoso28_tr, u8g2_font_logisoso22_tr,
};
static const uint8_t* const kQuipFonts[] = {u8g2_font_lubBI18_tf, u8g2_font_lubBI14_tf,
                                            u8g2_font_lubBI12_tf};

static uint16_t bandColor(door::Band b) {
  switch (b) {
    case door::Band::Red: return GxEPD_RED;
    case door::Band::Black: return GxEPD_BLACK;
    default: return GxEPD_WHITE;
  }
}

// ---------- screens ----------

static void drawStatus(const Frame& f, const std::string& updated) {
  display.fillScreen(GxEPD_WHITE);
  u8g2.setFontMode(1);

  // Header (0..53)
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  u8g2.setFont(u8g2_font_helvB24_tf);
  u8g2.setCursor(20, 38);
  u8g2.print(f.name.c_str());
  u8g2.setFont(u8g2_font_helvR14_tf);
  u8g2.setCursor(W - 20 - textW(f.title.c_str()), 36);
  u8g2.print(f.title.c_str());
  display.fillRect(0, 50, W, 3, GxEPD_BLACK);

  // With the timeline strip the band and text lines move up to make room.
  const bool S = f.strip;
  const int16_t bandY = 62, bandH = S ? 180 : 200;
  const int16_t quipY = S ? 292 : 318, timeY = S ? 345 : 385;
  uint16_t bg = bandColor(f.band);
  uint16_t fg = (f.band == door::Band::None) ? GxEPD_BLACK : GxEPD_WHITE;
  if (f.band != door::Band::None) display.fillRect(0, bandY, W, bandH, bg);
  if (f.muted) {
    for (int i = 0; i < 3; i++) display.drawRect(20 + i, bandY + 10 + i, W - 40 - 2 * i, bandH - 20 - 2 * i, GxEPD_BLACK);
  }
  u8g2.setForegroundColor(fg);
  u8g2.setBackgroundColor(bg);
  fitFont(f.label.c_str(), kLabelFonts, sizeof(kLabelFonts) / sizeof(kLabelFonts[0]), W - 80);
  int16_t asc = u8g2.getFontAscent();
  centered(f.label.c_str(), W / 2, bandY + bandH / 2 + asc / 2);

  // Quip
  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  if (!f.quip.empty()) {
    fitFont(f.quip.c_str(), kQuipFonts, sizeof(kQuipFonts) / sizeof(kQuipFonts[0]), W - 40);
    centered(f.quip.c_str(), W / 2, quipY);
  }

  // Time line
  if (!f.timeLine.empty()) {
    u8g2.setFont(u8g2_font_helvB24_tf);
    centered(f.timeLine.c_str(), W / 2, timeY);
  }

  // Timeline strip (370..440): working hours, busy blocks black, red "now" tick.
  if (S && f.dayEnd > f.dayStart) {
    const int16_t tlX = 40, tlW = W - 80, tlY = 392, tlH = 26;
    auto xFor = [&](int32_t m) -> int16_t {
      if (m < f.dayStart) m = f.dayStart;
      if (m > f.dayEnd) m = f.dayEnd;
      return tlX + (int32_t)(m - f.dayStart) * tlW / (f.dayEnd - f.dayStart);
    };
    display.drawRect(tlX, tlY, tlW, tlH, GxEPD_BLACK);
    for (auto& b : f.blocks) {
      int16_t a = xFor(b.first), z = xFor(b.second);
      if (z > a) display.fillRect(a, tlY, z - a, tlH, GxEPD_BLACK);
    }
    u8g2.setFont(u8g2_font_helvR12_tf);
    int span = (f.dayEnd - f.dayStart) / 60;
    int step = span > 12 ? 3 : 2;
    for (int h = (f.dayStart + 59) / 60; h * 60 <= f.dayEnd; h++) {
      int16_t x = xFor(h * 60);
      display.drawFastVLine(x, tlY + tlH, 5, GxEPD_BLACK);
      if ((h - (f.dayStart + 59) / 60) % step) continue;
      char lbl[4];
      int h12 = h % 12 == 0 ? 12 : h % 12;
      snprintf(lbl, sizeof(lbl), "%d%c", h12, (h % 24) < 12 ? 'a' : 'p');
      centered(lbl, x, tlY - 6);
    }
    if (f.nowMin >= f.dayStart && f.nowMin <= f.dayEnd) {
      // White halo keeps the red tick visible on top of a black block.
      int16_t nx = xFor(f.nowMin);
      display.fillRect(nx - 4, tlY - 2, 9, tlH + 4, GxEPD_WHITE);
      display.fillRect(nx - 2, tlY - 2, 5, tlH + 10, GxEPD_RED);
      display.fillTriangle(nx - 7, tlY + tlH + 14, nx + 7, tlY + tlH + 14, nx, tlY + tlH + 6, GxEPD_RED);
    }
  }

  // Footer (450..480)
  display.drawFastHLine(0, 450, W, GxEPD_BLACK);
  u8g2.setFont(u8g2_font_helvR12_tf);
  std::string left = updated.empty() ? std::string() : "Updated " + updated;
  int16_t x = 10;
  // Small inverted tags: MANUAL for an active override, STALE when the broker is unreachable.
  auto tag = [&](const char* t, uint16_t bg) {
    int16_t tw = textW(t) + 10;
    display.fillRect(x, 455, tw, 21, bg);
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(bg);
    u8g2.setCursor(x + 5, 471);
    u8g2.print(t);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
    x += tw + 10;
  };
  if (f.manual) tag("MANUAL", GxEPD_BLACK);
  if (f.stale) tag("STALE", GxEPD_RED);
  u8g2.setCursor(x, 472);
  u8g2.print(left.c_str());
  u8g2.setCursor(W - 10 - textW(f.footerRight.c_str()), 472);
  u8g2.print(f.footerRight.c_str());
}

static void drawBoot(const BootInfo& b) {
  display.fillScreen(GxEPD_WHITE);
  u8g2.setFontMode(1);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  display.fillRect(0, 0, W, 110, GxEPD_RED);
  u8g2.setForegroundColor(GxEPD_WHITE);
  u8g2.setBackgroundColor(GxEPD_RED);
  u8g2.setFont(u8g2_font_logisoso50_tr);
  centered("DOOR SIGN", W / 2, 80);

  u8g2.setForegroundColor(GxEPD_BLACK);
  u8g2.setBackgroundColor(GxEPD_WHITE);
  const int16_t lx = 60, vx = 260;
  int16_t y = 175;
  auto row = [&](const char* k, const std::string& v) {
    u8g2.setFont(u8g2_font_helvR18_tf);
    u8g2.setCursor(lx, y);
    u8g2.print(k);
    u8g2.setFont(u8g2_font_helvB24_tf);
    u8g2.setCursor(vx, y);
    u8g2.print(v.c_str());
    y += 52;
  };
  if (b.online) {
    row("Network", b.ssid);
    row("IP address", b.ip);
    row("Web page", "http://" + b.host + ".local");
    row("MAC", b.mac);
  } else {
    row("Network", "NOT CONNECTED");
    row("MAC", b.mac);
  }
  display.drawFastHLine(0, 450, W, GxEPD_BLACK);
  u8g2.setFont(u8g2_font_helvR12_tf);
  u8g2.setCursor(10, 472);
  u8g2.print(("Firmware " + b.fw).c_str());
}

// ---------- panel plumbing ----------

void displayBegin() {
  SPI.end();
  SPI.begin(EPD_SCLK, EPD_MISO, EPD_MOSI, EPD_CS);
  display.init(0, true, EPD_RESET_MS, false);
  u8g2.begin(display);
  gFrameLock = xSemaphoreCreateMutex();
}

void drawBootScreen(const BootInfo& b) {
  uint32_t t0 = millis();
  display.setFullWindow();
  display.firstPage();
  do {
    drawBoot(b);
  } while (display.nextPage());
  display.hibernate();
  logf("[epd] boot screen drawn in %lu ms", (unsigned long)(millis() - t0));
}

static void renderTask(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Frame f;
    bool force;
    xSemaphoreTake(gFrameLock, portMAX_DELAY);
    f = gPending;
    force = gForce;
    gForce = false;
    xSemaphoreGive(gFrameLock);
    uint32_t h = f.hash();
    if (h == gDrawnHash && !force) continue;

    door::Clock c = nowClock();
    std::string updated = c.valid ? door::formatTime(c.secOfDay) : "";
    logf("[epd] render '%s' / '%s'", f.label.c_str(), f.timeLine.c_str());
    gBusy = true;
    uint32_t t0 = millis();
    display.setFullWindow();
    display.firstPage();
    do {
      drawStatus(f, updated);
    } while (display.nextPage());
    display.hibernate();
    gLastMs = millis() - t0;
    gCount++;
    gDrawnHash = h;
    gBusy = false;
    logf("[epd] render done in %lu ms", (unsigned long)gLastMs);
  }
}

void displayStartTask() {
  // Core 1 alongside loop(); AsyncTCP runs on core 0 by default.
  xTaskCreatePinnedToCore(renderTask, "render", 8192, nullptr, 1, &gTask, 1);
}

void displayRequest(const Frame& f, bool force) {
  uint32_t h = f.hash();
  if (h == gRequestedHash && !force) return;
  xSemaphoreTake(gFrameLock, portMAX_DELAY);
  gPending = f;
  if (force) gForce = true;
  gRequestedHash = h;
  xSemaphoreGive(gFrameLock);
  if (gTask) xTaskNotifyGive(gTask);
}

bool displayBusy() { return gBusy; }
uint32_t displayLastMs() { return gLastMs; }
uint32_t displayCount() { return gCount; }
