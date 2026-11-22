// Door Sign broker: web app entry point (SPEC section 6).
//
// GET ?token=...&action=status&cals=<id>:<mode>,...[&at=<epoch seconds>]
//   at: compute the status as of another time (the sign's time preview).
// GET ?token=...&action=calendars
//
// Script Properties: TOKEN (required), TZ (default America/Chicago).
// Run doorSetup() once from the editor to create a token.

var DOOR_CACHE_SECONDS = 55;
var DOOR_EVENT_FIELDS =
  "items(status,eventType,transparency,visibility,summary,start,end," +
  "attendees(email,self,responseStatus,resource),organizer(email,self)," +
  "hangoutLink,conferenceData(conferenceId)),nextPageToken";

function doGet(e) {
  try {
    var props = PropertiesService.getScriptProperties();
    var token = props.getProperty("TOKEN");
    var p = (e && e.parameter) || {};
    if (!token || p.token !== token) return doorJson_({ error: "unauthorized" });
    var tz = props.getProperty("TZ") || "America/Chicago";
    var action = p.action || "status";
    if (action === "calendars") return doorJson_(doorCalendars_());
    if (action === "status") return doorJson_(doorStatus_(p.cals || "", tz, doorAt_(p.at)));
    return doorJson_({ error: "unknown action" });
  } catch (err) {
    return doorJson_({ error: String((err && err.message) || err) });
  }
}

// Only calendar names travel on this call, never events.
function doorCalendars_() {
  var items = [];
  var pageToken;
  do {
    var res = Calendar.CalendarList.list({ maxResults: 250, pageToken: pageToken, fields: "items(id,summary,summaryOverride,backgroundColor,primary),nextPageToken" });
    (res.items || []).forEach(function (c) {
      items.push({ id: c.primary ? "primary" : c.id, name: c.summaryOverride || c.summary || c.id, color: c.backgroundColor || null, primary: !!c.primary });
    });
    pageToken = res.nextPageToken;
  } while (pageToken);
  items.sort(function (a, b) {
    return (b.primary ? 1 : 0) - (a.primary ? 1 : 0) || a.name.localeCompare(b.name);
  });
  return { calendars: items };
}

// Optional preview time: epoch seconds within 90 days of now, else null.
function doorAt_(param) {
  var s = Number(param);
  if (!param || !isFinite(s)) return null;
  var ms = Math.round(s) * 1000;
  return Math.abs(ms - Date.now()) <= 90 * 86400000 ? ms : null;
}

function doorStatus_(calsParam, tz, atMs) {
  var cache = CacheService.getScriptCache();
  var atKey = atMs ? "|" + Math.floor(atMs / 60000) : "";
  var key = "st:" + Utilities.base64EncodeWebSafe(Utilities.computeDigest(Utilities.DigestAlgorithm.MD5, calsParam + "|" + tz + atKey));
  var hit = cache.get(key);
  if (hit) return JSON.parse(hit);

  var me = String(Session.getEffectiveUser().getEmail() || "").toLowerCase();
  var localDateMs = function (d) {
    return Utilities.parseDate(d + " 00:00:00", tz, "yyyy-MM-dd HH:mm:ss").getTime();
  };
  var fmt = function (ms, pattern) {
    return Utilities.formatDate(new Date(ms), tz, pattern);
  };
  var now = atMs || Date.now();
  var dayStart = localDateMs(fmt(now, "yyyy-MM-dd"));
  var dayEnd = localDateMs(fmt(dayStart + 36 * 3600000, "yyyy-MM-dd")); // DST-safe
  var windowEnd = dayEnd + 12 * 3600000; // look past midnight for "until"

  var segs = [];
  doorParseCals(calsParam).forEach(function (c) {
    var cal = { id: c.id, mode: c.mode, primary: c.id === "primary" || c.id.toLowerCase() === me };
    var pageToken;
    do {
      var res = Calendar.Events.list(c.id, {
        timeMin: new Date(dayStart).toISOString(),
        timeMax: new Date(windowEnd).toISOString(),
        singleEvents: true,
        orderBy: "startTime",
        maxResults: 250,
        pageToken: pageToken,
        fields: DOOR_EVENT_FIELDS, // descriptions are never requested
      });
      (res.items || []).forEach(function (ev) {
        var s = doorClassifyEvent(ev, cal, me, localDateMs);
        if (s) segs.push(s);
      });
      pageToken = res.nextPageToken;
    } while (pageToken);
  });

  var out = doorComputeStatus(segs, {
    now: now,
    dayStart: dayStart,
    dayEnd: dayEnd,
    fmtHM: function (ms) {
      return fmt(ms, "HH:mm");
    },
    fmtIso: function (ms) {
      return fmt(ms, "yyyy-MM-dd'T'HH:mm:ssXXX");
    },
  });
  cache.put(key, JSON.stringify(out), DOOR_CACHE_SECONDS);
  return out;
}

function doorJson_(obj) {
  return ContentService.createTextOutput(JSON.stringify(obj)).setMimeType(ContentService.MimeType.JSON);
}

// Run once from the Apps Script editor. Creates a random token if none exists
// and logs it, so it can be entered on the sign's Settings page.
function doorSetup() {
  var props = PropertiesService.getScriptProperties();
  var token = props.getProperty("TOKEN");
  if (!token) {
    token = (Utilities.getUuid() + Utilities.getUuid()).replace(/-/g, "");
    props.setProperty("TOKEN", token);
  }
  if (!props.getProperty("TZ")) props.setProperty("TZ", "America/Chicago");
  Logger.log("Broker token: " + token);
  Logger.log("Status now: " + JSON.stringify(doorStatus_("primary:mine", props.getProperty("TZ"))));
}
