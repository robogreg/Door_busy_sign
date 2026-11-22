// Starter excuse library (SPEC section 11). Written to LittleFS /quips.json on
// first boot; edit on the device after that.
#pragma once

static const char kDefaultQuips[] PROGMEM = R"JSON({
"available": [
  {"text": "Door's open. Questions welcome, solutions preferred.", "level": "dry"},
  {"text": "Accepting visitors, bribes and pull requests.", "level": "full"},
  {"text": "Status: idle loop. Interrupts enabled.", "level": "dry"},
  {"text": "All systems nominal. Knock anyway.", "level": "dry"},
  {"text": "Calendar is empty. Suspicious, but true.", "level": "full"},
  {"text": "Come on in. The robots are friendly.", "level": "dry"},
  {"text": "Ask me anything. I'll pretend I know.", "level": "full"},
  {"text": "Low latency mode: response within one knock.", "level": "dry"}
],
"meeting": [
  {"text": "In a meeting. Probably scheduling another meeting.", "level": "full"},
  {"text": "This meeting could have been an email.", "level": "full"},
  {"text": "Interrupts masked. Retry after the ISR.", "level": "dry"},
  {"text": "Currently being synergized.", "level": "full"},
  {"text": "Discussing things that will become your action items.", "level": "full"},
  {"text": "Aligning stakeholders. Send snacks.", "level": "full"},
  {"text": "Building consensus, one slide at a time.", "level": "dry"},
  {"text": "Nodding thoughtfully. Back soon.", "level": "dry"}
],
"call": [
  {"text": "On a call. Pretend this door is soundproof.", "level": "dry"},
  {"text": "On the phone. Yes, an actual phone call.", "level": "dry"},
  {"text": "Bandwidth fully allocated.", "level": "dry"},
  {"text": "Voice channel occupied. Packets will be dropped.", "level": "dry"},
  {"text": "Hand signals accepted. Knocking is not.", "level": "full"},
  {"text": "My mute button cannot be trusted. Shh.", "level": "full"},
  {"text": "Negotiating. Your silence helps the price.", "level": "full"},
  {"text": "Probably talking to Shenzhen.", "level": "full"}
],
"focus": [
  {"text": "Heads down. Thinking expensive thoughts.", "level": "full"},
  {"text": "Compiling. Do not disturb the build.", "level": "dry"},
  {"text": "In the zone. The zone has a strict door policy.", "level": "dry"},
  {"text": "Interrupt only for fire or free food.", "level": "full"},
  {"text": "Turning coffee into decisions.", "level": "dry"},
  {"text": "Running at 100% CPU. Fan noise expected.", "level": "full"},
  {"text": "Each knock costs 23 minutes of focus. Choose wisely.", "level": "full"}
],
"busy": [
  {"text": "Busy. Like, actually busy.", "level": "dry"},
  {"text": "Request queued. Please hold.", "level": "dry"},
  {"text": "Saving the world, one gearbox at a time.", "level": "dry"},
  {"text": "Leave a message at the beep. There is no beep.", "level": "full"},
  {"text": "Urgent? Text me. On fire? Pull the alarm.", "level": "full"},
  {"text": "Occupied. Come back with more context.", "level": "full"}
],
"away": [
  {"text": "Away. The office plant is in charge.", "level": "full"},
  {"text": "Out of the office. Probably on a plane.", "level": "dry"},
  {"text": "Walking the warehouse. Back soon.", "level": "dry"},
  {"text": "Currently in another time zone, mentally or physically.", "level": "full"},
  {"text": "Out. Not out out. Just out.", "level": "full"}
],
"gone": [
  {"text": "Gone for the day. The robots are on night shift.", "level": "dry"},
  {"text": "Back tomorrow. This door will wait.", "level": "dry"},
  {"text": "The office plant is in charge.", "level": "full"},
  {"text": "Logged off. Batteries recharging.", "level": "dry"}
]
})JSON";
