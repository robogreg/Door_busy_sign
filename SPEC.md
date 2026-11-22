# Door Sign: Office Door Status Display

Owner: Greg Needel
Status: Spec v1, ready to build (Nov 22, 2026)

## 1. Summary

An e-paper sign mounted beside Greg's office door that shows whether he is available, in a meeting, on a call, heads down, busy, or away.

- Status follows Google Calendar automatically.
- Privacy by design: attendees and descriptions never leave Google. The current event's title is shown on the sign unless the event is marked private, in which case it reads "busy" with no title (changed Nov 22, 2026; titles were originally excluded).
- Manual override from a web page hosted on the device itself, including one-tap bookmark URLs (for example "on a call for 30 min").
- Dashboard-style display with a rotating library of funny one-liners per state.
- Built and tested at home, then moved to the office, so Wi-Fi changes must never require a reflash.

## 2. Goals and non-goals

### Goals
1. Calendar-driven status with zero daily effort.
2. No calendar text ever reaches the device.
3. Manual override in two taps from a laptop or phone on the same network; one-tap bookmark URLs.
4. Readable from across the hall, looks good, has personality.
5. Survives reboots, Wi-Fi drops, router reboots and Google outages gracefully.
6. Easy network moves (home to office) with no computer needed.

### Non-goals (v1)
- Showing attendees or descriptions.
- Battery operation. The sign is always on, USB-C powered.
- Internet-wide remote control. Overrides are LAN only.

## 3. Hardware

| Item | Detail |
|---|---|
| Controller | Waveshare E-Paper ESP32 Driver Board. ESP32 (WROOM-32 class), 240 MHz, 520 KB SRAM, 4 MB flash, no PSRAM |
| Wi-Fi | 802.11 b/g/n, 2.4 GHz only |
| USB | USB-C on boards made Dec 2024 or later (micro USB before). USB-serial: CH343 (CP2102 on boards before July 2022) |
| Panel | Waveshare 7.5" e-Paper (B) V2, 800 x 480, red / black / white, 163.2 x 97.92 mm active, 170.2 x 111.2 mm outline |
| Panel timing | Full refresh 26 s, fast refresh 16 s, black/white partial refresh 1.5 s |
| Power | 5 V USB-C, about 50 to 150 mA with Wi-Fi active. Panel draws power only while refreshing (48 mW typ.) |
| Board switch 1 | Display select. 7.5" (B) is in the 0.47R group. Try position A first, then B |
| Board switch 2 | USB-UART power. Must be ON to upload firmware |

### Fixed SPI pin map (board-wired, not changeable)

| Signal | GPIO |
|---|---|
| DIN / MOSI | 14 |
| SCLK | 13 |
| CS | 15 |
| DC | 27 |
| RST | 26 |
| BUSY | 25 |

These are NOT the ESP32 default SPI pins. SPI must be remapped in firmware (see GxEPD2 example `GxEPD2_WS_ESP32_Driver`).

### Panel constraints that drive the design
- Tri-color updates take 16 to 26 s and visibly flash. Redraw only when something meaningful changes. Never on a clock tick.
- Black/white partial refresh (1.5 s) is the only quick path; behavior with the red layer is unknown until bench tested.
- Put the panel to sleep after every update. Do a full clean refresh at least daily to prevent ghosting.
- BUSY waits must allow at least 30 s for a tri-color refresh before treating it as a timeout.

### Mounting and power (decided)
- Wall-mounted beside the office door, USB-C to a nearby power port, always on.
- Enclosure: laser-cut or 3D-printed frame, front bezel, driver board on the back, recessed USB-C with strain relief, keyhole wall mount. REV-orange accent.

## 4. Architecture

Three components:

1. **Status broker** (Google Apps Script, runs as Greg in REV's Google Workspace). Reads the selected calendars, computes a status, returns a small JSON blob. No calendar text leaves Google except calendar names on the picker call.
2. **Door device** (ESP32 firmware). Polls the broker every 60 s, merges manual overrides, decides what to show, renders only on change, panel sleeps.
3. **Control page** (served by the ESP32). Overrides, custom messages, settings, Wi-Fi, excuse library, OTA.

Data flow:
```
Google Calendar -> Apps Script broker -> (HTTPS GET every 60 s) -> ESP32
ESP32: override > calendar > working-hours default -> render if changed -> panel sleep
Laptop/phone -> http://<sign-ip>/ -> override stored in NVS -> immediate re-render
```

### Why Apps Script (decided)
| Option | Privacy | Device complexity | Verdict |
|---|---|---|---|
| Apps Script status broker | Only state + times leave Google | One HTTPS GET, small JSON | Chosen |
| Secret iCal URL parsed on ESP32 | Full calendar text on device; URL is a full-access secret | High (recurrence, TZ, size vs 520 KB RAM) | Rejected |
| Calendar API with OAuth on ESP32 | Refresh token on a door | Medium/high | Rejected |
| Free/busy API only | Good | Medium | Rejected, loses meeting/focus/OOO distinction |

Greg is super admin of REV's Workspace, so the web app can be deployed "execute as me, access: anyone", protected by a long random token.

## 5. Status model

| State id | Label on sign | Source | Treatment | Default override length |
|---|---|---|---|---|
| `available` | AVAILABLE | Calendar (no event) or manual | Black on white | 1 h |
| `meeting` | IN A MEETING | Calendar or manual | White on red band | 1 h |
| `call` | ON A CALL | Manual only | White on red band | 30 min |
| `focus` | HEADS DOWN | Calendar (Focus Time) or manual | White on black band | 1 h |
| `busy` | BUSY | Calendar (solo event) or manual | White on black band | 1 h |
| `away` | AWAY | Calendar (Out of Office) or manual | Black on white, muted | 1 h |
| `gone` | GONE FOR THE DAY | Outside working hours | Black on white, muted | Automatic |
| `custom` | free text | Manual | User-chosen band (red/black/none) | 1 h |

A bare override (no duration given) always rolls back to the calendar on its own: 30 min for a call, 1 h for everything else (decided Nov 22, 2026). `d=clear` keeps it until cleared, `d=event` until the current calendar event ends.

## 6. Calendar rules (implemented in Apps Script)

1. Read the calendars passed in by the device (picked on the device's Settings page), from now to end of day. Default: primary only.
2. Each picked calendar has a mode:
   - `mine` (default for non-primary calendars): count an event only if Greg is organizer or attendee.
   - `all`: any event on that calendar blocks Greg.
3. Ignore events Greg declined.
4. Ignore events with transparency = transparent ("free").
5. Ignore all-day events except Out of Office.
6. `eventType == outOfOffice` -> `away`. `eventType == focusTime` -> `focus`. Requires the Advanced Calendar Service (CalendarApp does not expose eventType).
7. Event with at least one other attendee or a conference link -> `meeting`. Solo event -> `busy`.
8. Overlaps across all picked calendars: priority `away` > `meeting` > `busy` > `focus`.
9. Outside working hours with nothing scheduled -> `gone`.
10. Private or confidential events (Google visibility) count as `busy` even with guests or a Meet link, and their title is never returned. Other events pass their title, reduced to Latin-1 and capped at 60 characters.

### Broker API

`GET <script-url>?token=...&action=status&cals=<id>:<mode>,<id>:<mode>`

```json
{
  "state": "meeting",
  "until": "2026-11-20T15:30:00-06:00",
  "next": { "state": "available", "at": "2026-11-20T15:30:00-06:00" },
  "blocks": [["09:00","09:30"],["14:00","15:30"]],
  "label": null,
  "title": "Budget review",
  "titleUntil": "2026-11-20T15:00:00-06:00",
  "generated": "2026-11-20T14:12:03-06:00"
}
```
- `blocks`: today's busy time as start/end pairs only (for the timeline strip). No titles.
- `until`: end of the current state (null if open-ended).
- `title`: title of the event that sets the current state (highest priority, latest start on ties), null if none or private. `titleUntil` is that event's end, since back-to-back events share one `until`.

`GET <script-url>?token=...&action=calendars`

```json
{ "calendars": [ { "id": "primary", "name": "Greg Needel", "color": "#4285f4", "primary": true }, ... ] }
```
Only calendar names travel on this call, never events.

Errors: `{ "error": "unauthorized" }` or `{ "error": "<message>" }` with HTTP 200 (Apps Script limitation); device treats any `error` key as a failed poll.

### Broker implementation notes
- Standalone Apps Script project, Advanced Calendar Service enabled.
- Token and time zone (America/Chicago) in Script Properties. Working hours live only on the device (M3 decision, Nov 22, 2026): the broker returns `available` when nothing is scheduled and the device turns that into `gone` outside working hours (rule 9). Calendar events outside working hours still show.
- Event fields are requested with a `fields` mask that excludes descriptions and attendee names, so they never enter the script. `summary` and `visibility` are requested for the title rule.
- Cache computed status ~60 s per `cals` key (CacheService). One call/min is ~1,440/day, within quotas.
- Keep in repo under `broker/` and deploy with `clasp`.

## 7. Override and priority logic

Priority: **manual override > calendar > working-hours default**.

- Every override has an expiry: 15 min, 30 min, 1 h, until current calendar event ends, until end of day, or until cleared.
- On expiry, fall back to calendar at the next poll (or immediately using the last cached broker response).
- "Back to calendar" clears the override immediately.
- Active override persists in NVS so a power blip doesn't drop it.
- If the broker has been unreachable 10+ minutes, keep showing the last known state with a small "stale" icon. Manual overrides always work.

## 8. Device control page

Served by the ESP32 on port 80. Reachable at the sign's IP and `http://door.local` (mDNS may be blocked on the office network; IP is the reliable path).

### Pages
- **Home:** current status preview, big buttons (Available, On a call, Meeting, Heads down, Busy, Away), duration chips, custom message box with band color, "Back to calendar."
- **Settings:** broker URL and token, calendar picker (checklist from `action=calendars`, each with mine/all mode), display name and title, working hours, time zone, humor level (off / dry / full), daily clean-refresh time, PIN.
- **Wi-Fi:** saved networks (see section 9).
- **Excuse library:** view, add, edit, delete quips per state (JSON in LittleFS).
- **System:** IP, MAC, SSID, RSSI, uptime, firmware version, last broker response, recent log, OTA firmware upload, reboot.

### One-tap endpoints (GET, for bookmarks / iPhone Shortcuts / Stream Deck)
| URL | Effect |
|---|---|
| `/s/call?d=30&k=PIN` | On a call for 30 min |
| `/s/meeting?d=60&k=PIN` | Meeting for 60 min |
| `/s/busy?d=60&k=PIN` | Busy for 60 min |
| `/s/focus?d=60&k=PIN` | Heads down |
| `/s/away?d=clear&k=PIN` | Away until cleared |
| `/s/available?k=PIN` | Available (override) |
| `/s/custom?m=Back+at+3&c=red&d=60&k=PIN` | Custom message |
| `/s/clear?k=PIN` | Back to calendar |
| `/status.json` | Read-only current state (no PIN) |

`d` = minutes; `d=event` = until current calendar event ends; omitted = state default. Responses return a tiny HTML confirmation page (for bookmark use) or JSON when `Accept: application/json`.

### Security (sized for the threat)
- Plain HTTP on the LAN. The PIN is prank prevention, not real security.
- Changes require the PIN (cookie after first login on the web UI; bookmark URLs carry `k=`).
- Viewing status needs nothing.
- Wi-Fi passwords and broker token are write-only in the UI (never echoed back).

## 9. Wi-Fi and network

### Facts
- Office network: REV Wi-Fi, WPA2-PSK (shared password). No 802.1X.
- ESP32 is 2.4 GHz only; dual-band SSIDs are fine if 2.4 GHz is enabled.
- Unknown: whether the office network isolates clients or blocks mDNS. Test during bring-up.

### Home-to-office moves
- **Saved network list:** up to 5 networks (SSID, password, optional static IP config) in NVS. On boot, scan and join the strongest known network. Greg adds the REV network at home before moving the sign.
- **Wi-Fi page:** list, add, edit, delete, reorder saved networks.
- **Setup mode fallback:** if no known network connects within ~30 s at boot, start a hotspot `DoorSign-Setup` (WPA2, password shown on the panel) with a captive portal for choosing a network and entering the password.
- **On-screen setup instructions:** in setup mode the panel shows "Wi-Fi setup needed", hotspot name and password, and a Wi-Fi QR code (`WIFI:T:WPA;S:DoorSign-Setup;P:...;;`).
- **Force setup mode:** hold BOOT (GPIO0) 5 s after power-up. Verify the button is reachable on this board; if not, add a recessed enclosure button on a spare GPIO.
- **Failsafe:** if the sign was online and loses Wi-Fi, keep showing the last status and retry quietly for 10 minutes before falling back to setup mode (so a router reboot doesn't put the door in setup mode).

### Fixed IP at the office
- IT reserves addresses by MAC. Preferred: DHCP reservation on the REV network for the sign's MAC (no device changes).
- Fallback: per-network static IP (address, gateway, subnet, DNS) on the Wi-Fi page, so home stays on DHCP.

### IP visibility
- **Boot screen:** on every power-up, show network name, IP, MAC, `door.local`, and firmware version. Hold ~20 s after connecting, then switch to the status layout.
- **Footer:** IP address in small text, bottom right, always visible.
- Same details on the System page and in the serial log at boot.

## 10. Display layout (800 x 480)

| Zone | Height | Content |
|---|---|---|
| Header | ~50 px | "GREG NEEDEL" left, title right (e.g. "CEO, REV Robotics", configurable) |
| Status band | ~200 px | State label in huge type, inverted on red or black per the status table. Mood face at the right end with a "Current mood" caption above it |
| Quip | ~70 px | Rotating one-liner, larger italic. During a non-private calendar event, the event title replaces it |
| Time line | ~50 px | "Free at 3:30 PM" / "Free until 2:00 PM" / "Back tomorrow" |
| Timeline strip | ~70 px | 8a to 6p (follows working hours), busy blocks filled black, red "now" tick, no titles |
| Footer | ~30 px | Icons: manual override, stale data, Wi-Fi. Last updated time. IP address bottom right |

### Other screens
- Boot / network screen (section 9).
- Wi-Fi setup screen with QR code (section 9).
- Error screen for unrecoverable faults (message + IP if known).

### Refresh policy
- Full tri-color refresh on any change to state, quip, "until" text, or label.
- Timeline "now" tick moves at most every 15 min (partial refresh if bench testing shows it's clean, otherwise full).
- No live clock on the panel.
- Daily clean refresh at a configurable time (default 5:00 AM).
- Latency from button press to final image: 16 to 26 s expected. If it feels slow for "On a call", test a black/white partial preview followed by the full color pass.

### Rendering
- Render the whole layout into a frame model and compare a hash with what's on the panel; only refresh if different.
- Use GxEPD2 paged drawing (two 800 x 480 bit planes = ~96 KB, tight without PSRAM).
- Fonts via U8g2_for_Adafruit_GFX (large bold for the status band).
- Provide a host-side preview tool (section 12) so layouts can be iterated without the 26 s refresh.

## 11. Humor: excuse library

- One quip picked at random on each state change (not every refresh), no repeats until the list cycles.
- Humor level "off" shows a plain status line (e.g. "Free at 3:30 PM" only); "dry" uses a tamer subset (flag per quip); "full" uses all.
- Stored as `data/quips.json`, editable on the device.

Format:
```json
{ "available": [ { "text": "Door's open. Questions welcome, solutions preferred.", "level": "dry" }, ... ], ... }
```

### Starter set

**available**
- Door's open. Questions welcome, solutions preferred.
- Accepting visitors, bribes and pull requests.
- Status: idle loop. Interrupts enabled.
- All systems nominal. Knock anyway.
- Calendar is empty. Suspicious, but true.
- Come on in. The robots are friendly.
- Ask me anything. I'll pretend I know.
- Low latency mode: response within one knock.

**meeting**
- In a meeting. Probably scheduling another meeting.
- This meeting could have been an email.
- Interrupts masked. Retry after the ISR.
- Currently being synergized.
- Discussing things that will become your action items.
- Aligning stakeholders. Send snacks.
- Building consensus, one slide at a time.
- Nodding thoughtfully. Back soon.

**call**
- On a call. Pretend this door is soundproof.
- On the phone. Yes, an actual phone call.
- Bandwidth fully allocated.
- Voice channel occupied. Packets will be dropped.
- Hand signals accepted. Knocking is not.
- My mute button cannot be trusted. Shh.
- Negotiating. Your silence helps the price.
- Probably talking to Shenzhen.

**focus**
- Heads down. Thinking expensive thoughts.
- Compiling. Do not disturb the build.
- In the zone. The zone has a strict door policy.
- Interrupt only for fire or free food.
- Turning coffee into decisions.
- Running at 100% CPU. Fan noise expected.
- Each knock costs 23 minutes of focus. Choose wisely.

**busy**
- Busy. Like, actually busy.
- Request queued. Please hold.
- Saving the world, one gearbox at a time.
- Leave a message at the beep. There is no beep.
- Urgent? Text me. On fire? Pull the alarm.
- Occupied. Come back with more context.

**away / gone**
- Away. The office plant is in charge.
- Gone for the day. The robots are on night shift.
- Out of the office. Probably on a plane.
- Walking the warehouse. Back soon.
- Currently in another time zone, mentally or physically.
- Back tomorrow. This door will wait.
- Out. Not out out. Just out.

## 12. Software stack and repo layout

### Firmware
- PlatformIO, Arduino-ESP32 core, board `esp32dev`.
- GxEPD2 with `GxEPD2_750c_Z08` (800 x 480 red/black/white), SPI remapped to the pins in section 3.
- U8g2_for_Adafruit_GFX for fonts.
- ESPAsyncWebServer + AsyncTCP (current maintained forks that build on Arduino-ESP32 core 3.x).
- ArduinoJson v7.
- ESPmDNS, DNSServer (captive portal), Preferences (NVS), LittleFS.
- NTP with POSIX TZ `CST6CDT,M3.2.0,M11.1.0`.
- HTTPS client for the broker (Google redirects Apps Script responses: follow redirects; use the bundled CA store or pin Google roots).
- OTA via the System page (Update library).
- Task watchdog; reboot if the main loop stalls.
- Do not write a custom Wi-Fi manager on top of WiFiManager; the multi-network list + setup mode is custom but small.

### Suggested layout
```
door-sign/
  CLAUDE.md
  SPEC.md
  firmware/
    platformio.ini
    src/
      main.cpp
      config.h            # pins, defaults, version
      display/            # layout, screens, render hash, fonts
      net/                # wifi manager (multi-network, setup mode, captive portal)
      web/                # async server, routes, PIN auth
      broker/             # HTTPS poll + JSON parse
      state/              # status resolver, overrides, expiry
      storage/            # NVS settings, LittleFS quips
    data/                 # LittleFS: web UI (gzipped), quips.json
    test/                 # PlatformIO native unit tests (resolver, rules, expiry)
  broker/
    Code.gs               # Apps Script broker
    appsscript.json
    .clasp.json.example
  tools/
    preview/              # host-side renderer: same layout code -> PNG (800x480, 3 colors)
  enclosure/              # DXF/STL later
```

### Testing
- Status resolver (override vs calendar vs hours, expiry) as pure C++ with PlatformIO `native` tests.
- Broker rules unit-tested in Apps Script with fixture events, or ported to a small Node test harness.
- Host-side layout preview generating PNGs for every state, setup screen and boot screen.

### Secrets
- No secrets in the repo. Wi-Fi credentials, broker token and PIN are entered on the device and stored in NVS.
- Optional `firmware/src/secrets.h` (gitignored, with `secrets.example.h`) to pre-seed a dev Wi-Fi network for bench work only.

## 13. Build milestones and acceptance criteria

### M1: Bench bring-up (the 2025 attempt failed here)
Likely past failure causes, most likely first: wrong SPI pins (generic examples), wrong panel driver (V1 640x384 vs V2 800x480), switch 1 position, switch 2 off during upload, flex cable seating, Arduino core 3.x library breakage, BUSY timeout shorter than 26 s.

Steps:
1. Flash Waveshare's local demo for 7.5" (B) V2 (Arduino IDE, "ESP32 Dev Module", switch 2 ON). Proves board, cable, switches.
2. Run GxEPD2 `GxEPD2_WS_ESP32_Driver` with `GxEPD2_750c_Z08`. Proves the library path.
3. PlatformIO sketch: static mock of the real layout with large fonts. Log full refresh time. Try black/white partial refresh and note red-layer behavior.
4. Join Wi-Fi, serve a hello page, reach it by IP and `door.local` from a laptop.

Done when: layout mock renders correctly in red/black/white, refresh timings logged, page reachable.

### M2: Manual-only sign
Multi-network Wi-Fi + setup mode + captive portal + QR screen, boot/network screen, control page Home/Wi-Fi/System, PIN, one-tap endpoints, override storage and expiry, quips, render-on-change.

Done when: every state and custom message can be set from the page and bookmarks, overrides survive a power cycle and expire correctly, sign moves between two networks without reflashing.

Scope note (Nov 22, 2026): multi-network Wi-Fi, setup mode, captive portal and the QR screen are deferred. Wi-Fi comes from `secrets.h` and the sign is reflashed for the office network before install. The quip editor UI stays in M4.

### M3: Calendar
Apps Script broker (status + calendars actions), token, calendar picker on Settings, polling, rules, timeline strip, stale handling.

Done when: a test meeting, focus block, OOO event, solo event and declined event each produce the right state; no event text appears in any device log or response.

Passed Nov 22, 2026: solo, meeting (Meet link), focus and OOO verified live on the sign, plus OOO over focus priority; no test title text found on the device. Declined is covered by broker unit tests only, since Google drops a self-decline on your own event.

### M4: Polish
Final typography and layout, humor levels, excuse library editor, OTA, daily clean refresh, error screen.

### M5: Enclosure and install
Frame, mount, cable routing, DHCP reservation at the office.

### M6: Nice-to-haves
- Desk "on a call" button (second ESP32 hitting the bookmark URL) or Stream Deck key.
- QR code on Available/Busy screens linking to a Google appointment booking page.
- Sync status to chat presence.
- "Knock" button on the sign that pings Greg's phone.
- Opt-in `#door` event labels.
- Multiple signs from the same firmware.

## 14. Decisions and open questions

### Decided
1. Mount: wall beside the door, USB-C, always on.
2. Wi-Fi: REV network, shared password (PSK). Multi-network list for home-to-office moves.
3. Calendars: primary plus team calendars, chosen on the device's Settings page, per-calendar mine/all mode.
4. Workspace policy: Greg is super admin; Apps Script web app deployed with "anyone" access behind a token.
5. IP: DHCP reservation by MAC preferred; IP and MAC shown on boot screen, IP always in footer.

### Open
1. Opt-in event labels: recommended for v2.
2. ~~Black/white partial refresh behavior on the (B) panel.~~ Answered in M1 (Nov 22, 2026): with GxEPD2 `GxEPD2_750c_Z08`, a partial window refresh takes the full 26 s and degrades red and black in every column the window spans, top to bottom (red faded to pink, black to grey). Use full refreshes only. A fast black/white path would need custom LUTs; revisit in M4 if at all. Measured full refresh: 26.05 s busy, about 27 s total.
3. Does the REV Wi-Fi isolate clients or block mDNS? (answered in M1 step 4)
4. Is the BOOT button reachable on this board for forced setup mode? (answered in M1)
