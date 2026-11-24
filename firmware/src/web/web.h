// Control page and one-tap endpoints (SPEC section 8).
#pragma once

void webBegin();

// Every 60 s the sign fetches /ping from its own web server. After three misses
// in a row (once one check has passed) it restarts. Returns true on the boot
// that follows such a restart.
void webStartSelfCheck();
bool webSelfCheckRestarted();
// Seconds since the last passed self-check, -1 if none yet.
long webSelfCheckAgo();
