// Shared device state. Web handlers run on the AsyncTCP task and the display
// renders on its own task, so everything here is guarded by appLock().
#pragma once

#include <Arduino.h>
#include <string>

#include "status.h"

struct Settings {
  std::string name = "GREG NEEDEL";
  std::string title = "CEO, REV Robotics";
  std::string pin = "1234";
  std::string tz = "CST6CDT,M3.2.0,M11.1.0";
  door::WorkHours hours;
  uint8_t humor = 2;  // 0 off, 1 dry, 2 full
  uint8_t fast = 0;   // fast black/white refresh: 0 off, 1..3 waveform length
  // Calendar broker (M3)
  std::string brokerUrl;    // Apps Script web app .../exec URL
  std::string brokerToken;  // write-only in the UI
  std::string cals = "primary:mine";
};

struct BrokerState {
  uint32_t lastPollMs = 0;  // millis() of the last attempt, 0 = never
  int64_t lastOk = 0;       // epoch of the last good response, 0 = never
  std::string lastError;
  std::string lastResponse;  // raw JSON (states and times only)
  bool pollNow = false;
  // Calendar picker: 0 idle, 1 pending, 2 ready, 3 error
  uint8_t calsState = 0;
  std::string calsJson;  // raw {"calendars":[...]} or error text
};

struct App {
  Settings settings;
  door::Override ovr;
  door::CalendarData cal;
  BrokerState broker;
  door::Mood mood = door::Mood::None;
  int64_t moodSetAt = 0;       // epoch, for the morning reset
  uint32_t galleryUntil = 0;   // millis(); mood gallery test screen while active
  // Time preview: the sign shows another date and time (offset from real time)
  // until simUntil (millis). Nothing persistent changes while it runs.
  int64_t simOffset = 0;
  uint32_t simUntil = 0;
  door::CalendarData simCal;   // broker status as of the simulated time
  // Snapshot computed by loop(), read by the web handlers.
  door::Status status;
  std::string timeLine;
  std::string quip;
  bool stale = false;
  // Requests from web handlers, consumed by loop().
  bool forceRender = false;
  bool newQuip = false;
  bool reboot = false;
  bool updating = false;       // firmware upload in progress
};

extern App app;

void appLock();
void appUnlock();

struct AppGuard {
  AppGuard() { appLock(); }
  ~AppGuard() { appUnlock(); }
};

// Current wall clock from the system time (NTP).
door::Clock nowClock();
// Wall clock for epoch t in the configured time zone.
door::Clock clockAt(int64_t t);
// True while a time preview is running.
bool simActive();

// Ring-buffer log, also echoed to Serial.
void logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
String logDump();
