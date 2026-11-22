#include "app.h"

#include <stdarg.h>
#include <time.h>

App app;

static SemaphoreHandle_t gLock = nullptr;

void appLock() {
  if (!gLock) gLock = xSemaphoreCreateRecursiveMutex();
  xSemaphoreTakeRecursive(gLock, portMAX_DELAY);
}

void appUnlock() { xSemaphoreGiveRecursive(gLock); }

door::Clock nowClock() { return clockAt((int64_t)time(nullptr)); }

bool simActive() {
  AppGuard g;
  return app.simUntil && (int32_t)(app.simUntil - millis()) > 0;
}

door::Clock clockAt(int64_t t64) {
  door::Clock c;
  time_t t = (time_t)t64;
  if (time(nullptr) < 1700000000 || t < 1700000000) return c;  // NTP not synced yet
  struct tm lt;
  localtime_r(&t, &lt);
  c.valid = true;
  c.now = (int64_t)t;
  c.secOfDay = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  c.wday = (uint8_t)lt.tm_wday;
  return c;
}

static const int kLogLines = 40;
static String gLog[kLogLines];
static int gLogHead = 0;
static SemaphoreHandle_t gLogLock = nullptr;

void logf(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  Serial.println(buf);
  if (!gLogLock) gLogLock = xSemaphoreCreateMutex();
  char stamp[16];
  snprintf(stamp, sizeof(stamp), "%7lu ", (unsigned long)(millis() / 1000));
  xSemaphoreTake(gLogLock, portMAX_DELAY);
  gLog[gLogHead] = String(stamp) + buf;
  gLogHead = (gLogHead + 1) % kLogLines;
  xSemaphoreGive(gLogLock);
}

String logDump() {
  String out;
  if (!gLogLock) return out;
  xSemaphoreTake(gLogLock, portMAX_DELAY);
  for (int i = 0; i < kLogLines; i++) {
    const String& l = gLog[(gLogHead + i) % kLogLines];
    if (l.length()) {
      out += l;
      out += '\n';
    }
  }
  xSemaphoreGive(gLogLock);
  return out;
}
