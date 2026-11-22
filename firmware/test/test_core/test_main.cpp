// Native unit tests for the status core: pio test -d firmware -e native
#include <unity.h>

#include <set>

#include "status.h"

using namespace door;

static const int64_t kMidnight = 1763877600;  // arbitrary local midnight

static Clock at(uint8_t wday, int h, int m) {
  Clock c;
  c.valid = true;
  c.secOfDay = h * 3600 + m * 60;
  c.now = kMidnight + c.secOfDay;
  c.wday = wday;
  return c;
}

void setUp() {}
void tearDown() {}

void test_parse_state_and_band() {
  State s;
  TEST_ASSERT_TRUE(parseState("call", s));
  TEST_ASSERT_EQUAL(State::Call, s);
  TEST_ASSERT_FALSE(parseState("nap", s));
  TEST_ASSERT_FALSE(parseState(nullptr, s));
  Band b;
  TEST_ASSERT_TRUE(parseBand("red", b));
  TEST_ASSERT_EQUAL(Band::Red, b);
  TEST_ASSERT_FALSE(parseBand("blue", b));
  TEST_ASSERT_EQUAL_STRING("IN A MEETING", stateLabel(State::Meeting));
}

void test_parse_duration() {
  Duration d;
  TEST_ASSERT_TRUE(parseDuration(nullptr, d));
  TEST_ASSERT_EQUAL(DurKind::Default, d.kind);
  TEST_ASSERT_TRUE(parseDuration("30", d));
  TEST_ASSERT_EQUAL(DurKind::Minutes, d.kind);
  TEST_ASSERT_EQUAL_UINT32(30, d.minutes);
  TEST_ASSERT_TRUE(parseDuration("eod", d));
  TEST_ASSERT_EQUAL(DurKind::EndOfDay, d.kind);
  TEST_ASSERT_TRUE(parseDuration("event", d));
  TEST_ASSERT_EQUAL(DurKind::Event, d.kind);
  TEST_ASSERT_TRUE(parseDuration("clear", d));
  TEST_ASSERT_EQUAL(DurKind::Cleared, d.kind);
  TEST_ASSERT_FALSE(parseDuration("0", d));
  TEST_ASSERT_FALSE(parseDuration("1441", d));
  TEST_ASSERT_FALSE(parseDuration("30x", d));
  TEST_ASSERT_FALSE(parseDuration("-5", d));
}

void test_expiry_defaults() {
  Clock c = at(2, 10, 0);
  Duration def;
  TEST_ASSERT_EQUAL_INT64(c.now + 30 * 60, computeExpiry(State::Call, def, c));
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Meeting, def, c));
  // Every bare override rolls back after an hour, including away and custom.
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Away, def, c));
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Available, def, c));
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Custom, def, c));
  Duration clr;
  parseDuration("clear", clr);
  TEST_ASSERT_EQUAL_INT64(kUntilCleared, computeExpiry(State::Away, clr, c));
  Duration ev;
  ev.kind = DurKind::Event;  // no calendar yet: falls back to default
  TEST_ASSERT_EQUAL_INT64(c.now + 30 * 60, computeExpiry(State::Call, ev, c));
}

void test_expiry_explicit_and_invalid_clock() {
  Clock c = at(2, 10, 0);
  Duration d;
  parseDuration("15", d);
  TEST_ASSERT_EQUAL_INT64(c.now + 15 * 60, computeExpiry(State::Busy, d, c));
  parseDuration("eod", d);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 86400, computeExpiry(State::Busy, d, c));
  parseDuration("clear", d);
  TEST_ASSERT_EQUAL_INT64(kUntilCleared, computeExpiry(State::Call, d, c));
  Clock bad;
  Duration def;
  TEST_ASSERT_EQUAL_INT64(kUntilCleared, computeExpiry(State::Away, def, bad));
  parseDuration("15", d);
  TEST_ASSERT_EQUAL_INT64(kUntilCleared, computeExpiry(State::Call, d, bad));
}

void test_override_beats_hours_and_expires() {
  WorkHours h;
  Clock c = at(2, 10, 0);
  Override o;
  o.active = true;
  o.state = State::Call;
  o.expires = c.now + 1800;
  Status st = resolve(c, o, h);
  TEST_ASSERT_EQUAL(State::Call, st.state);
  TEST_ASSERT_TRUE(st.manual);
  TEST_ASSERT_EQUAL(Band::Red, st.band);
  TEST_ASSERT_EQUAL_STRING("ON A CALL", st.label.c_str());

  Clock later = at(2, 10, 30);  // exactly at expiry: override is over
  st = resolve(later, o, h);
  TEST_ASSERT_EQUAL(State::Available, st.state);
  TEST_ASSERT_FALSE(st.manual);
}

void test_override_until_cleared_and_invalid_clock() {
  WorkHours h;
  Override o;
  o.active = true;
  o.state = State::Away;
  o.expires = kUntilCleared;
  TEST_ASSERT_EQUAL(State::Away, resolve(at(3, 23, 0), o, h).state);
  Override timed = o;
  timed.expires = 123;  // long past, but clock unknown: keep it
  Clock bad;
  TEST_ASSERT_TRUE(overrideActive(timed, bad));
  o.active = false;
  TEST_ASSERT_FALSE(overrideActive(o, at(3, 9, 0)));
}

void test_custom_override() {
  WorkHours h;
  Override o;
  o.active = true;
  o.state = State::Custom;
  o.text = "Back at 3";
  o.band = Band::Black;
  Status st = resolve(at(1, 11, 0), o, h);
  TEST_ASSERT_EQUAL(State::Custom, st.state);
  TEST_ASSERT_EQUAL(Band::Black, st.band);
  TEST_ASSERT_EQUAL_STRING("Back at 3", st.label.c_str());
}

void test_working_hours_default() {
  WorkHours h;  // Mon..Fri 8:00..18:00
  Override none;
  Status st = resolve(at(2, 9, 0), none, h);  // Tuesday 9 AM
  TEST_ASSERT_EQUAL(State::Available, st.state);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 18 * 3600, st.until);

  st = resolve(at(2, 7, 59), none, h);  // before start
  TEST_ASSERT_EQUAL(State::Gone, st.state);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 8 * 3600, st.until);

  st = resolve(at(2, 18, 0), none, h);  // end is exclusive
  TEST_ASSERT_EQUAL(State::Gone, st.state);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 86400 + 8 * 3600, st.until);

  st = resolve(at(5, 19, 0), none, h);  // Friday evening -> Monday
  TEST_ASSERT_EQUAL(State::Gone, st.state);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 3 * 86400 + 8 * 3600, st.until);

  st = resolve(at(6, 12, 0), none, h);  // Saturday noon
  TEST_ASSERT_EQUAL(State::Gone, st.state);

  Clock bad;
  TEST_ASSERT_EQUAL(State::Available, resolve(bad, none, h).state);
}

void test_format_time() {
  TEST_ASSERT_EQUAL_STRING("12:00 AM", formatTime(0).c_str());
  TEST_ASSERT_EQUAL_STRING("9:05 AM", formatTime(9 * 3600 + 5 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("12:30 PM", formatTime(12 * 3600 + 30 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("3:30 PM", formatTime(15 * 3600 + 30 * 60).c_str());
  TEST_ASSERT_EQUAL_STRING("11:59 PM", formatTime(86399).c_str());
}

void test_time_lines() {
  WorkHours h;
  Override o;
  o.active = true;
  o.state = State::Meeting;
  Clock c = at(2, 14, 0);
  o.expires = c.now + 90 * 60;
  TEST_ASSERT_EQUAL_STRING("Free at 3:30 PM", timeLine(resolve(c, o, h), c).c_str());

  o.state = State::Call;
  o.expires = kMidnight + 86400 + 9 * 3600;  // tomorrow 9 AM
  TEST_ASSERT_EQUAL_STRING("Free at tomorrow 9:00 AM", timeLine(resolve(c, o, h), c).c_str());

  o.expires = kUntilCleared;
  TEST_ASSERT_EQUAL_STRING("", timeLine(resolve(c, o, h), c).c_str());

  Override none;
  TEST_ASSERT_EQUAL_STRING("Free until 6:00 PM", timeLine(resolve(c, none, h), c).c_str());
  Clock eve = at(2, 19, 0);
  TEST_ASSERT_EQUAL_STRING("Back tomorrow", timeLine(resolve(eve, none, h), eve).c_str());
  Clock fri = at(5, 19, 0);
  TEST_ASSERT_EQUAL_STRING("Back Monday", timeLine(resolve(fri, none, h), fri).c_str());
  Clock early = at(2, 6, 0);
  TEST_ASSERT_EQUAL_STRING("Back at 8:00 AM", timeLine(resolve(early, none, h), early).c_str());

  o.state = State::Custom;
  o.text = "Lunch";
  o.expires = c.now + 30 * 60;
  TEST_ASSERT_EQUAL_STRING("Until 2:30 PM", timeLine(resolve(c, o, h), c).c_str());
}

void test_hash_changes() {
  TEST_ASSERT_NOT_EQUAL(fnv1a("IN A MEETING"), fnv1a("ON A CALL"));
  TEST_ASSERT_EQUAL_UINT32(fnv1a("abc"), fnv1a("abc"));
}

void test_shuffle_bag_cycles_without_repeats() {
  ShuffleBag bag;
  bag.reset(5);
  unsigned seed = 7;
  auto rnd = [&](size_t k) {
    seed = seed * 1103515245u + 12345u;
    return (size_t)((seed >> 8) % k);
  };
  size_t prev = 99;
  for (int cycle = 0; cycle < 20; cycle++) {
    std::set<size_t> seen;
    for (int i = 0; i < 5; i++) {
      size_t v = bag.next(rnd);
      TEST_ASSERT_TRUE(v < 5);
      TEST_ASSERT_NOT_EQUAL(prev, v);
      seen.insert(v);
      prev = v;
    }
    TEST_ASSERT_EQUAL(5, (int)seen.size());
  }
  ShuffleBag empty;
  empty.reset(0);
  TEST_ASSERT_EQUAL(0, (int)empty.next(rnd));
}

// ---------- M3: calendar ----------

static CalendarData cal(const Clock& c, State s, int64_t until) {
  CalendarData d;
  d.valid = true;
  d.state = s;
  d.until = until;
  d.fetchedAt = c.now;
  return d;
}

void test_iso8601() {
  int64_t t;
  TEST_ASSERT_TRUE(parseIso8601("1970-01-01T00:00:00Z", t));
  TEST_ASSERT_EQUAL_INT64(0, t);
  TEST_ASSERT_TRUE(parseIso8601("2026-11-20T15:30:00-06:00", t));
  TEST_ASSERT_EQUAL_INT64(1795210200, t);  // 21:30 UTC, checked against Node Date.parse
  TEST_ASSERT_TRUE(parseIso8601("2026-11-20T21:30:00.000Z", t));
  TEST_ASSERT_EQUAL_INT64(1795210200, t);
  TEST_ASSERT_TRUE(parseIso8601("2024-02-29T12:00:00+05:30", t));
  TEST_ASSERT_EQUAL_INT64(1709188200, t);
  TEST_ASSERT_FALSE(parseIso8601("2026-11-20 15:30:00Z", t));
  TEST_ASSERT_FALSE(parseIso8601("2026-11-20T15:30:00", t));
  TEST_ASSERT_FALSE(parseIso8601("2026-13-20T15:30:00Z", t));
  TEST_ASSERT_FALSE(parseIso8601(nullptr, t));
}

void test_hhmm() {
  uint16_t m;
  TEST_ASSERT_TRUE(parseHHMM("09:30", m));
  TEST_ASSERT_EQUAL_UINT16(570, m);
  TEST_ASSERT_TRUE(parseHHMM("24:00", m));
  TEST_ASSERT_EQUAL_UINT16(1440, m);
  TEST_ASSERT_FALSE(parseHHMM("24:01", m));
  TEST_ASSERT_FALSE(parseHHMM("9:30", m));
  TEST_ASSERT_FALSE(parseHHMM("09:60", m));
}

void test_calendar_beats_hours_override_beats_calendar() {
  WorkHours h;
  Clock c = at(2, 14, 12);
  CalendarData d = cal(c, State::Meeting, kMidnight + 15 * 3600 + 30 * 60);
  Override none;
  Status st = resolve(c, none, h, d);
  TEST_ASSERT_EQUAL(State::Meeting, st.state);
  TEST_ASSERT_FALSE(st.manual);
  TEST_ASSERT_EQUAL_STRING("Free at 3:30 PM", timeLine(st, c).c_str());

  Override o;
  o.active = true;
  o.state = State::Call;
  TEST_ASSERT_EQUAL(State::Call, resolve(c, o, h, d).state);
}

void test_calendar_title_detail() {
  WorkHours h;
  Override none;
  Clock c = at(2, 10, 15);
  CalendarData d = cal(c, State::Meeting, kMidnight + 11 * 3600);
  d.title = "Quick sync";
  d.titleUntil = kMidnight + 10 * 3600 + 30 * 60;
  TEST_ASSERT_EQUAL_STRING("Quick sync", resolve(c, none, h, d).detail.c_str());
  // Back-to-back: after the titled event ends, the next poll brings the new title.
  TEST_ASSERT_EQUAL_STRING("", resolve(at(2, 10, 45), none, h, d).detail.c_str());
  // After "until" the state rolls to "next", which has no title.
  d.hasNext = true;
  d.nextState = State::Busy;
  d.titleUntil = 0;
  TEST_ASSERT_EQUAL_STRING("", resolve(at(2, 11, 5), none, h, d).detail.c_str());
  // A manual override never shows the event title.
  Override o;
  o.active = true;
  o.state = State::Focus;
  TEST_ASSERT_EQUAL_STRING("", resolve(c, o, h, d).detail.c_str());
}

void test_calendar_events_show_outside_hours() {
  WorkHours h;
  Clock eve = at(2, 19, 0);
  CalendarData d = cal(eve, State::Meeting, eve.now + 1800);
  TEST_ASSERT_EQUAL(State::Meeting, resolve(eve, Override{}, h, d).state);
  CalendarData free = cal(eve, State::Available, 0);
  TEST_ASSERT_EQUAL(State::Gone, resolve(eve, Override{}, h, free).state);
}

void test_calendar_available_until_next_event() {
  WorkHours h;
  Clock c = at(2, 10, 0);
  CalendarData d = cal(c, State::Available, kMidnight + 14 * 3600);
  d.hasNext = true;
  d.nextState = State::Meeting;
  d.nextAt = d.until;
  Status st = resolve(c, Override{}, h, d);
  TEST_ASSERT_EQUAL(State::Available, st.state);
  TEST_ASSERT_EQUAL_STRING("Free until 2:00 PM", timeLine(st, c).c_str());

  // Next event after working hours: hours end wins.
  d.until = kMidnight + 20 * 3600;
  TEST_ASSERT_EQUAL_STRING("Free until 6:00 PM", timeLine(resolve(c, Override{}, h, d), c).c_str());
}

void test_calendar_rolls_to_next_between_polls() {
  WorkHours h;
  Clock c = at(2, 10, 0);
  CalendarData d = cal(c, State::Available, kMidnight + 10 * 3600 + 60);
  d.hasNext = true;
  d.nextState = State::Meeting;
  d.nextAt = d.until;
  Clock later = at(2, 10, 2);  // poll has not landed yet
  Status st = resolve(later, Override{}, h, d);
  TEST_ASSERT_EQUAL(State::Meeting, st.state);
  TEST_ASSERT_EQUAL_INT64(0, st.until);

  // Meeting ends, next is available: back to working-hours default.
  CalendarData m = cal(c, State::Meeting, kMidnight + 10 * 3600 + 60);
  m.hasNext = true;
  m.nextState = State::Available;
  st = resolve(later, Override{}, h, m);
  TEST_ASSERT_EQUAL(State::Available, st.state);
  TEST_ASSERT_EQUAL_INT64(kMidnight + 18 * 3600, st.until);
}

void test_old_calendar_data_ignored() {
  WorkHours h;
  Clock c = at(2, 14, 0);
  CalendarData d = cal(c, State::Away, 0);
  d.fetchedAt = c.now - kCalendarMaxAge - 1;
  TEST_ASSERT_EQUAL(State::Available, resolve(c, Override{}, h, d).state);
  d.fetchedAt = c.now - kCalendarMaxAge;
  TEST_ASSERT_EQUAL(State::Away, resolve(c, Override{}, h, d).state);
}

void test_expiry_uses_calendar() {
  Clock c = at(2, 14, 12);
  int64_t end = kMidnight + 15 * 3600 + 30 * 60;
  CalendarData m = cal(c, State::Meeting, end);
  Duration ev;
  parseDuration("event", ev);
  TEST_ASSERT_EQUAL_INT64(end, computeExpiry(State::Call, ev, c, &m));
  // Default ignores the calendar: always 1 h (call 30 min).
  Duration def;
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Meeting, def, c, &m));
  TEST_ASSERT_EQUAL_INT64(c.now + 30 * 60, computeExpiry(State::Call, def, c, &m));

  CalendarData a = cal(c, State::Available, kMidnight + 16 * 3600);
  TEST_ASSERT_EQUAL_INT64(c.now + 60 * 60, computeExpiry(State::Available, def, c, &a));
  TEST_ASSERT_EQUAL_INT64(c.now + 30 * 60, computeExpiry(State::Call, ev, c, &a));  // event -> default
}

// ---------- Mood ----------

void test_mood_parse() {
  Mood m;
  TEST_ASSERT_TRUE(parseMood("grumpy", m));
  TEST_ASSERT_EQUAL(Mood::Grumpy, m);
  TEST_ASSERT_TRUE(parseMood("zipped", m));
  TEST_ASSERT_EQUAL(Mood::Zipped, m);
  TEST_ASSERT_FALSE(parseMood("none", m));  // "none" is clearing, not a mood
  TEST_ASSERT_FALSE(parseMood("hangry", m));
  for (int i = 1; i < (int)Mood::Count; i++) {
    TEST_ASSERT_TRUE(parseMood(moodId((Mood)i), m));
    TEST_ASSERT_EQUAL(i, (int)m);
  }
}

void test_mood_clears_each_morning() {
  WorkHours h;  // starts 8:00
  Clock tue10 = at(2, 10, 0);
  int64_t setMon = tue10.now - 86400;  // Monday 10:00
  TEST_ASSERT_TRUE(moodExpired(setMon, tue10, h));
  TEST_ASSERT_FALSE(moodExpired(tue10.now - 3600, tue10, h));  // set at 9:00 today

  Clock tue7 = at(2, 7, 0);  // before today's start
  TEST_ASSERT_FALSE(moodExpired(tue7.now - 3600, tue7, h));       // set at 6:00 today
  TEST_ASSERT_FALSE(moodExpired(tue7.now - 12 * 3600, tue7, h));  // set last night at 19:00
  TEST_ASSERT_TRUE(moodExpired(tue7.now - 24 * 3600, tue7, h));   // set yesterday 7:00, before yesterday's start

  Clock tue8 = at(2, 8, 0);  // exactly at start
  TEST_ASSERT_TRUE(moodExpired(tue8.now - 60, tue8, h));
  Clock bad;
  TEST_ASSERT_FALSE(moodExpired(123, bad, h));
  TEST_ASSERT_FALSE(moodExpired(0, tue10, h));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_state_and_band);
  RUN_TEST(test_parse_duration);
  RUN_TEST(test_expiry_defaults);
  RUN_TEST(test_expiry_explicit_and_invalid_clock);
  RUN_TEST(test_override_beats_hours_and_expires);
  RUN_TEST(test_override_until_cleared_and_invalid_clock);
  RUN_TEST(test_custom_override);
  RUN_TEST(test_working_hours_default);
  RUN_TEST(test_format_time);
  RUN_TEST(test_time_lines);
  RUN_TEST(test_hash_changes);
  RUN_TEST(test_shuffle_bag_cycles_without_repeats);
  RUN_TEST(test_iso8601);
  RUN_TEST(test_hhmm);
  RUN_TEST(test_calendar_beats_hours_override_beats_calendar);
  RUN_TEST(test_calendar_title_detail);
  RUN_TEST(test_calendar_events_show_outside_hours);
  RUN_TEST(test_calendar_available_until_next_event);
  RUN_TEST(test_calendar_rolls_to_next_between_polls);
  RUN_TEST(test_old_calendar_data_ignored);
  RUN_TEST(test_expiry_uses_calendar);
  RUN_TEST(test_mood_parse);
  RUN_TEST(test_mood_clears_each_morning);
  return UNITY_END();
}
