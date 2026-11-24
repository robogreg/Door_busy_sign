#include "display.h"

#include <Arduino.h>
#include <GxEPD2_3C.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>

#include "app.h"
#include "config.h"
#include "faces.h"

// Driver subclass: a longer BUSY timeout, plus an experimental fast black and
// white refresh (protected members of GxEPD2_EPD).
//
// Fast refresh runs the UC8179 controller in KW (black/white) mode with LUTs
// from registers instead of the panel's OTP tri-color waveform. RAM 0x10 holds
// the OLD image and 0x13 the NEW one; only pixels that change are driven, with
// charge-balanced pulses taken from GxEPD2's GDEW075T7 partial LUTs (same
// controller), scaled longer for the slower tri-color film. Red is drawn as
// black. Unsupported by the panel maker, so it is off by default and the sign
// still does a full color refresh regularly.
class Epd750cZ08Long : public GxEPD2_750c_Z08 {
 public:
  Epd750cZ08Long(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
      : GxEPD2_750c_Z08(cs, dc, rst, busy) {
    _busy_timeout = EPD_BUSY_TIMEOUT_US;
  }
  // strength 1..3 scales the waveform: about 70, 140 or 210 frames.
  void fastInit(uint8_t strength) {
    if (_hibernating) _reset();  // controller was asleep
    // Same base setup as GxEPD2_750c_Z08::_InitDisplay (private there).
    _writeCommand(0x01);  // power setting: VGH/VGL 20 V, VDH/VDL 15 V
    _writeData(0x07);
    _writeData(0x07);
    _writeData(0x3F);
    _writeData(0x3F);
    _writeCommand(0x61);  // resolution 800 x 480
    _writeData(WIDTH / 256);
    _writeData(WIDTH % 256);
    _writeData(HEIGHT / 256);
    _writeData(HEIGHT % 256);
    _writeCommand(0x15);
    _writeData(0x00);
    _writeCommand(0x60);  // TCON
    _writeData(0x22);
    _writeCommand(0x00);  // panel setting: LUT from registers, KW mode
    _writeData(0x3F);
    _writeCommand(0x82);  // VCOM DC -2.0 V, as GxEPD2 uses for the T7 partial LUT
    _writeData(0x26);
    _writeCommand(0x50);  // VCOM and data interval: border LUT, copy NEW to OLD after refresh
    _writeData(0x39);
    _writeData(0x07);
    const uint8_t m = strength < 1 ? 1 : strength > 3 ? 3 : strength;
    lut(0x20, 0x00, m);  // VCOM
    lut(0x21, 0x00, m);  // white stays white: not driven
    lut(0x22, 0x5A, m);  // black to white
    lut(0x23, 0x84, m);  // white to black
    lut(0x24, 0x00, m);  // black stays black: not driven
    lut(0x25, 0x00, m);  // border
    _using_partial_mode = false;
  }
  void planeBegin(uint8_t cmd) { _writeCommand(cmd); }
  void planeBytes(const uint8_t* d, size_t n) {
    _startTransfer();
    for (size_t i = 0; i < n; i++) _transfer(d[i]);
    _endTransfer();
  }
  void fastRefresh() {
    if (!_power_is_on) {
      _writeCommand(0x04);
      _waitWhileBusy("fast power on", 200);
      _power_is_on = true;
    }
    _writeCommand(0x12);
    _waitWhileBusy("fast", 3000);
    _writeCommand(0x02);  // power off, but no deep sleep: RAM keeps the OLD image
    _waitWhileBusy("fast power off", 200);
    _power_is_on = false;
  }
  // Back to the stock tri-color setup: a hardware reset clears our registers.
  void resetController() {
    _reset();
    _power_is_on = false;
    _using_partial_mode = false;
  }
  bool asleep() const { return _hibernating; }

 private:
  void lut(uint8_t cmd, uint8_t levels, uint8_t m) {
    // One group: 4 phase levels (2 bits each), 4 phase lengths in frames, repeat.
    const uint8_t g[6] = {levels, (uint8_t)(30 * m), (uint8_t)(5 * m), (uint8_t)(30 * m), (uint8_t)(5 * m), 1};
    _writeCommand(cmd);
    for (int i = 0; i < 60; i++) _writeData(i < 6 ? g[i] : (uint8_t)0);
  }
};

// Paged drawing: 160-row pages, two bit planes = 32 KB buffer (no PSRAM).
static GxEPD2_3C<Epd750cZ08Long, 160> display(Epd750cZ08Long(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
static U8G2_FOR_ADAFRUIT_GFX u8g2;  // bound to the panel; render task only

static const int16_t W = 800, H = 480;

static SemaphoreHandle_t gFrameLock;
static TaskHandle_t gTask = nullptr;
static Frame gPending;
static Frame gDrawn;              // what is on the panel now (guarded by gFrameLock)
static std::string gDrawnUpdated;  // its "Updated" time
static bool gForce = false;
static uint32_t gRequestedHash = 0;
static uint32_t gDrawnHash = 0;
static volatile bool gBusy = false;
static volatile uint32_t gLastMs = 0, gCount = 0;
static volatile bool gLastFast = false;
// Fast refresh bookkeeping (render task only).
static bool gOldValid = false;   // controller RAM 0x10 holds gDrawn in black/white
static bool gFastDirty = false;  // registers changed since the last reset
static uint8_t gFastCount = 0;   // fast refreshes since the last full one
static uint32_t gLastRenderMs = 0;
static const uint8_t kFullEvery = 12;               // full color refresh after this many fast ones
static const uint32_t kCleanupMs = 10 * 60 * 1000;  // and after 10 quiet minutes
static void writePlane(uint8_t cmd, const Frame& f, const std::string& updated);

uint32_t Frame::hash() const {
  uint32_t h = door::fnv1a(name);
  h = door::fnv1a("|" + title, h);
  h = door::fnv1a("|" + label, h);
  h = door::fnv1a("|" + quip, h);
  h = door::fnv1a("|" + timeLine, h);
  h = door::fnv1a("|" + footerRight, h);
  h = door::fnv1a("|" + date, h);
  char tail[48];
  snprintf(tail, sizeof(tail), "|%d%d%d%d%d%d|%u-%u|%d|m%d|g%d", (int)band, muted, manual, stale, strip, sim,
           dayStart, dayEnd, strip ? nowMin : -1, (int)mood, gallery);
  h = door::fnv1a(tail, h);
  if (strip) {
    for (auto& b : blocks) {
      snprintf(tail, sizeof(tail), "|%u-%u", b.first, b.second);
      h = door::fnv1a(tail, h);
    }
  }
  return h;
}

static const uint8_t* const kLabelFonts[] = {
    u8g2_font_logisoso58_tr, u8g2_font_logisoso50_tr, u8g2_font_logisoso42_tr,
    u8g2_font_logisoso34_tr, u8g2_font_logisoso28_tr, u8g2_font_logisoso22_tr,
};
// Plain sans for the quip / meeting title, largest that fits on one line.
static const uint8_t* const kQuipFonts[] = {u8g2_font_fur30_tf, u8g2_font_fur25_tf, u8g2_font_fur20_tf,
                                            u8g2_font_fur17_tf, u8g2_font_fur14_tf};

static uint16_t bandColor(door::Band b) {
  switch (b) {
    case door::Band::Red: return GxEPD_RED;
    case door::Band::Black: return GxEPD_BLACK;
    default: return GxEPD_WHITE;
  }
}

// ---------- screens ----------
// A Painter draws a screen onto any GFX target (the panel, or a preview strip).
// It holds no globals, so the panel and a web preview can render concurrently.

struct Painter {
  Adafruit_GFX& g;
  U8G2_FOR_ADAFRUIT_GFX& u;

  int16_t textW(const char* s) { return u.getUTF8Width(s); }

  void centered(const char* s, int16_t cx, int16_t baseline) {
    u.setCursor(cx - textW(s) / 2, baseline);
    u.print(s);
  }

  // Pick the largest font from the list that fits maxW.
  void fitFont(const char* s, const uint8_t* const* fonts, size_t n, int16_t maxW) {
    for (size_t i = 0; i < n; i++) {
      u.setFont(fonts[i]);
      if (textW(s) <= maxW) return;
    }
  }

  void status(const Frame& f, const std::string& updated) {
    g.fillScreen(GxEPD_WHITE);
    u.setFontMode(1);

    // Header (0..53)
    u.setForegroundColor(GxEPD_BLACK);
    u.setBackgroundColor(GxEPD_WHITE);
    u.setFont(u8g2_font_helvB24_tf);
    u.setCursor(20, 38);
    u.print(f.name.c_str());
    u.setFont(u8g2_font_helvR14_tf);
    u.setCursor(W - 20 - textW(f.title.c_str()), 36);
    u.print(f.title.c_str());
    g.fillRect(0, 50, W, 3, GxEPD_BLACK);

    // With the timeline strip the band and text lines move up to make room.
    const bool S = f.strip;
    const int16_t bandY = 62, bandH = S ? 180 : 200;
    const int16_t quipY = S ? 292 : 318, timeY = S ? 345 : 385;
    uint16_t bg = bandColor(f.band);
    uint16_t fg = (f.band == door::Band::None) ? GxEPD_BLACK : GxEPD_WHITE;
    if (f.band != door::Band::None) g.fillRect(0, bandY, W, bandH, bg);
    if (f.muted) {
      for (int i = 0; i < 3; i++) g.drawRect(20 + i, bandY + 10 + i, W - 40 - 2 * i, bandH - 20 - 2 * i, GxEPD_BLACK);
    }
    // With a mood face at the right end, the label shifts left into the remaining space.
    const bool face = f.mood != door::Mood::None;
    // "Current mood" caption above the face; the pair is centered in the band.
    const int16_t moodTop = bandY + (bandH - 152) / 2;
    const int16_t faceCx = W - 100, faceCy = moodTop + 24 + kMoodDiscRadius;
    const int16_t labelW = face ? W - 80 - 200 : W - 80;
    const int16_t labelCx = face ? (40 + W - 200) / 2 : W / 2;
    u.setForegroundColor(fg);
    u.setBackgroundColor(bg);
    fitFont(f.label.c_str(), kLabelFonts, sizeof(kLabelFonts) / sizeof(kLabelFonts[0]), labelW);
    int16_t asc = u.getFontAscent();
    centered(f.label.c_str(), labelCx, bandY + bandH / 2 + asc / 2);
    if (face) {
      u.setFont(u8g2_font_helvB12_tf);
      centered("Current mood", faceCx, moodTop + 14);
      drawMoodFace(g, f.mood, faceCx, faceCy);
    }

    // Quip
    u.setForegroundColor(GxEPD_BLACK);
    u.setBackgroundColor(GxEPD_WHITE);
    if (!f.quip.empty()) {
      fitFont(f.quip.c_str(), kQuipFonts, sizeof(kQuipFonts) / sizeof(kQuipFonts[0]), W - 40);
      centered(f.quip.c_str(), W / 2, quipY);
    }

    // Time line
    if (!f.timeLine.empty()) {
      u.setFont(u8g2_font_helvB24_tf);
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
      g.drawRect(tlX, tlY, tlW, tlH, GxEPD_BLACK);
      for (auto& b : f.blocks) {
        int16_t a = xFor(b.first), z = xFor(b.second);
        if (z > a) g.fillRect(a, tlY, z - a, tlH, GxEPD_BLACK);
      }
      u.setFont(u8g2_font_helvR12_tf);
      int span = (f.dayEnd - f.dayStart) / 60;
      int step = span > 12 ? 3 : 2;
      for (int h = (f.dayStart + 59) / 60; h * 60 <= f.dayEnd; h++) {
        int16_t x = xFor(h * 60);
        g.drawFastVLine(x, tlY + tlH, 5, GxEPD_BLACK);
        if ((h - (f.dayStart + 59) / 60) % step) continue;
        char lbl[4];
        int h12 = h % 12 == 0 ? 12 : h % 12;
        snprintf(lbl, sizeof(lbl), "%d%c", h12, (h % 24) < 12 ? 'a' : 'p');
        centered(lbl, x, tlY - 6);
      }
      if (f.nowMin >= f.dayStart && f.nowMin <= f.dayEnd) {
        // White halo keeps the red tick visible on top of a black block.
        int16_t nx = xFor(f.nowMin);
        g.fillRect(nx - 4, tlY - 2, 9, tlH + 4, GxEPD_WHITE);
        g.fillRect(nx - 2, tlY - 2, 5, tlH + 10, GxEPD_RED);
        g.fillTriangle(nx - 7, tlY + tlH + 14, nx + 7, tlY + tlH + 14, nx, tlY + tlH + 6, GxEPD_RED);
      }
    }

    // Footer (450..480)
    g.drawFastHLine(0, 450, W, GxEPD_BLACK);
    u.setFont(u8g2_font_helvR12_tf);
    std::string left = updated.empty() ? std::string() : "Updated " + updated;
    int16_t x = 10;
    // Small inverted tags: MANUAL for an active override, STALE when the broker is unreachable.
    auto tag = [&](const char* t, uint16_t tbg) {
      int16_t tw = textW(t) + 10;
      g.fillRect(x, 455, tw, 21, tbg);
      u.setForegroundColor(GxEPD_WHITE);
      u.setBackgroundColor(tbg);
      u.setCursor(x + 5, 471);
      u.print(t);
      u.setForegroundColor(GxEPD_BLACK);
      u.setBackgroundColor(GxEPD_WHITE);
      x += tw + 10;
    };
    // Today's date, centered and bold, so passers-by can see the sign is current.
    if (!f.date.empty()) {
      u.setFont(u8g2_font_helvB14_tf);
      centered(f.date.c_str(), W / 2, 472);
      u.setFont(u8g2_font_helvR12_tf);
    }
    if (f.sim) tag("PREVIEW", GxEPD_RED);
    if (f.manual) tag("MANUAL", GxEPD_BLACK);
    if (f.stale) tag("STALE", GxEPD_RED);
    u.setCursor(x, 472);
    u.print(left.c_str());
    u.setCursor(W - 10 - textW(f.footerRight.c_str()), 472);
    u.print(f.footerRight.c_str());
  }

  // Test screen: every mood face in a 4 x 4 grid, no labels, on white, red and
  // black rows to check them against each band color.
  void gallery() {
    g.fillScreen(GxEPD_WHITE);
    const uint16_t rowBg[4] = {GxEPD_WHITE, GxEPD_RED, GxEPD_BLACK, GxEPD_WHITE};
    for (int i = 1; i < (int)door::Mood::Count; i++) {
      int idx = i - 1, col = idx % 4, row = idx / 4;
      int16_t x0 = col * 200, y0 = row * 120;
      if (col == 0 && rowBg[row] != GxEPD_WHITE) g.fillRect(0, y0, W, 120, rowBg[row]);
      drawMoodFace(g, (door::Mood)i, x0 + 100, y0 + 60);
    }
  }

  void boot(const BootInfo& b) {
    g.fillScreen(GxEPD_WHITE);
    u.setFontMode(1);
    g.fillRect(0, 0, W, 110, GxEPD_RED);
    u.setForegroundColor(GxEPD_WHITE);
    u.setBackgroundColor(GxEPD_RED);
    u.setFont(u8g2_font_logisoso50_tr);
    centered("DOOR SIGN", W / 2, 80);

    u.setForegroundColor(GxEPD_BLACK);
    u.setBackgroundColor(GxEPD_WHITE);
    const int16_t lx = 60, vx = 260;
    int16_t y = 175;
    auto row = [&](const char* k, const std::string& v) {
      u.setFont(u8g2_font_helvR18_tf);
      u.setCursor(lx, y);
      u.print(k);
      u.setFont(u8g2_font_helvB24_tf);
      u.setCursor(vx, y);
      u.print(v.c_str());
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
    g.drawFastHLine(0, 450, W, GxEPD_BLACK);
    u.setFont(u8g2_font_helvR12_tf);
    u.setCursor(10, 472);
    u.print(("Firmware " + b.fw).c_str());
  }
};

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
  Painter p{display, u8g2};
  display.setFullWindow();
  display.firstPage();
  do {
    p.boot(b);
  } while (display.nextPage());
  display.hibernate();
  logf("[epd] boot screen drawn in %lu ms", (unsigned long)(millis() - t0));
}

static void renderTask(void*) {
  Painter p{display, u8g2};
  for (;;) {
    bool kicked = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(30000)) > 0;
    uint8_t fast;
    bool updating;
    {
      AppGuard g;
      fast = app.settings.fast;
      updating = app.updating;
    }
    Frame f;
    bool force, cleanup = false;
    xSemaphoreTake(gFrameLock, portMAX_DELAY);
    f = kicked ? gPending : gDrawn;
    force = kicked && gForce;
    if (kicked) gForce = false;
    xSemaphoreGive(gFrameLock);
    uint32_t h = f.hash();
    if (!kicked) {
      // Idle: after fast refreshes, restore red and clear ghosting once things are quiet.
      if (updating || gFastCount == 0 || millis() - gLastRenderMs < kCleanupMs || !gDrawnHash) continue;
      cleanup = true;
    } else if (h == gDrawnHash && !force) {
      continue;
    }

    door::Clock c = nowClock();
    if (f.sim && c.valid) {
      AppGuard g;  // a preview shows its own "Updated" time
      c = clockAt(c.now + app.simOffset);
    }
    std::string updated = c.valid ? door::formatTime(c.secOfDay) : "";
    const bool useFast = fast && !force && !cleanup && !f.gallery && !gDrawn.gallery && gDrawnHash &&
                         gFastCount < kFullEvery;
    logf("[epd] %s render '%s' / '%s'", useFast ? "fast" : cleanup ? "cleanup" : "full", f.label.c_str(),
         f.timeLine.c_str());
    gBusy = true;
    uint32_t t0 = millis();
    if (useFast) {
      auto& epd = display.epd2;
      if (epd.asleep()) gOldValid = false;
      epd.fastInit(fast);
      gFastDirty = true;
      if (!gOldValid) {
        xSemaphoreTake(gFrameLock, portMAX_DELAY);
        Frame prev = gDrawn;
        std::string prevUpdated = gDrawnUpdated;
        xSemaphoreGive(gFrameLock);
        writePlane(0x10, prev, prevUpdated);
      }
      writePlane(0x13, f, updated);
      uint32_t t1 = millis();
      epd.fastRefresh();
      logf("[epd] fast: data %lu ms, waveform %lu ms", (unsigned long)(t1 - t0), (unsigned long)(millis() - t1));
      gOldValid = true;  // the controller copied NEW to OLD
      gFastCount++;
    } else {
      if (gFastDirty) {
        display.epd2.resetController();
        gFastDirty = false;
      }
      display.setFullWindow();
      display.firstPage();
      do {
        if (f.gallery) p.gallery();
        else p.status(f, updated);
      } while (display.nextPage());
      gFastCount = 0;
      if (fast && !f.gallery) {
        // Stay awake and load this screen as the OLD image for the next fast refresh.
        display.powerOff();
        writePlane(0x10, f, updated);
        gOldValid = true;
      } else {
        display.hibernate();
        gOldValid = false;
      }
    }
    gLastMs = millis() - t0;
    gLastFast = useFast;
    gLastRenderMs = millis();
    gCount++;
    gDrawnHash = h;
    xSemaphoreTake(gFrameLock, portMAX_DELAY);
    gDrawn = f;
    gDrawnUpdated = updated;
    xSemaphoreGive(gFrameLock);
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
bool displayLastFast() { return gLastFast; }

// ---------- web preview ----------

namespace {

// A horizontal slice of the 800 x 480 screen in two bit planes. Drawing outside
// the slice is clipped cheaply, so the whole screen can be re-run per slice.
class StripCanvas : public Adafruit_GFX {
 public:
  static const int16_t kRows = 32;
  StripCanvas() : Adafruit_GFX(W, H) {}
  void begin(int16_t y0) {
    y0_ = y0;
    memset(k_, 0, sizeof(k_));
    memset(r_, 0, sizeof(r_));
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= W || y < y0_ || y >= y0_ + kRows) return;
    set(x, y - y0_, c);
  }
  void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) override {
    int16_t x1 = max<int16_t>(x, 0), x2 = min<int16_t>(x + w, W);
    int16_t y1 = max<int16_t>(y, y0_), y2 = min<int16_t>(y + h, y0_ + kRows);
    for (int16_t yy = y1; yy < y2; yy++)
      for (int16_t xx = x1; xx < x2; xx++) set(xx, yy - y0_, c);
  }
  void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) override { fillRect(x, y, w, 1, c); }
  void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t c) override { fillRect(x, y, 1, h, c); }
  void fillScreen(uint16_t c) override { fillRect(0, 0, W, H, c); }
  // 0 white, 1 black, 2 red
  uint8_t at(int16_t x, int16_t row) const {
    int i = row * (W / 8) + x / 8;
    uint8_t bit = 0x80 >> (x & 7);
    return (r_[i] & bit) ? 2 : (k_[i] & bit) ? 1 : 0;
  }
  int16_t y0() const { return y0_; }
  // Black or red ink bits for 8 pixels (MSB first).
  uint8_t ink(int16_t row, int16_t xb) const { return k_[row * (W / 8) + xb] | r_[row * (W / 8) + xb]; }

 private:
  void set(int16_t x, int16_t row, uint16_t c) {
    int i = row * (W / 8) + x / 8;
    uint8_t bit = 0x80 >> (x & 7);
    k_[i] &= ~bit;
    r_[i] &= ~bit;
    if (c == GxEPD_RED) r_[i] |= bit;
    else if (c == GxEPD_BLACK) k_[i] |= bit;
  }
  int16_t y0_ = -1000;
  uint8_t k_[W / 8 * kRows];
  uint8_t r_[W / 8 * kRows];
};

// 4-bit palette BMP, top-down: 14 + 40 + 16 * 4 header bytes, 400 bytes per row.
const size_t kBmpHeader = 14 + 40 + 64;
const size_t kBmpRow = W / 2;
const size_t kBmpSize = kBmpHeader + kBmpRow * H;

void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

void bmpHeader(uint8_t* h) {
  memset(h, 0, kBmpHeader);
  h[0] = 'B';
  h[1] = 'M';
  put32(h + 2, kBmpSize);
  put32(h + 10, kBmpHeader);
  put32(h + 14, 40);
  put32(h + 18, W);
  put32(h + 22, (uint32_t)(-(int32_t)H));  // negative height = top-down rows
  put16(h + 26, 1);
  put16(h + 28, 4);
  put32(h + 34, kBmpRow * H);
  put32(h + 46, 3);  // colors used
  uint8_t* pal = h + 54;  // B, G, R, 0
  const uint8_t colors[3][3] = {{255, 255, 255}, {20, 20, 20}, {40, 30, 200}};
  for (int i = 0; i < 3; i++) memcpy(pal + i * 4, colors[i], 3);
}

}  // namespace

// Streams frame f into controller RAM (0x10 OLD or 0x13 NEW) as one black/white
// plane, red drawn as black, rendered in 32-row strips.
static void writePlane(uint8_t cmd, const Frame& f, const std::string& updated) {
  static StripCanvas* cv = nullptr;  // 6.4 KB, kept once fast refresh is used
  static U8G2_FOR_ADAFRUIT_GFX u;
  if (!cv) {
    cv = new StripCanvas;
    u.begin(*cv);
  }
  Painter p{*cv, u};
  auto& epd = display.epd2;
  epd.planeBegin(cmd);
  uint8_t row[W / 8];
  for (int16_t y0 = 0; y0 < H; y0 += StripCanvas::kRows) {
    cv->begin(y0);
    if (f.gallery) p.gallery();
    else p.status(f, updated);
    for (int16_t r = 0; r < StripCanvas::kRows && y0 + r < H; r++) {
      for (int16_t xb = 0; xb < W / 8; xb++) row[xb] = ~cv->ink(r, xb);  // 1 = white
      epd.planeBytes(row, sizeof(row));
    }
  }
}

struct DisplayPreview::Impl {
  Frame frame;
  std::string updated;
  StripCanvas canvas;
  U8G2_FOR_ADAFRUIT_GFX u;
  uint8_t header[kBmpHeader];
};

DisplayPreview::DisplayPreview() : impl_(new Impl) {
  xSemaphoreTake(gFrameLock, portMAX_DELAY);
  impl_->frame = gDrawnHash ? gDrawn : gPending;  // nothing drawn yet: show what is queued
  impl_->updated = gDrawnUpdated;
  xSemaphoreGive(gFrameLock);
  impl_->u.begin(impl_->canvas);
  bmpHeader(impl_->header);
}

DisplayPreview::~DisplayPreview() { delete impl_; }

size_t DisplayPreview::size() { return kBmpSize; }

size_t DisplayPreview::fill(uint8_t* buf, size_t maxLen, size_t index) {
  size_t n = 0;
  while (n < maxLen && index < kBmpSize) {
    if (index < kBmpHeader) {
      buf[n++] = impl_->header[index++];
      continue;
    }
    size_t off = index - kBmpHeader;
    int16_t y = off / kBmpRow;
    int16_t x = (off % kBmpRow) * 2;
    StripCanvas& cv = impl_->canvas;
    if (y < cv.y0() || y >= cv.y0() + StripCanvas::kRows) {
      cv.begin(y - y % StripCanvas::kRows);
      Painter p{cv, impl_->u};
      if (impl_->frame.gallery) p.gallery();
      else p.status(impl_->frame, impl_->updated);
    }
    int16_t row = y - cv.y0();
    buf[n++] = (cv.at(x, row) << 4) | cv.at(x + 1, row);
    index++;
  }
  return n;
}
