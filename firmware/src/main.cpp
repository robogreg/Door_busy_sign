// Door Sign firmware: M2 manual-only sign.
//
// loop() resolves the status every 500 ms (override > working hours), builds a
// Frame, and hands it to the render task, which only touches the panel when the
// frame hash changes. Web handlers run on the AsyncTCP task.

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <time.h>

#include "app.h"
#include "broker/broker.h"
#include "config.h"
#include "display/display.h"
#include "status.h"
#include "storage/storage.h"
#include "web/web.h"

#if __has_include("secrets.h")
#include "secrets.h"
#define HAVE_DEV_WIFI 1
#else
#define HAVE_DEV_WIFI 0
#endif

static uint32_t gBootHoldUntil = 0;
static std::string gQuipKey;

static bool joinWifi() {
#if HAVE_DEV_WIFI
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
    // 201 no AP found, 202/15 auth fail or handshake timeout (usually wrong password)
    logf("[net] disconnected, reason %u", info.wifi_sta_disconnected.reason);
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(MDNS_HOST);
  WiFi.setAutoReconnect(true);
  // Mesh networks: scan all channels and join the strongest node.
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  logf("[net] MAC %s, joining '%s'", WiFi.macAddress().c_str(), DEV_WIFI_SSID);
  WiFi.begin(DEV_WIFI_SSID, DEV_WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) delay(250);
  if (WiFi.status() != WL_CONNECTED) {
    logf("[net] Wi-Fi join failed after 30 s, will keep retrying");
    return false;
  }
  logf("[net] connected in %lu ms: SSID %s IP %s RSSI %d", (unsigned long)(millis() - t0),
       WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
#else
  WiFi.mode(WIFI_STA);
  logf("[net] no secrets.h, Wi-Fi skipped. MAC %s", WiFi.macAddress().c_str());
  return false;
#endif
}

static void startTime() {
  std::string tz;
  {
    AppGuard g;
    tz = app.settings.tz;
  }
  configTzTime(tz.c_str(), "pool.ntp.org", "time.google.com");
  uint32_t t0 = millis();
  while (!nowClock().valid && millis() - t0 < 10000) delay(100);
  door::Clock c = nowClock();
  if (c.valid) logf("[time] synced, local %s", door::formatTime(c.secOfDay).c_str());
  else logf("[time] not synced yet, timed overrides wait for NTP");
}

// "Sunday, Nov 22", or empty before NTP sync.
static std::string dateString(int64_t t64) {
  time_t t = (time_t)t64;
  struct tm lt;
  if (t < 1700000000 || !localtime_r(&t, &lt)) return "";
  static const char* const kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  char buf[24];
  snprintf(buf, sizeof(buf), "%s, %s %d", kDays[lt.tm_wday], kMonths[lt.tm_mon], lt.tm_mday);
  return buf;
}

static Frame buildFrame() {
  Frame f;
  AppGuard g;

  // Time preview: end it when its window runs out.
  if (app.simUntil && !simActive()) {
    logf("[sim] time preview ended");
    app.simUntil = 0;
    app.simOffset = 0;
    app.simCal = door::CalendarData{};
  }
  const bool sim = app.simUntil != 0;
  const door::Clock real = nowClock();
  const door::Clock c = sim && real.valid ? clockAt(real.now + app.simOffset) : real;

  // Expire a finished override; clear the mood each morning. During a preview
  // these are only applied to the picture, never saved.
  door::Override ovr = app.ovr;
  door::Mood mood = app.mood;
  if (ovr.active && !door::overrideActive(ovr, c)) {
    ovr = door::Override{};
    if (!sim) {
      logf("[state] override %s expired", door::stateId(app.ovr.state));
      app.ovr = ovr;
      saveOverride();
    }
  }
  if (mood != door::Mood::None && door::moodExpired(app.moodSetAt, c, app.settings.hours)) {
    mood = door::Mood::None;
    if (!sim) {
      logf("[state] mood %s cleared for the new day", door::moodId(app.mood));
      app.mood = mood;
      app.moodSetAt = 0;
      saveMood();
    }
  }
  const door::CalendarData& cal = sim ? app.simCal : app.cal;

  door::Status st = door::resolve(c, ovr, app.settings.hours, cal);
  std::string key = std::string(door::stateId(st.state)) + "|" + st.label;
  if (key != gQuipKey || app.newQuip) {
    app.quip = pickQuip(st.state, app.settings.humor);
    if (key != gQuipKey) logf("[state] now %s", key.c_str());
    gQuipKey = key;
    app.newQuip = false;
  }
  app.status = st;
  app.timeLine = door::timeLine(st, c);

  f.name = app.settings.name;
  f.title = app.settings.title;
  f.label = st.label;
  f.band = st.band;
  // A non-private calendar event shows its title where the quip would go.
  f.quip = st.detail.empty() ? app.quip : st.detail;
  f.timeLine = app.timeLine;
  f.manual = st.manual;
  f.muted = st.state == door::State::Away || st.state == door::State::Gone;
  f.mood = mood;
  f.sim = sim;
  f.date = dateString(c.now);
  f.footerRight = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "no Wi-Fi";

  // Calendar extras: stale icon and the timeline strip.
  bool configured = !app.settings.brokerUrl.empty() && !app.settings.brokerToken.empty();
  const BrokerState& b = app.broker;
  app.stale = configured && real.valid &&
              (b.lastOk ? (real.now - b.lastOk > door::kStaleAfter) : millis() > door::kStaleAfter * 1000UL);
  f.stale = app.stale;
  int64_t midnight = c.now - c.secOfDay;
  f.strip = configured && c.valid && cal.valid && cal.fetchedAt >= midnight &&
            c.now - cal.fetchedAt <= door::kCalendarMaxAge;
  if (f.strip) {
    f.dayStart = app.settings.hours.startMin;
    f.dayEnd = app.settings.hours.endMin;
    f.blocks = cal.blocks;
    f.nowMin = (int16_t)((c.secOfDay / 60) / 15 * 15);
  }
  return f;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  logf("=== Door Sign FW %s ===", FW_VERSION);

  storageBegin();
  loadSettings();
  loadOverride();
  loadMood();
  loadQuips();
  displayBegin();

  bool online = joinWifi();
  if (online && MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    logf("[net] mDNS: http://%s.local/", MDNS_HOST);
  }
  startTime();
  buildFrame();  // so /status.json is right while the boot screen draws
  webBegin();
  brokerBegin();

  BootInfo b;
  b.online = online;
  b.ssid = WiFi.SSID().c_str();
  b.ip = WiFi.localIP().toString().c_str();
  b.mac = WiFi.macAddress().c_str();
  b.host = MDNS_HOST;
  b.fw = FW_VERSION;
  drawBootScreen(b);  // blocking, about 27 s; web server already answering
  gBootHoldUntil = millis() + 20000;
  displayStartTask();

  // Watchdog on loop(): it never blocks (rendering lives on its own task).
  esp_task_wdt_init(30, true);
  esp_task_wdt_add(nullptr);
  logf("[sys] ready, heap %u", ESP.getFreeHeap());
}

void loop() {
  esp_task_wdt_reset();
  static uint32_t last = 0;
  if (millis() - last >= 500) {
    last = millis();
    Frame f = buildFrame();
    bool force, reboot, gallery;
    {
      AppGuard g;
      force = app.forceRender;
      app.forceRender = false;
      reboot = app.reboot;
      gallery = app.galleryUntil && (int32_t)(app.galleryUntil - millis()) > 0;
      if (!gallery) app.galleryUntil = 0;
    }
    if (gallery) {
      Frame gf;
      gf.gallery = true;
      displayRequest(gf);
    } else if ((int32_t)(millis() - gBootHoldUntil) >= 0) {
      displayRequest(f, force);
    }
    if (reboot) {
      logf("[sys] reboot requested");
      delay(300);
      ESP.restart();
    }
  }
  delay(10);
}
