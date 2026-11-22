#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

#include <vector>

#include "app.h"
#include "default_quips.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif

static const char* kQuipPath = "/quips.json";
static bool gFsOk = false;

void storageBegin() {
  gFsOk = LittleFS.begin(true);  // format on first use
  if (!gFsOk) {
    logf("[fs] LittleFS mount failed, using built-in quips");
    return;
  }
  if (!LittleFS.exists(kQuipPath)) {
    File f = LittleFS.open(kQuipPath, "w");
    if (f) {
      f.print(FPSTR(kDefaultQuips));
      f.close();
      logf("[fs] seeded %s", kQuipPath);
    }
  }
}

// ---------- settings ----------

static std::string getStr(Preferences& p, const char* key, const std::string& def) {
  if (!p.isKey(key)) return def;
  return std::string(p.getString(key, def.c_str()).c_str());
}

void loadSettings() {
  Preferences p;
  p.begin("door", true);
  AppGuard g;
  Settings& s = app.settings;
#ifdef DEV_PIN
  s.pin = DEV_PIN;
#endif
  s.name = getStr(p, "name", s.name);
  s.title = getStr(p, "title", s.title);
  s.pin = getStr(p, "pin", s.pin);
  s.tz = getStr(p, "tz", s.tz);
  s.hours.startMin = p.getUShort("ws", s.hours.startMin);
  s.hours.endMin = p.getUShort("we", s.hours.endMin);
  s.hours.days = p.getUChar("days", s.hours.days);
  s.humor = p.getUChar("humor", s.humor);
  s.brokerUrl = getStr(p, "burl", s.brokerUrl);
  s.brokerToken = getStr(p, "btok", s.brokerToken);
  s.cals = getStr(p, "cals", s.cals);
  p.end();
}

void saveSettings() {
  Preferences p;
  p.begin("door", false);
  AppGuard g;
  const Settings& s = app.settings;
  p.putString("name", s.name.c_str());
  p.putString("title", s.title.c_str());
  p.putString("pin", s.pin.c_str());
  p.putString("tz", s.tz.c_str());
  p.putUShort("ws", s.hours.startMin);
  p.putUShort("we", s.hours.endMin);
  p.putUChar("days", s.hours.days);
  p.putUChar("humor", s.humor);
  p.putString("burl", s.brokerUrl.c_str());
  p.putString("btok", s.brokerToken.c_str());
  p.putString("cals", s.cals.c_str());
  p.end();
}

// ---------- override ----------

void loadOverride() {
  Preferences p;
  p.begin("ovr", true);
  AppGuard g;
  door::Override& o = app.ovr;
  o.active = p.getBool("on", false);
  o.state = (door::State)p.getUChar("st", 0);
  if ((int)o.state >= (int)door::State::Count) o.active = false;
  o.expires = p.getLong64("exp", 0);
  o.band = (door::Band)p.getUChar("band", 0);
  o.text = getStr(p, "txt", "");
  p.end();
}

void saveOverride() {
  Preferences p;
  p.begin("ovr", false);
  AppGuard g;
  const door::Override& o = app.ovr;
  p.putBool("on", o.active);
  p.putUChar("st", (uint8_t)o.state);
  p.putLong64("exp", o.expires);
  p.putUChar("band", (uint8_t)o.band);
  p.putString("txt", o.text.c_str());
  p.end();
}

// ---------- quips ----------

struct Quip {
  std::string text;
  bool dry;
};
static std::vector<Quip> gQuips[(int)door::State::Count];
static door::ShuffleBag gBags[(int)door::State::Count];
static uint8_t gBagHumor[(int)door::State::Count];

static bool parseQuips(const char* json, std::vector<Quip>* out) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  if (!doc.is<JsonObject>()) return false;
  for (int i = 0; i < (int)door::State::Count; i++) {
    out[i].clear();
    JsonArray arr = doc[door::stateId((door::State)i)].as<JsonArray>();
    for (JsonVariant v : arr) {
      const char* t = v["text"] | "";
      if (!*t) continue;
      const char* lvl = v["level"] | "full";
      out[i].push_back({t, strcmp(lvl, "dry") == 0});
    }
  }
  return true;
}

static void resetBags() {
  for (int i = 0; i < (int)door::State::Count; i++) {
    gBags[i].reset(0);
    gBagHumor[i] = 255;
  }
}

void loadQuips() {
  std::string json = quipsJson();
  if (!parseQuips(json.c_str(), gQuips)) {
    logf("[fs] quips.json invalid, using built-in quips");
    parseQuips(kDefaultQuips, gQuips);
  }
  resetBags();
  size_t total = 0;
  for (auto& v : gQuips) total += v.size();
  logf("[fs] %u quips loaded", (unsigned)total);
}

std::string quipsJson() {
  if (gFsOk) {
    File f = LittleFS.open(kQuipPath, "r");
    if (f) {
      String s = f.readString();
      f.close();
      return std::string(s.c_str());
    }
  }
  return std::string(kDefaultQuips);
}

bool saveQuipsJson(const std::string& json) {
  std::vector<Quip> tmp[(int)door::State::Count];
  if (!parseQuips(json.c_str(), tmp)) return false;
  if (!gFsOk) return false;
  File f = LittleFS.open(kQuipPath, "w");
  if (!f) return false;
  f.print(json.c_str());
  f.close();
  for (int i = 0; i < (int)door::State::Count; i++) gQuips[i] = tmp[i];
  resetBags();
  return true;
}

std::string pickQuip(door::State s, uint8_t humor) {
  if (humor == 0) return "";
  int i = (int)s;
  std::vector<size_t> idx;
  for (size_t k = 0; k < gQuips[i].size(); k++)
    if (humor >= 2 || gQuips[i][k].dry) idx.push_back(k);
  if (idx.empty()) return "";
  if (gBagHumor[i] != humor || gBags[i].size() != idx.size()) {
    gBags[i].reset(idx.size());
    gBagHumor[i] = humor;
  }
  size_t n = gBags[i].next([](size_t k) { return (size_t)(esp_random() % k); });
  return gQuips[i][idx[n]].text;
}
