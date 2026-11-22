// Door Sign broker: calendar rules (SPEC section 6).
//
// Pure functions, no Apps Script services, so the same file runs under Node for
// tests (broker/test). Time zone work is injected by the caller (Code.gs).
//
// Privacy: nothing here reads or returns event titles, descriptions or
// attendee names. Output is states, times and busy blocks only.

var DOOR_PRIORITY = { away: 4, meeting: 3, busy: 2, focus: 1 };
var DOOR_IGNORED_TYPES = { workingLocation: true, birthday: true };

function doorLower_(s) {
  return String(s || "").toLowerCase();
}

// Turns one Calendar API event into { start, end, state } or null if it does
// not block Greg. cal = { id, mode: "mine" | "all", primary: bool }.
// me = Greg's email, lowercase. localDateMs("2026-11-20") -> local midnight ms.
function doorClassifyEvent(ev, cal, me, localDateMs) {
  if (!ev || ev.status === "cancelled") return null;
  var type = ev.eventType || "default";
  if (DOOR_IGNORED_TYPES[type]) return null;
  var allDay = !!(ev.start && ev.start.date);
  // Rule 5: all-day events only count when they are Out of Office.
  if (allDay && type !== "outOfOffice") return null;
  // Rule 4: "free" events never block.
  if (ev.transparency === "transparent" && type !== "outOfOffice") return null;

  // On a non-primary calendar, "self" flags refer to that calendar, so match by email.
  var isMe = function (p) {
    if (!p) return false;
    if (cal.primary && p.self) return true;
    return doorLower_(p.email) === me;
  };
  var attendees = ev.attendees || [];
  var mine = null;
  for (var i = 0; i < attendees.length; i++) {
    if (isMe(attendees[i])) {
      mine = attendees[i];
      break;
    }
  }
  // Rule 3: declined events never block.
  if (mine && mine.responseStatus === "declined") return null;
  // Rule 2: "mine" calendars only count events Greg organizes or attends.
  if (!cal.primary && cal.mode !== "all" && !mine && !isMe(ev.organizer)) return null;

  var state;
  if (type === "outOfOffice") state = "away";
  else if (type === "focusTime") state = "focus";
  else {
    // Rule 7: anyone else invited, or a conference link, makes it a meeting.
    var others = 0;
    for (var j = 0; j < attendees.length; j++) {
      var a = attendees[j];
      if (!isMe(a) && !a.resource && a.responseStatus !== "declined") others++;
    }
    var conf = !!(ev.hangoutLink || ev.conferenceData);
    state = others > 0 || conf ? "meeting" : "busy";
  }

  var start = allDay ? localDateMs(ev.start.date) : Date.parse(ev.start.dateTime);
  var end = allDay ? localDateMs(ev.end.date) : Date.parse(ev.end.dateTime);
  if (!(end > start)) return null;
  return { start: start, end: end, state: state };
}

// Rule 8: highest priority segment covering t, else "available".
function doorStateAt(segs, t) {
  var best = "available";
  var bestP = 0;
  for (var i = 0; i < segs.length; i++) {
    var s = segs[i];
    if (s.start <= t && t < s.end && DOOR_PRIORITY[s.state] > bestP) {
      best = s.state;
      bestP = DOOR_PRIORITY[s.state];
    }
  }
  return best;
}

// Today's blocked time as merged [start, end] pairs clipped to the day.
function doorBlocks(segs, dayStart, dayEnd, fmtHM) {
  var spans = [];
  for (var i = 0; i < segs.length; i++) {
    var a = Math.max(segs[i].start, dayStart);
    var b = Math.min(segs[i].end, dayEnd);
    if (b > a) spans.push([a, b]);
  }
  spans.sort(function (x, y) {
    return x[0] - y[0];
  });
  var merged = [];
  for (var k = 0; k < spans.length; k++) {
    var last = merged[merged.length - 1];
    if (last && spans[k][0] <= last[1]) last[1] = Math.max(last[1], spans[k][1]);
    else merged.push([spans[k][0], spans[k][1]]);
  }
  return merged.map(function (m) {
    return [fmtHM(m[0]), m[1] >= dayEnd ? "24:00" : fmtHM(m[1])];
  });
}

// Builds the broker status response from classified segments.
// opts = { now, dayStart, dayEnd, fmtHM(ms) -> "HH:mm", fmtIso(ms) -> ISO with offset }
function doorComputeStatus(segs, opts) {
  var now = opts.now;
  var state = doorStateAt(segs, now);
  var points = [];
  for (var i = 0; i < segs.length; i++) {
    if (segs[i].start > now) points.push(segs[i].start);
    if (segs[i].end > now) points.push(segs[i].end);
  }
  points.sort(function (a, b) {
    return a - b;
  });
  var until = null;
  var next = null;
  for (var k = 0; k < points.length; k++) {
    var s = doorStateAt(segs, points[k]);
    if (s !== state) {
      until = points[k];
      next = { state: s, at: opts.fmtIso(points[k]) };
      break;
    }
  }
  return {
    state: state,
    until: until === null ? null : opts.fmtIso(until),
    next: next,
    blocks: doorBlocks(segs, opts.dayStart, opts.dayEnd, opts.fmtHM),
    label: null,
    generated: opts.fmtIso(now),
  };
}

// "id:mode,id:mode" -> [{ id, mode }]. Defaults to the primary calendar.
function doorParseCals(param) {
  var out = [];
  String(param || "")
    .split(",")
    .forEach(function (part) {
      part = part.trim();
      if (!part) return;
      var i = part.lastIndexOf(":");
      var id = i > 0 ? part.slice(0, i) : part;
      var mode = i > 0 ? part.slice(i + 1) : "mine";
      if (mode !== "all") mode = "mine";
      out.push({ id: id, mode: mode });
    });
  if (!out.length) out.push({ id: "primary", mode: "mine" });
  return out;
}

if (typeof module !== "undefined") {
  module.exports = {
    doorClassifyEvent: doorClassifyEvent,
    doorStateAt: doorStateAt,
    doorBlocks: doorBlocks,
    doorComputeStatus: doorComputeStatus,
    doorParseCals: doorParseCals,
  };
}
