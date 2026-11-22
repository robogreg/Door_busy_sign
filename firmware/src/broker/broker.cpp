#include "broker.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "app.h"
#include "google_roots.h"

static TaskHandle_t gTask = nullptr;
static const uint32_t kPollMs = 60000;

static String urlEncode(const std::string& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  for (unsigned char c : s) {
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += (char)c;
    else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

bool brokerConfigured() {
  AppGuard g;
  return !app.settings.brokerUrl.empty() && !app.settings.brokerToken.empty();
}

void brokerKick() {
  if (gTask) xTaskNotifyGive(gTask);
}

// GET <url>?token=..&action=..[&cals=..]. Returns HTTP body or sets err.
static bool brokerGet(const char* action, std::string& body, std::string& err) {
  std::string base, token, cals;
  {
    AppGuard g;
    base = app.settings.brokerUrl;
    token = app.settings.brokerToken;
    cals = app.settings.cals;
  }
  String url = base.c_str();
  url += url.indexOf('?') >= 0 ? "&" : "?";
  url += "token=" + urlEncode(token) + "&action=" + action;
  if (strcmp(action, "status") == 0) url += "&cals=" + urlEncode(cals);

  WiFiClientSecure client;
  client.setCACert(kGoogleRootsPem);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // Apps Script 302s to googleusercontent.com
  http.setTimeout(15000);
  http.setConnectTimeout(10000);
  if (!http.begin(client, url)) {
    err = "bad broker URL";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    err = code < 0 ? std::string("HTTP error: ") + http.errorToString(code).c_str() : "HTTP " + std::to_string(code);
    http.end();
    return false;
  }
  body = http.getString().c_str();
  http.end();
  return true;
}

static bool parseStatus(const std::string& body, door::CalendarData& out, std::string& err) {
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, body)) {
    err = std::string("bad JSON: ") + e.c_str();
    return false;
  }
  if (doc["error"].is<const char*>()) {
    err = std::string("broker: ") + doc["error"].as<const char*>();
    return false;
  }
  door::CalendarData d;
  if (!door::parseState(doc["state"] | "", d.state) || d.state == door::State::Gone || d.state == door::State::Custom) {
    err = "broker: unknown state";
    return false;
  }
  const char* until = doc["until"] | (const char*)nullptr;
  if (until && !door::parseIso8601(until, d.until)) {
    err = "broker: bad until";
    return false;
  }
  JsonObject next = doc["next"].as<JsonObject>();
  if (!next.isNull()) {
    d.hasNext = door::parseState(next["state"] | "", d.nextState) && door::parseIso8601(next["at"] | "", d.nextAt);
  }
  for (JsonArray b : doc["blocks"].as<JsonArray>()) {
    uint16_t a, z;
    if (door::parseHHMM(b[0] | "", a) && door::parseHHMM(b[1] | "", z) && z > a) d.blocks.push_back({a, z});
  }
  const char* title = doc["title"] | (const char*)nullptr;
  if (title && *title) {
    d.title.assign(title, strnlen(title, 80));
    const char* tu = doc["titleUntil"] | (const char*)nullptr;
    if (tu) door::parseIso8601(tu, d.titleUntil);
  }
  d.valid = true;
  out = d;
  return true;
}

static void pollStatus() {
  std::string body, err;
  bool ok = brokerGet("status", body, err);
  door::CalendarData d;
  if (ok) ok = parseStatus(body, d, err);
  door::Clock c = nowClock();
  AppGuard g;
  app.broker.lastPollMs = millis();
  if (ok) {
    d.fetchedAt = c.valid ? c.now : 0;
    bool changed = !app.cal.valid || app.cal.state != d.state || app.cal.until != d.until;
    app.cal = d;
    app.broker.lastOk = d.fetchedAt;
    app.broker.lastError.clear();
    app.broker.lastResponse = body;
    if (changed) logf("[cal] %s until %lld, %u blocks", door::stateId(d.state), (long long)d.until, (unsigned)d.blocks.size());
  } else {
    if (app.broker.lastError != err) logf("[cal] poll failed: %s", err.c_str());
    app.broker.lastError = err;
  }
}

static void pollCalendars() {
  std::string body, err;
  bool ok = brokerGet("calendars", body, err);
  if (ok) {
    JsonDocument doc;
    if (deserializeJson(doc, body)) ok = false, err = "bad JSON";
    else if (doc["error"].is<const char*>()) ok = false, err = std::string("broker: ") + doc["error"].as<const char*>();
  }
  AppGuard g;
  app.broker.calsState = ok ? 2 : 3;
  app.broker.calsJson = ok ? body : err;
  logf("[cal] calendar list %s", ok ? "loaded" : err.c_str());
}

static void brokerTask(void*) {
  uint32_t last = 0;
  bool first = true;
  for (;;) {
    bool kicked = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0;
    if (WiFi.status() != WL_CONNECTED || !brokerConfigured()) continue;
    bool wantCals;
    {
      AppGuard g;
      wantCals = app.broker.calsState == 1;
    }
    if (wantCals) pollCalendars();
    if (first || kicked || millis() - last >= kPollMs) {
      first = false;
      last = millis();
      pollStatus();
    }
  }
}

void brokerBegin() {
  // TLS handshakes need a deep stack.
  xTaskCreatePinnedToCore(brokerTask, "broker", 12288, nullptr, 1, &gTask, 0);
}
