// Door Sign status core: pure C++ (no Arduino), unit-tested natively.
//
// Priority: manual override > calendar (M3) > working-hours default.
// Time is passed in as a Clock so tests never depend on the host time zone.
#pragma once

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace door {

enum class State : uint8_t { Available, Meeting, Call, Focus, Busy, Away, Gone, Custom, Count };
enum class Band : uint8_t { None, Red, Black };

const char* stateId(State s);     // "available"
const char* stateLabel(State s);  // "AVAILABLE"
bool parseState(const char* id, State& out);
Band defaultBand(State s);
const char* bandId(Band b);  // "none" | "red" | "black"
bool parseBand(const char* id, Band& out);

// Local wall-clock snapshot. secOfDay and wday are local time; now is epoch seconds.
struct Clock {
  bool valid = false;
  int64_t now = 0;
  int32_t secOfDay = 0;  // 0..86399
  uint8_t wday = 0;      // 0 = Sunday
};

struct WorkHours {
  uint16_t startMin = 8 * 60;
  uint16_t endMin = 18 * 60;
  uint8_t days = 0x3E;  // bit 0 = Sunday; default Mon..Fri
};

constexpr int64_t kUntilCleared = 0;

struct Override {
  bool active = false;
  State state = State::Available;
  int64_t expires = kUntilCleared;  // epoch seconds, 0 = until cleared
  Band band = Band::None;           // custom only
  std::string text;                 // custom only
};

// Last good broker response (SPEC section 6), already converted to epoch seconds.
struct CalendarData {
  bool valid = false;
  State state = State::Available;
  int64_t until = 0;  // 0 = open-ended
  bool hasNext = false;
  State nextState = State::Available;
  int64_t nextAt = 0;
  std::vector<std::pair<uint16_t, uint16_t>> blocks;  // minutes from local midnight
  int64_t fetchedAt = 0;  // device epoch of the last successful poll
  std::string title;       // current event title, empty if none or private
  int64_t titleUntil = 0;  // end of the event the title belongs to
};

// Calendar data older than this is ignored and working hours take over.
constexpr int64_t kCalendarMaxAge = 2 * 3600;
// Broker unreachable this long -> "stale" icon (SPEC section 7).
constexpr int64_t kStaleAfter = 10 * 60;

// State the calendar implies right now. After "until" passes, falls through to
// "next" (with unknown end) until the next poll. False if there is no usable data.
bool calendarStateAt(const CalendarData& cal, const Clock& c, State& st, int64_t& until);

// "2026-11-20T15:30:00-06:00" or "...Z" -> epoch seconds.
bool parseIso8601(const char* s, int64_t& out);
// "09:30" -> 570. Accepts "24:00".
bool parseHHMM(const char* s, uint16_t& out);

enum class DurKind : uint8_t { Default, Minutes, EndOfDay, Event, Cleared };
struct Duration {
  DurKind kind = DurKind::Default;
  uint32_t minutes = 0;
};

// "" or null -> Default, "30" -> Minutes, "eod" -> EndOfDay, "event" -> Event,
// "clear" -> Cleared. Returns false on garbage or minutes outside 1..1440.
bool parseDuration(const char* s, Duration& out);

// Default override length: 30 min for a call, 1 h for everything else.
Duration defaultDuration(State s);

// Absolute expiry for a new override. Default is 1 h (30 min for a call), so a
// bare override always rolls back to the calendar. "event" means until the
// current calendar event ends (state default if not in an event). If the clock
// is not valid, timed overrides become until-cleared rather than guessing.
int64_t computeExpiry(State s, const Duration& d, const Clock& c, const CalendarData* cal = nullptr);

bool overrideActive(const Override& o, const Clock& c);

struct Status {
  State state = State::Available;
  Band band = Band::None;
  bool manual = false;
  int64_t until = 0;  // epoch seconds, 0 = open-ended
  std::string label;
  std::string detail;  // current calendar event title (shown in place of the quip)
};

// Override > calendar > working hours. A calendar "available" still becomes
// "gone" outside working hours; calendar events outside hours still show.
Status resolve(const Clock& c, const Override& o, const WorkHours& h, const CalendarData& cal = CalendarData{});

// Epoch seconds of the next working-hours start strictly after now (or later today).
int64_t nextWorkStart(const Clock& c, const WorkHours& h);

// "3:30 PM"
std::string formatTime(int32_t secOfDay);
// "3:30 PM" today, "tomorrow 9:00 AM", "Monday 9:00 AM"
std::string formatWhen(const Clock& c, int64_t t);
// The time line under the quip: "Free at 3:30 PM", "Free until 6:00 PM", "Back tomorrow".
std::string timeLine(const Status& st, const Clock& c);

// ---------- Mood emoji ----------

enum class Mood : uint8_t {
  None,
  Happy, Chill, Focused, Facepalm, Stressed, Grumpy, Tired, Celebrating,  // classic 8
  Love, Meh, Sick, MindBlown, Thinking, Hungry, Zen, Zipped,                // extended
  Count
};
const char* moodId(Mood m);    // "grumpy"
const char* moodName(Mood m);  // "Grumpy"
bool parseMood(const char* id, Mood& out);
// Mood clears each morning: true once the start of today's working hours has
// passed since it was set (whether or not today is a working day).
bool moodExpired(int64_t setAt, const Clock& c, const WorkHours& h);

// FNV-1a, used for the render-on-change frame hash.
uint32_t fnv1a(const std::string& s, uint32_t h = 2166136261u);

// No-repeat shuffle bag over n items. rnd(k) must return 0..k-1.
class ShuffleBag {
 public:
  void reset(size_t n);
  template <typename Rng>
  size_t next(Rng rnd) {
    if (order_.empty()) return 0;
    if (pos_ >= order_.size()) {
      size_t last = order_.back();
      for (size_t i = order_.size() - 1; i > 0; i--) {
        size_t j = rnd(i + 1);
        size_t t = order_[i];
        order_[i] = order_[j];
        order_[j] = t;
      }
      // avoid the same item twice in a row across cycles
      if (order_.size() > 1 && order_[0] == last) {
        size_t t = order_[0];
        order_[0] = order_[1];
        order_[1] = t;
      }
      pos_ = 0;
    }
    return order_[pos_++];
  }
  size_t size() const { return order_.size(); }

 private:
  std::vector<size_t> order_;
  size_t pos_ = 0;
};

}  // namespace door
