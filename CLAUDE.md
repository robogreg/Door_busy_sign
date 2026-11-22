# CLAUDE.md

Project: Door Sign, an ESP32 + 7.5" tri-color e-paper office door status display synced to Google Calendar. Full spec in `SPEC.md`; read it before making changes.

## Ground rules
- Work milestone by milestone (SPEC.md section 13). Don't start M2 features until M1 bring-up passes on real hardware.
- The display panel is a Waveshare 7.5" e-Paper (B) V2, 800 x 480, red/black/white. GxEPD2 class: `GxEPD2_750c_Z08`.
- SPI pins are board-wired and non-default: MOSI 14, SCLK 13, CS 15, DC 27, RST 26, BUSY 25. Always remap SPI.
- A tri-color refresh takes up to 26 s. Never refresh on a timer tick; render only when the computed frame changes. BUSY timeouts must be at least 30 s.
- No PSRAM: use GxEPD2 paged drawing; watch heap when the web server is running.
- Privacy: calendar event titles, attendees and descriptions must never reach the device. The Apps Script broker returns only states, times and busy blocks.
- No secrets in the repo. Wi-Fi credentials, broker token and PIN live in NVS. `secrets.h` is gitignored dev-only.
- Use PlatformIO with the Arduino-ESP32 core. Prefer maintained library forks that build on core 3.x.
- Keep the status resolver and calendar rules as pure, unit-testable code (PlatformIO native tests).
- Don't use em dashes in UI text, docs or comments. Write REV in all caps.

## Commands (once scaffolded)
- Build: `pio run -d firmware`
- Flash: `pio run -d firmware -t upload` (board switch 2 must be ON)
- Upload web UI/data: `pio run -d firmware -t uploadfs`
- Serial monitor: `pio device monitor -d firmware -b 115200`
- Native tests: `pio test -d firmware -e native`
- Broker tests: `cd broker && npm test`
- Broker deploy: `cd broker && clasp push && clasp deploy`
