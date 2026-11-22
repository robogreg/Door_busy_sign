#include "status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace door {

namespace {
struct StateInfo {
  const char* id;
  const char* label;
  Band band;
};
const StateInfo kInfo[] = {
    {"available", "AVAILABLE", Band::None},
    {"meeting", "IN A MEETING", Band::Red},
    {"call", "ON A CALL", Band::Red},
    {"focus", "HEADS DOWN", Band::Black},
    {"busy", "BUSY", Band::Black},
    {"away", "AWAY", Band::None},
    {"gone", "GONE FOR THE DAY", Band::None},
    {"custom", "", Band::None},
};
const char* kDays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
constexpr int32_t kDay = 86400;

int64_t floorDiv(int64_t a, int64_t b) {
  int64_t q = a / b;
  if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
  return q;
}
}  // namespace

const char* stateId(State s) { return kInfo[(int)s].id; }
const char* stateLabel(State s) { return kInfo[(int)s].label; }
Band defaultBand(State s) { return kInfo[(int)s].band; }

bool parseState(const char* id, State& out) {
  if (!id) return false;
  for (int i = 0; i < (int)State::Count; i++) {
    if (strcmp(id, kInfo[i].id) == 0) {
      out = (State)i;
      return true;
    }
  }
  return false;
}

const char* bandId(Band b) {
  switch (b) {
    case Band::Red: return "red";
    case Band::Black: return "black";
    default: return "none";
  }
}

bool parseBand(const char* id, Band& out) {
  if (!id) return false;
  if (strcmp(id, "red") == 0) out = Band::Red;
  else if (strcmp(id, "black") == 0) out = Band::Black;
  else if (strcmp(id, "none") == 0) out = Band::None;
  else return false;
  return true;
}

bool parseDuration(const char* s, Duration& out) {
  out = Duration{};
  if (!s || !*s) return true;
  if (strcmp(s, "eod") == 0) { out.kind = DurKind::EndOfDay; return true; }
  if (strcmp(s, "event") == 0) { out.kind = DurKind::Event; return true; }
  if (strcmp(s, "clear") == 0) { out.kind = DurKind::Cleared; return true; }
  char* end = nullptr;
  long m = strtol(s, &end, 10);
  if (end == s || *end != '\0' || m < 1 || m > 1440) return false;
  out.kind = DurKind::Minutes;
  out.minutes = (uint32_t)m;
  return true;
}

Duration defaultDuration(State s) {
  Duration d;
  // Every override rolls back to the calendar on its own unless told otherwise.
  d.kind = DurKind::Minutes;
  d.minutes = s == State::Call ? 30 : 60;
  return d;
}

bool calendarStateAt(const CalendarData& cal, const Clock& c, State& st, int64_t& until) {
  if (!cal.valid) return false;
  if (cal.until == 0 || (c.valid && c.now < cal.until)) {
    st = cal.state;
    until = cal.until;
    return true;
  }
  if (cal.hasNext) {
    st = cal.nextState;
    until = 0;  // unknown until the next poll
    return true;
  }
  return false;
}

static bool calUsable(const CalendarData* cal, const Clock& c, State& st, int64_t& until) {
  if (!cal || !c.valid || !cal->valid) return false;
  if (c.now - cal->fetchedAt > kCalendarMaxAge) return false;
  return calendarStateAt(*cal, c, st, until);
}

int64_t computeExpiry(State s, const Duration& d0, const Clock& c, const CalendarData* cal) {
  Duration d = d0;
  State cs = State::Available;
  int64_t cu = 0;
  bool haveCal = calUsable(cal, c, cs, cu) && cu > c.now;
  bool inEvent = haveCal && cs != State::Available;
  if (c.valid && d.kind == DurKind::Event && inEvent) return cu;
  if (d.kind == DurKind::Default || d.kind == DurKind::Event) d = defaultDuration(s);
  if (d.kind == DurKind::Cleared || !c.valid) return kUntilCleared;
  if (d.kind == DurKind::EndOfDay) return c.now + (kDay - c.secOfDay);
  return c.now + (int64_t)d.minutes * 60;
}

bool overrideActive(const Override& o, const Clock& c) {
  if (!o.active) return false;
  if (o.expires == kUntilCleared) return true;
  if (!c.valid) return true;  // can't judge expiry without time; keep it
  return c.now < o.expires;
}

int64_t nextWorkStart(const Clock& c, const WorkHours& h) {
  int64_t midnight = c.now - c.secOfDay;
  for (int d = 0; d <= 7; d++) {
    uint8_t wd = (uint8_t)((c.wday + d) % 7);
    if (!(h.days & (1 << wd))) continue;
    int64_t start = midnight + (int64_t)d * kDay + (int64_t)h.startMin * 60;
    if (start > c.now) return start;
  }
  return 0;
}

Status resolve(const Clock& c, const Override& o, const WorkHours& h, const CalendarData& cal) {
  Status st;
  if (overrideActive(o, c)) {
    st.state = o.state;
    st.manual = true;
    st.until = o.expires;
    if (o.state == State::Custom) {
      st.band = o.band;
      st.label = o.text;
    } else {
      st.band = defaultBand(o.state);
      st.label = stateLabel(o.state);
    }
    return st;
  }
  State cs = State::Available;
  int64_t cu = 0;
  bool haveCal = calUsable(&cal, c, cs, cu);
  if (haveCal && cs != State::Available) {
    st.state = cs;
    st.until = cu;
    st.band = defaultBand(cs);
    st.label = stateLabel(cs);
    return st;
  }
  if (!c.valid) {
    st.state = State::Available;
  } else {
    int32_t minute = c.secOfDay / 60;
    bool workDay = h.days & (1 << c.wday);
    if (workDay && minute >= h.startMin && minute < h.endMin) {
      st.state = State::Available;
      st.until = c.now - c.secOfDay + (int64_t)h.endMin * 60;
      if (haveCal && cu > c.now && cu < st.until) st.until = cu;  // free until the next event
    } else {
      st.state = State::Gone;
      st.until = nextWorkStart(c, h);
    }
  }
  st.band = defaultBand(st.state);
  st.label = stateLabel(st.state);
  return st;
}

std::string formatTime(int32_t secOfDay) {
  int32_t m = (secOfDay / 60) % 1440;
  int hh = m / 60, mm = m % 60;
  int h12 = hh % 12 == 0 ? 12 : hh % 12;
  char buf[12];
  snprintf(buf, sizeof(buf), "%d:%02d %s", h12, mm, hh < 12 ? "AM" : "PM");
  return buf;
}

namespace {
// Day offset from today (0 = today) and local secOfDay for epoch t.
void split(const Clock& c, int64_t t, int64_t& dayOff, int32_t& sod) {
  int64_t rel = (int64_t)c.secOfDay + (t - c.now);
  dayOff = floorDiv(rel, kDay);
  sod = (int32_t)(rel - dayOff * kDay);
}
}  // namespace

std::string formatWhen(const Clock& c, int64_t t) {
  int64_t off;
  int32_t sod;
  split(c, t, off, sod);
  std::string tm = formatTime(sod);
  if (off == 0) return tm;
  if (off == 1) return "tomorrow " + tm;
  return std::string(kDays[(c.wday + off) % 7]) + " " + tm;
}

std::string timeLine(const Status& st, const Clock& c) {
  if (!c.valid || st.until == 0) return "";
  int64_t off;
  int32_t sod;
  split(c, st.until, off, sod);
  switch (st.state) {
    case State::Available: return "Free until " + formatWhen(c, st.until);
    case State::Meeting:
    case State::Call:
    case State::Focus:
    case State::Busy: return "Free at " + formatWhen(c, st.until);
    case State::Away:
    case State::Gone:
      if (off == 0) return "Back at " + formatTime(sod);
      if (off == 1) return "Back tomorrow";
      return std::string("Back ") + kDays[(c.wday + off) % 7];
    case State::Custom: return "Until " + formatWhen(c, st.until);
    default: return "";
  }
}

namespace {
bool digits(const char* s, int n, int& out) {
  out = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    out = out * 10 + (s[i] - '0');
  }
  return true;
}
// Howard Hinnant's days_from_civil.
int64_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}
}  // namespace

bool parseIso8601(const char* s, int64_t& out) {
  if (!s || strlen(s) < 20) return false;
  int y, mo, d, hh, mm, ss;
  if (!digits(s, 4, y) || s[4] != '-' || !digits(s + 5, 2, mo) || s[7] != '-' || !digits(s + 8, 2, d) ||
      s[10] != 'T' || !digits(s + 11, 2, hh) || s[13] != ':' || !digits(s + 14, 2, mm) || s[16] != ':' ||
      !digits(s + 17, 2, ss))
    return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || hh > 23 || mm > 59 || ss > 60) return false;
  const char* p = s + 19;
  if (*p == '.') {  // fractional seconds, ignored
    p++;
    while (*p >= '0' && *p <= '9') p++;
  }
  int offset = 0;
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    int oh, om;
    if (!digits(p + 1, 2, oh) || p[3] != ':' || !digits(p + 4, 2, om)) return false;
    offset = (oh * 60 + om) * 60 * (*p == '-' ? -1 : 1);
    p += 6;
  } else {
    return false;
  }
  if (*p) return false;
  out = daysFromCivil(y, mo, d) * 86400 + hh * 3600 + mm * 60 + ss - offset;
  return true;
}

bool parseHHMM(const char* s, uint16_t& out) {
  int h, m;
  if (!s || strlen(s) != 5 || s[2] != ':' || !digits(s, 2, h) || !digits(s + 3, 2, m)) return false;
  if (m > 59 || h > 24 || (h == 24 && m != 0)) return false;
  out = (uint16_t)(h * 60 + m);
  return true;
}

namespace {
struct MoodInfo {
  const char* id;
  const char* name;
};
const MoodInfo kMoods[] = {
    {"none", "None"},         {"happy", "Happy"},     {"chill", "Chill"},
    {"focused", "Focused"},   {"facepalm", "Facepalm"}, {"stressed", "Stressed"},
    {"grumpy", "Grumpy"},     {"tired", "Tired"},     {"celebrating", "Celebrating"},
    {"love", "Grateful"},     {"meh", "Meh"},         {"sick", "Under the weather"},
    {"mindblown", "Mind blown"}, {"thinking", "Thinking"}, {"hungry", "Hungry"},
    {"zen", "Zen"},           {"zipped", "Do not disturb"},
};
static_assert(sizeof(kMoods) / sizeof(kMoods[0]) == (size_t)Mood::Count, "mood table");
}  // namespace

const char* moodId(Mood m) { return kMoods[(int)m].id; }
const char* moodName(Mood m) { return kMoods[(int)m].name; }

bool parseMood(const char* id, Mood& out) {
  if (!id) return false;
  for (int i = 1; i < (int)Mood::Count; i++) {
    if (strcmp(id, kMoods[i].id) == 0) {
      out = (Mood)i;
      return true;
    }
  }
  return false;
}

bool moodExpired(int64_t setAt, const Clock& c, const WorkHours& h) {
  if (!c.valid || setAt <= 0) return false;
  int64_t todayStart = c.now - c.secOfDay + (int64_t)h.startMin * 60;
  if (c.now < todayStart) todayStart -= kDay;  // before today's start: compare with yesterday's
  return setAt < todayStart && c.now >= todayStart;
}

uint32_t fnv1a(const std::string& s, uint32_t h) {
  for (unsigned char ch : s) {
    h ^= ch;
    h *= 16777619u;
  }
  return h;
}

void ShuffleBag::reset(size_t n) {
  order_.resize(n);
  for (size_t i = 0; i < n; i++) order_[i] = i;
  pos_ = n;  // forces a shuffle on first next()
}

}  // namespace door
