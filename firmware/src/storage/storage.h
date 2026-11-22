// NVS settings and override persistence, LittleFS quip library.
#pragma once

#include <string>

#include "status.h"

void storageBegin();  // mounts LittleFS, seeds quips.json if missing

void loadSettings();  // into app.settings
void saveSettings();
void loadOverride();  // into app.ovr
void saveOverride();

// Quip library
void loadQuips();
// Random quip for a state at a humor level (0 off -> ""). No repeats until the list cycles.
std::string pickQuip(door::State s, uint8_t humor);
std::string quipsJson();                    // raw file for the web UI
bool saveQuipsJson(const std::string& json);  // validates, writes, reloads
