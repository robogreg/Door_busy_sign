// E-paper rendering. All panel access happens on the render task (after boot).
#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include "status.h"

// Everything that appears on the status screen. The hash covers all fields
// except the "updated" time, which is stamped at render time.
struct Frame {
  std::string name, title;
  std::string label, quip, timeLine;
  std::string footerRight;  // IP address
  std::string date;         // "Sunday, Nov 22" in the footer; changes at midnight
  door::Band band = door::Band::None;
  bool muted = false;   // away / gone: outlined band
  bool manual = false;  // override icon in footer
  bool stale = false;   // broker unreachable 10+ min
  // Timeline strip (shown when calendar data is current)
  bool strip = false;
  uint16_t dayStart = 8 * 60, dayEnd = 18 * 60;      // minutes, follows working hours
  std::vector<std::pair<uint16_t, uint16_t>> blocks;  // busy minutes
  int16_t nowMin = -1;                                // rounded to 15 min, -1 hides the tick
  door::Mood mood = door::Mood::None;                 // face at the right end of the band
  bool gallery = false;                               // test screen: all 16 mood faces
  uint32_t hash() const;
};

struct BootInfo {
  std::string ssid, ip, mac, host, fw;
  bool online = false;
};

void displayBegin();                  // SPI remap + panel init
void drawBootScreen(const BootInfo&);  // blocking, call before displayStartTask()
void displayStartTask();
// Queue a frame; the render task draws the latest one. force redraws even if unchanged.
void displayRequest(const Frame& f, bool force = false);
bool displayBusy();
uint32_t displayLastMs();
uint32_t displayCount();

// Renders what is on the panel now as a 4-bit BMP, streamed in 32-row slices so
// it needs only a few KB of RAM. Safe to use from the web server task.
class DisplayPreview {
 public:
  DisplayPreview();
  ~DisplayPreview();
  static size_t size();
  // Fills buf with up to maxLen bytes of the BMP starting at index; returns bytes written.
  size_t fill(uint8_t* buf, size_t maxLen, size_t index);

 private:
  struct Impl;
  Impl* impl_;
  DisplayPreview(const DisplayPreview&) = delete;
  DisplayPreview& operator=(const DisplayPreview&) = delete;
};
