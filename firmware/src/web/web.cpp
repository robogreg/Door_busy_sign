#include "web.h"

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>

#include "app.h"
#include "broker/broker.h"
#include "config.h"
#include "display/display.h"
#include "storage/storage.h"
#include "ui.h"

static AsyncWebServer server(80);
static const size_t kMaxCustom = 32;

// ---------- helpers ----------

static String param(AsyncWebServerRequest* r, const char* name, bool post = false) {
  if (!r->hasParam(name, post)) return String();
  return r->getParam(name, post)->value();
}

static bool wantsJson(AsyncWebServerRequest* r) {
  if (r->hasParam("json")) return true;
  if (!r->hasHeader("Accept")) return false;
  return r->getHeader("Accept")->value().indexOf("application/json") >= 0;
}

static String cookiePin(AsyncWebServerRequest* r) {
  if (!r->hasHeader("Cookie")) return String();
  String c = r->getHeader("Cookie")->value();
  int i = c.indexOf("dsk=");
  if (i < 0) return String();
  int e = c.indexOf(';', i);
  return c.substring(i + 4, e < 0 ? c.length() : e);
}

static bool authorized(AsyncWebServerRequest* r) {
  std::string pin;
  {
    AppGuard g;
    pin = app.settings.pin;
  }
  String k = param(r, "k");
  if (!k.length()) k = param(r, "k", true);
  if (!k.length()) k = cookiePin(r);
  return k.length() && pin == k.c_str();
}

static void sendJson(AsyncWebServerRequest* r, int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  r->send(code, "application/json", out);
}

static void sendPage(AsyncWebServerRequest* r, int code, const String& msg) {
  String html = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>Door Sign</title><style>body{font-family:system-ui,sans-serif;margin:2rem;"
                  "font-size:1.4rem}a{color:#d4570f}</style></head><body><p>");
  html += msg;
  html += F("</p><p><a href='/'>Open control page</a></p></body></html>");
  r->send(code, "text/html", html);
}

static void denied(AsyncWebServerRequest* r) {
  if (wantsJson(r)) {
    JsonDocument d;
    d["error"] = "unauthorized";
    sendJson(r, 401, d);
  } else {
    sendPage(r, 401, "Wrong or missing PIN.");
  }
}

static void fillStatus(JsonDocument& d) {
  AppGuard g;
  const door::Status& st = app.status;
  d["state"] = door::stateId(st.state);
  d["label"] = st.label;
  d["band"] = door::bandId(st.band);
  d["manual"] = st.manual;
  d["until"] = st.until;
  d["timeLine"] = app.timeLine;
  d["quip"] = app.quip;
  d["stale"] = app.stale;
  d["mood"] = app.mood == door::Mood::None ? "" : door::moodId(app.mood);
  d["moodName"] = app.mood == door::Mood::None ? "" : door::moodName(app.mood);
  d["calendar"] = app.cal.valid;
  JsonArray blocks = d["blocks"].to<JsonArray>();
  for (auto& b : app.cal.blocks) {
    JsonArray p = blocks.add<JsonArray>();
    p.add(b.first);
    p.add(b.second);
  }
  d["name"] = app.settings.name;
  d["title"] = app.settings.title;
  bool sim = simActive();
  d["sim"] = sim;
  if (sim) {
    door::Clock r = nowClock();
    if (r.valid) {
      door::Clock s = clockAt(r.now + app.simOffset);
      d["simAt"] = s.now;
      d["simLeft"] = (int32_t)(app.simUntil - millis()) / 1000;
    }
  }
}

// ---------- override commands ----------

// Applies /s/<cmd>. Returns a human-readable confirmation or sets err.
static bool applyCommand(const String& cmd, AsyncWebServerRequest* r, String& msg) {
  door::Clock c = nowClock();
  if (cmd == "clear") {
    {
      AppGuard g;
      app.ovr = door::Override{};
      app.newQuip = true;
    }
    saveOverride();
    logf("[web] override cleared");
    msg = "Back to calendar.";
    return true;
  }
  door::State s;
  if (!door::parseState(cmd.c_str(), s) || s == door::State::Gone) {
    msg = "Unknown state.";
    return false;
  }
  door::Duration d;
  String ds = param(r, "d");
  if (!door::parseDuration(ds.c_str(), d)) {
    msg = "Bad duration. Use minutes (1 to 1440), eod, event or clear.";
    return false;
  }
  door::Override o;
  o.active = true;
  o.state = s;
  door::CalendarData cal;
  {
    AppGuard g;
    cal = app.cal;
  }
  o.expires = door::computeExpiry(s, d, c, &cal);
  if (s == door::State::Custom) {
    String m = param(r, "m");
    m.trim();
    if (!m.length()) {
      msg = "Custom message is empty.";
      return false;
    }
    if (m.length() > kMaxCustom) m = m.substring(0, kMaxCustom);
    o.text = m.c_str();
    String cs = param(r, "c");
    o.band = door::Band::Black;
    if (cs.length() && !door::parseBand(cs.c_str(), o.band)) {
      msg = "Bad color. Use red, black or none.";
      return false;
    }
  }
  {
    AppGuard g;
    app.ovr = o;
    app.newQuip = true;
  }
  saveOverride();
  std::string label = s == door::State::Custom ? o.text : door::stateLabel(s);
  msg = label.c_str();
  if (o.expires) msg += (" until " + door::formatWhen(c, o.expires)).c_str();
  else msg += " until cleared";
  logf("[web] override %s, expires %lld", door::stateId(s), (long long)o.expires);
  return true;
}

static void handleCommand(AsyncWebServerRequest* r, const String& cmd) {
  if (!authorized(r)) return denied(r);
  String msg;
  bool ok = applyCommand(cmd, r, msg);
  if (wantsJson(r)) {
    JsonDocument d;
    d["ok"] = ok;
    d["message"] = msg;
    sendJson(r, ok ? 200 : 400, d);
  } else {
    sendPage(r, ok ? 200 : 400,
             ok ? msg + ".<br><small>The sign updates in about 30 seconds.</small>" : msg);
  }
}

// ---------- mood ----------

// /m/<mood>?k=PIN or /m/clear. Independent of status overrides; clears each morning.
static void handleMood(AsyncWebServerRequest* r, const char* id) {
  if (!authorized(r)) return denied(r);
  door::Mood m = door::Mood::None;
  bool ok = strcmp(id, "clear") == 0 || door::parseMood(id, m);
  String msg;
  if (ok) {
    door::Clock c = nowClock();
    {
      AppGuard g;
      app.mood = m;
      app.moodSetAt = c.valid ? c.now : 0;
    }
    saveMood();
    msg = m == door::Mood::None ? "Mood cleared" : String("Mood: ") + door::moodName(m);
    logf("[web] mood %s", door::moodId(m));
  } else {
    msg = "Unknown mood.";
  }
  if (wantsJson(r)) {
    JsonDocument d;
    d["ok"] = ok;
    d["message"] = msg;
    sendJson(r, ok ? 200 : 400, d);
  } else {
    sendPage(r, ok ? 200 : 400, ok ? msg + ".<br><small>The sign updates in about 30 seconds.</small>" : msg);
  }
}

// ---------- settings ----------

static bool parseHHMM(const String& s, uint16_t& out) {
  int colon = s.indexOf(':');
  if (colon < 1) return false;
  int h = s.substring(0, colon).toInt(), m = s.substring(colon + 1).toInt();
  if (h < 0 || h > 24 || m < 0 || m > 59 || h * 60 + m > 1440) return false;
  out = h * 60 + m;
  return true;
}

static void handleGetSettings(AsyncWebServerRequest* r) {
  if (!authorized(r)) return denied(r);
  JsonDocument d;
  {
    AppGuard g;
    const Settings& s = app.settings;
    d["name"] = s.name;
    d["title"] = s.title;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02u:%02u", s.hours.startMin / 60, s.hours.startMin % 60);
    d["ws"] = buf;
    snprintf(buf, sizeof(buf), "%02u:%02u", s.hours.endMin / 60, s.hours.endMin % 60);
    d["we"] = buf;
    d["days"] = s.hours.days;
    d["humor"] = s.humor;
    d["defaultPin"] = s.pin == "1234";
    d["brokerUrl"] = s.brokerUrl;
    d["tokenSet"] = !s.brokerToken.empty();  // never echoed back
    d["cals"] = s.cals;
  }
  sendJson(r, 200, d);
}

static void handlePostSettings(AsyncWebServerRequest* r) {
  if (!authorized(r)) return denied(r);
  Settings s;
  {
    AppGuard g;
    s = app.settings;
  }
  String v;
  if ((v = param(r, "name", true)).length()) s.name = v.substring(0, 32).c_str();
  if (r->hasParam("title", true)) s.title = param(r, "title", true).substring(0, 40).c_str();
  uint16_t t;
  if ((v = param(r, "ws", true)).length() && parseHHMM(v, t)) s.hours.startMin = t;
  if ((v = param(r, "we", true)).length() && parseHHMM(v, t)) s.hours.endMin = t;
  if ((v = param(r, "days", true)).length()) s.hours.days = (uint8_t)(v.toInt() & 0x7F);
  if ((v = param(r, "humor", true)).length()) s.humor = (uint8_t)constrain(v.toInt(), 0, 2);
  bool pinChanged = false;
  if ((v = param(r, "pin", true)).length()) {
    if (v.length() < 4 || v.length() > 12 || v.indexOf(';') >= 0 || v.indexOf(' ') >= 0) {
      JsonDocument d;
      d["error"] = "PIN must be 4 to 12 characters, no spaces or semicolons.";
      return sendJson(r, 400, d);
    }
    s.pin = v.c_str();
    pinChanged = true;
  }
  if (r->hasParam("brokerUrl", true)) {
    v = param(r, "brokerUrl", true);
    v.trim();
    if (v.length() && !v.startsWith("https://")) {
      JsonDocument d;
      d["error"] = "Broker URL must start with https://";
      return sendJson(r, 400, d);
    }
    s.brokerUrl = v.c_str();
  }
  if ((v = param(r, "token", true)).length()) {
    v.trim();
    s.brokerToken = v == "-" ? "" : v.c_str();  // "-" clears it
  }
  if (r->hasParam("cals", true)) {
    v = param(r, "cals", true);
    v.trim();
    s.cals = v.length() ? v.c_str() : "primary:mine";
  }
  if (s.hours.endMin <= s.hours.startMin) {
    JsonDocument d;
    d["error"] = "Working hours end must be after start.";
    return sendJson(r, 400, d);
  }
  bool brokerChanged;
  {
    AppGuard g;
    bool humorChanged = app.settings.humor != s.humor;
    brokerChanged = app.settings.brokerUrl != s.brokerUrl || app.settings.brokerToken != s.brokerToken ||
                    app.settings.cals != s.cals;
    app.settings = s;
    if (humorChanged) app.newQuip = true;
    if (brokerChanged) {
      app.cal = door::CalendarData{};  // don't show data from the old calendar set
      app.broker.lastOk = 0;
      app.broker.lastError.clear();
    }
  }
  saveSettings();
  if (brokerChanged) brokerKick();
  logf("[web] settings saved%s", pinChanged ? " (PIN changed)" : "");
  JsonDocument d;
  d["ok"] = true;
  AsyncWebServerResponse* resp;
  String out;
  serializeJson(d, out);
  resp = r->beginResponse(200, "application/json", out);
  if (pinChanged) resp->addHeader("Set-Cookie", ("dsk=" + s.pin + "; Path=/; Max-Age=31536000").c_str());
  r->send(resp);
}

// ---------- system ----------

static void handleSystem(AsyncWebServerRequest* r) {
  JsonDocument d;
  d["fw"] = FW_VERSION;
  d["ip"] = WiFi.localIP().toString();
  d["mac"] = WiFi.macAddress();
  d["ssid"] = WiFi.SSID();
  d["rssi"] = WiFi.RSSI();
  d["host"] = MDNS_HOST ".local";
  d["uptime"] = millis() / 1000;
  d["heap"] = ESP.getFreeHeap();
  d["heapMin"] = ESP.getMinFreeHeap();
  d["renderMs"] = displayLastMs();
  d["renders"] = displayCount();
  d["rendering"] = displayBusy();
  door::Clock c = nowClock();
  d["timeValid"] = c.valid;
  d["time"] = c.valid ? door::formatTime(c.secOfDay) : "";
  {
    AppGuard g;
    JsonObject b = d["broker"].to<JsonObject>();
    b["configured"] = !app.settings.brokerUrl.empty() && !app.settings.brokerToken.empty();
    b["lastPollAgo"] = app.broker.lastPollMs ? (millis() - app.broker.lastPollMs) / 1000 : -1;
    b["lastOkAgo"] = (app.broker.lastOk && c.valid) ? (long)(c.now - app.broker.lastOk) : -1;
    b["error"] = app.broker.lastError;
    b["response"] = app.broker.lastResponse;
  }
  if (authorized(r)) d["log"] = logDump();
  sendJson(r, 200, d);
}

// Calendar picker. ?refresh=1 asks the broker task to fetch the list; the UI polls.
static void handleCalendars(AsyncWebServerRequest* r) {
  if (!authorized(r)) return denied(r);
  if (param(r, "refresh") == "1") {
    {
      AppGuard g;
      app.broker.calsState = 1;
    }
    brokerKick();
    return r->send(200, "application/json", "{\"state\":\"pending\"}");
  }
  String out;
  {
    AppGuard g;
    switch (app.broker.calsState) {
      case 1: out = "{\"state\":\"pending\"}"; break;
      case 2: out = String("{\"state\":\"ready\",\"data\":") + app.broker.calsJson.c_str() + "}"; break;
      case 3: {
        JsonDocument d;
        d["state"] = "error";
        d["error"] = app.broker.calsJson;
        serializeJson(d, out);
        break;
      }
      default: out = "{\"state\":\"idle\"}";
    }
  }
  r->send(200, "application/json", out);
}

void webBegin() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    AsyncWebServerResponse* resp = r->beginResponse(200, "text/html", (const uint8_t*)kIndexHtml, strlen_P(kIndexHtml));
    resp->addHeader("Cache-Control", "no-cache");
    r->send(resp);
  });

  server.on("/status.json", HTTP_GET, [](AsyncWebServerRequest* r) {
    JsonDocument d;
    fillStatus(d);
    d["renders"] = displayCount();  // changes when the panel shows something new
    d["rendering"] = displayBusy();
    sendJson(r, 200, d);
  });

  // What the panel shows right now, as an image. Rendered on demand in slices.
  server.on("/sign.bmp", HTTP_GET, [](AsyncWebServerRequest* r) {
    auto pv = std::make_shared<DisplayPreview>();
    AsyncWebServerResponse* resp = r->beginResponse(
        "image/bmp", DisplayPreview::size(),
        [pv](uint8_t* buf, size_t maxLen, size_t index) -> size_t { return pv->fill(buf, maxLen, index); });
    resp->addHeader("Cache-Control", "no-store");
    r->send(resp);
  });

  // One-tap endpoints: /s/<state>?d=&k=  (gone is automatic only)
  static const char* kCmds[] = {"available", "meeting", "call", "focus", "busy", "away", "custom", "clear"};
  for (const char* cmd : kCmds) {
    String path = String("/s/") + cmd;
    server.on(path.c_str(), HTTP_GET, [cmd](AsyncWebServerRequest* r) { handleCommand(r, cmd); });
  }

  // Mood endpoints: /m/<mood>?k=  and /m/clear?k=
  for (int i = 1; i <= (int)door::Mood::Count; i++) {
    // moodId() returns string literals, safe to capture; the extra slot is "clear".
    const char* cid = i < (int)door::Mood::Count ? door::moodId((door::Mood)i) : "clear";
    server.on((String("/m/") + cid).c_str(), HTTP_GET, [cid](AsyncWebServerRequest* r) { handleMood(r, cid); });
  }

  server.on("/api/gallery", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!authorized(r)) return denied(r);
    {
      AppGuard g;
      app.galleryUntil = millis() + 180000;  // 3 minutes, then back to status
    }
    r->send(200, "application/json", "{\"ok\":true}");
  });

  // Time preview: POST at=YYYY-MM-DDTHH:MM (local) for 30 min, or clear=1.
  server.on("/api/sim", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!authorized(r)) return denied(r);
    if (param(r, "clear", true) == "1") {
      {
        AppGuard g;
        app.simUntil = 0;
        app.simOffset = 0;
        app.simCal = door::CalendarData{};
      }
      logf("[sim] time preview cleared");
      brokerKick();
      r->send(200, "application/json", "{\"ok\":true,\"message\":\"Back to the real time\"}");
      return;
    }
    String at = param(r, "at", true);
    struct tm tm = {};
    int y, mo, dd, hh, mi;
    if (sscanf(at.c_str(), "%4d-%2d-%2dT%2d:%2d", &y, &mo, &dd, &hh, &mi) != 5 || mo < 1 || mo > 12 || dd < 1 ||
        dd > 31 || hh > 23 || mi > 59) {
      r->send(400, "application/json", "{\"error\":\"Pick a date and time\"}");
      return;
    }
    door::Clock now = nowClock();
    if (!now.valid) {
      r->send(409, "application/json", "{\"error\":\"Clock not synced yet\"}");
      return;
    }
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = dd;
    tm.tm_hour = hh;
    tm.tm_min = mi;
    tm.tm_isdst = -1;  // let the TZ rules decide
    int64_t t = (int64_t)mktime(&tm);
    if (t < now.now - 90LL * 86400 || t > now.now + 90LL * 86400) {
      r->send(400, "application/json", "{\"error\":\"Pick a time within 90 days\"}");
      return;
    }
    {
      AppGuard g;
      app.simOffset = t - now.now;
      app.simUntil = millis() + 30UL * 60 * 1000;
      if (!app.simUntil) app.simUntil = 1;
      app.simCal = door::CalendarData{};
    }
    logf("[sim] previewing %s", at.c_str());
    brokerKick();
    r->send(200, "application/json", "{\"ok\":true,\"message\":\"Previewing that time for 30 min\"}");
  });

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!authorized(r)) return denied(r);
    String k = param(r, "k");
    AsyncWebServerResponse* resp = r->beginResponse(200, "application/json", "{\"ok\":true}");
    resp->addHeader("Set-Cookie", "dsk=" + k + "; Path=/; Max-Age=31536000");
    r->send(resp);
  });

  server.on("/api/settings", HTTP_GET, handleGetSettings);
  server.on("/api/settings", HTTP_POST, handlePostSettings);
  server.on("/api/system", HTTP_GET, handleSystem);
  server.on("/api/calendars", HTTP_GET, handleCalendars);

  server.on("/api/refresh", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!authorized(r)) return denied(r);
    {
      AppGuard g;
      app.forceRender = true;
    }
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* r) {
    if (!authorized(r)) return denied(r);
    {
      AppGuard g;
      app.reboot = true;
    }
    r->send(200, "application/json", "{\"ok\":true}");
  });

  server.onNotFound([](AsyncWebServerRequest* r) { r->send(404, "text/plain", "Not found"); });
  server.begin();
  logf("[web] server on port 80");
}
