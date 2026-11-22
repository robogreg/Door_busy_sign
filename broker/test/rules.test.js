// Broker rule tests: cd broker && npm test
// Times are treated as UTC "local" time to keep fixtures readable.
const test = require("node:test");
const assert = require("node:assert/strict");
const path = require("path");

// Node loads unknown extensions as CommonJS; Rules.gs exports when module exists.
const R = require(path.join(__dirname, "..", "Rules.gs"));

const ME = "greg@example.com";
const DAY = "2026-11-20";
const t = (hm) => Date.parse(`${DAY}T${hm}:00Z`);
const localDateMs = (d) => Date.parse(`${d}T00:00:00Z`);
const fmtHM = (ms) => new Date(ms).toISOString().slice(11, 16);
const fmtIso = (ms) => new Date(ms).toISOString().replace(".000Z", "Z");
const PRIMARY = { id: "primary", mode: "mine", primary: true };
const TEAM_MINE = { id: "team@group", mode: "mine", primary: false };
const TEAM_ALL = { id: "team@group", mode: "all", primary: false };

function ev(start, end, extra = {}) {
  return Object.assign(
    { start: { dateTime: `${DAY}T${start}:00Z` }, end: { dateTime: `${DAY}T${end}:00Z` } },
    extra
  );
}
const classify = (e, cal = PRIMARY) => R.doorClassifyEvent(e, cal, ME, localDateMs);
const opts = (now) => ({ now: t(now), dayStart: t("00:00"), dayEnd: t("00:00") + 86400000, fmtHM, fmtIso });

test("solo event is busy, event with a guest is a meeting", () => {
  assert.equal(classify(ev("09:00", "10:00")).state, "busy");
  const guest = ev("09:00", "10:00", {
    attendees: [{ email: ME, self: true, responseStatus: "accepted" }, { email: "pat@example.com" }],
  });
  assert.equal(classify(guest).state, "meeting");
});

test("conference link alone makes a meeting", () => {
  assert.equal(classify(ev("09:00", "10:00", { hangoutLink: "https://meet.google.com/x" })).state, "meeting");
  assert.equal(classify(ev("09:00", "10:00", { conferenceData: { conferenceId: "x" } })).state, "meeting");
});

test("rooms and declined guests do not make a meeting", () => {
  const e = ev("09:00", "10:00", {
    attendees: [
      { email: ME, self: true },
      { email: "room@resource.calendar.google.com", resource: true },
      { email: "pat@example.com", responseStatus: "declined" },
    ],
  });
  assert.equal(classify(e).state, "busy");
});

test("focus time and out of office", () => {
  assert.equal(classify(ev("13:00", "15:00", { eventType: "focusTime" })).state, "focus");
  assert.equal(classify(ev("13:00", "15:00", { eventType: "outOfOffice" })).state, "away");
});

test("declined, free, cancelled and working location events are ignored", () => {
  assert.equal(classify(ev("09:00", "10:00", { attendees: [{ email: ME, self: true, responseStatus: "declined" }, { email: "x@y" }] })), null);
  assert.equal(classify(ev("09:00", "10:00", { transparency: "transparent" })), null);
  assert.equal(classify(ev("09:00", "10:00", { status: "cancelled" })), null);
  assert.equal(classify(ev("09:00", "10:00", { eventType: "workingLocation" })), null);
});

test("all-day events only count when out of office", () => {
  const allDay = { start: { date: DAY }, end: { date: "2026-11-21" } };
  assert.equal(classify(allDay), null);
  const ooo = classify(Object.assign({ eventType: "outOfOffice" }, allDay));
  assert.equal(ooo.state, "away");
  assert.equal(ooo.start, t("00:00"));
  assert.equal(ooo.end, t("00:00") + 86400000);
});

test("mine mode on a team calendar only counts Greg's events", () => {
  const others = ev("09:00", "10:00", { organizer: { email: "pat@example.com", self: true }, attendees: [{ email: "pat@example.com", self: true }] });
  assert.equal(classify(others, TEAM_MINE), null, "self flags on a team calendar are not Greg");
  const invited = ev("09:00", "10:00", { organizer: { email: "pat@example.com" }, attendees: [{ email: "pat@example.com" }, { email: ME }] });
  assert.equal(classify(invited, TEAM_MINE).state, "meeting");
  const organized = ev("09:00", "10:00", { organizer: { email: ME } });
  assert.equal(classify(organized, TEAM_MINE).state, "busy");
  const declined = ev("09:00", "10:00", { attendees: [{ email: "pat@example.com" }, { email: ME, responseStatus: "declined" }] });
  assert.equal(classify(declined, TEAM_MINE), null);
});

test("all mode on a team calendar counts everyone's events", () => {
  const others = ev("09:00", "10:00", { organizer: { email: "pat@example.com" }, attendees: [{ email: "pat@example.com" }, { email: "lee@example.com" }] });
  assert.equal(classify(others, TEAM_ALL).state, "meeting");
});

test("overlap priority: away > meeting > busy > focus", () => {
  const segs = [
    { start: t("09:00"), end: t("12:00"), state: "focus" },
    { start: t("10:00"), end: t("11:00"), state: "busy" },
    { start: t("10:30"), end: t("11:30"), state: "meeting" },
    { start: t("10:45"), end: t("10:50"), state: "away" },
  ];
  assert.equal(R.doorStateAt(segs, t("09:30")), "focus");
  assert.equal(R.doorStateAt(segs, t("10:15")), "busy");
  assert.equal(R.doorStateAt(segs, t("10:40")), "meeting");
  assert.equal(R.doorStateAt(segs, t("10:46")), "away");
  assert.equal(R.doorStateAt(segs, t("12:00")), "available");
});

test("status: in a meeting, until its end, then available", () => {
  const segs = [
    { start: t("09:00"), end: t("09:30"), state: "busy" },
    { start: t("14:00"), end: t("15:30"), state: "meeting" },
  ];
  const s = R.doorComputeStatus(segs, opts("14:12"));
  assert.equal(s.state, "meeting");
  assert.equal(s.until, `${DAY}T15:30:00Z`);
  assert.deepEqual(s.next, { state: "available", at: `${DAY}T15:30:00Z` });
  assert.deepEqual(s.blocks, [["09:00", "09:30"], ["14:00", "15:30"]]);
  assert.equal(s.label, null);
  assert.equal(s.generated, `${DAY}T14:12:00Z`);
});

test("status: available until the next event, open-ended after the last", () => {
  const segs = [{ start: t("14:00"), end: t("15:30"), state: "meeting" }];
  const before = R.doorComputeStatus(segs, opts("10:00"));
  assert.equal(before.state, "available");
  assert.equal(before.until, `${DAY}T14:00:00Z`);
  assert.equal(before.next.state, "meeting");
  const after = R.doorComputeStatus(segs, opts("16:00"));
  assert.equal(after.state, "available");
  assert.equal(after.until, null);
  assert.equal(after.next, null);
});

test("status: back-to-back meetings are one meeting; meeting then focus changes state", () => {
  const segs = [
    { start: t("10:00"), end: t("11:00"), state: "meeting" },
    { start: t("11:00"), end: t("12:00"), state: "meeting" },
    { start: t("12:00"), end: t("13:00"), state: "focus" },
  ];
  const s = R.doorComputeStatus(segs, opts("10:30"));
  assert.equal(s.until, `${DAY}T12:00:00Z`);
  assert.equal(s.next.state, "focus");
  assert.deepEqual(s.blocks, [["10:00", "13:00"]]);
});

test("blocks clip to today and end of day shows 24:00", () => {
  const segs = [{ start: t("23:00"), end: t("23:00") + 3 * 3600000, state: "busy" }];
  assert.deepEqual(R.doorComputeStatus(segs, opts("08:00")).blocks, [["23:00", "24:00"]]);
});

test("title of the current event is passed, never description or attendees", () => {
  const e = ev("14:00", "15:00", { summary: "  Budget   review ", description: "acquisition", attendees: [{ email: ME, self: true }, { email: "ceo@other.com", displayName: "Someone" }] });
  const seg = classify(e);
  const out = R.doorComputeStatus([seg], opts("14:30"));
  assert.equal(out.state, "meeting");
  assert.equal(out.title, "Budget review");
  assert.ok(!/acquisition|Someone|ceo@other/.test(JSON.stringify(out)));
  assert.deepEqual(Object.keys(seg).sort(), ["end", "start", "state", "title"]);
  assert.equal(R.doorComputeStatus([seg], opts("13:00")).title, null);
});

test("private and confidential events read busy with no title", () => {
  for (const visibility of ["private", "confidential"]) {
    const e = ev("14:00", "15:00", { visibility, summary: "SECRET board meeting", hangoutLink: "https://meet.google.com/x" });
    const out = R.doorComputeStatus([classify(e)], opts("14:30"));
    assert.equal(out.state, "busy");
    assert.equal(out.title, null);
    assert.ok(!/SECRET/.test(JSON.stringify(out)));
  }
  const ooo = classify(ev("14:00", "15:00", { visibility: "private", eventType: "outOfOffice", summary: "Doctor" }));
  assert.equal(ooo.state, "away");
  assert.equal(ooo.title, null);
});

test("long titles are capped; overlapping events show the later one", () => {
  assert.equal(classify(ev("09:00", "10:00", { summary: "\u{1F680} Launch — René’s plan" })).title, "Launch - René's plan");
  const long = classify(ev("09:00", "10:00", { summary: "x".repeat(100) })).title;
  assert.equal(long.length, 60);
  assert.ok(long.endsWith("..."));
  const a = classify(ev("09:00", "11:00", { summary: "Long block" }));
  const b = classify(ev("10:00", "10:30", { summary: "Quick sync" }));
  assert.equal(R.doorComputeStatus([a, b], opts("10:15")).title, "Quick sync");
  assert.equal(R.doorComputeStatus([a, b], opts("10:15")).titleUntil, fmtIso(t("10:30")));
  assert.equal(R.doorComputeStatus([a, b], opts("10:45")).title, "Long block");
});

test("cals parameter parsing", () => {
  assert.deepEqual(JSON.parse(JSON.stringify(R.doorParseCals(""))), [{ id: "primary", mode: "mine" }]);
  assert.deepEqual(JSON.parse(JSON.stringify(R.doorParseCals("primary:mine, team@group.calendar.google.com:all"))), [
    { id: "primary", mode: "mine" },
    { id: "team@group.calendar.google.com", mode: "all" },
  ]);
  assert.deepEqual(JSON.parse(JSON.stringify(R.doorParseCals("x@y:bogus"))), [{ id: "x@y", mode: "mine" }]);
});
