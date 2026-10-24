// Wi-Fi setup and the connection to the Gorgi Helper app on the PC.
//
// Wi-Fi: on first start (or after "forget Wi-Fi") Gorgi opens its own
// network called "Gorgi-Setup". Join it with a phone and pick your Wi-Fi.
//
// Helper: Gorgi runs a WebSocket server on port 81 and announces itself on
// the network as "gorgi.local" (service _gorgi._tcp), so the helper can find
// it without an IP address. Messages are small JSON objects:
//
//   helper -> Gorgi  {"type":"hello","name":"MY-PC","key":"<pairing secret>"}
//                    {"type":"media","title":"..","artist":"..","album":"..","playing":true,"volume":40,
//                     "pos":63.5,"dur":215.0,"art":7}   (pos/dur in seconds, art = album art id)
//                    binary "GART" + id + total + offset + RGB565 data   (album art, in pieces)
//                    {"type":"clock","text":"21:45","epoch":1761245100}  (epoch: seconds since 1970)
//                    {"type":"time_add","kind":"timer","seconds":600,"label":"pasta"}
//                    {"type":"time_add","kind":"focus","seconds":1500}
//                    {"type":"time_add","kind":"remind","text":"call mom","at":1761245100}
//                    {"type":"time_add","kind":"remind","text":"vitamins","minute":480,"days":127}
//                                                 (repeating: minute of the day, days bit 0 = Sunday)
//                    {"type":"time_cancel","id":12}
//                    {"type":"claude","hooked":true,"sessions":[{"name":"flor","state":"waiting","text":"Needs your OK",
//                     "since":1761245100}],"usage":{"five":62.5,"five_reset":1761250000,"week":23,"week_reset":..,"at":..}}
//                    {"type":"claude_alert","name":"flor","kind":"waiting"|"done","text":"Claude needs you in flor"}
//                    {"type":"call","on":true}       (a Teams call started / ended)
//                    {"type":"update_info","available":true,"version":"4.2","busy":false}
//                    {"type":"update_status","text":"Updating the PC helper..."}
//                    {"type":"voice","state":"listening"|"thinking"|"idle","followup":false}
//                    {"type":"heard","text":"what you said"}
//                    {"type":"say","text":"Gorgi's answer","mood":"happy"}
//                    {"type":"level","value":0.4}       (voice loudness while speaking)
//   Gorgi -> helper  {"type":"hello","version":"3.6","paired":true}
//                    {"type":"cmd","action":"playpause"|"next"|"prev"}
//                    {"type":"cmd","action":"volume","value":40}
//                    {"type":"cmd","action":"teams_mute"}   (the mute button during a call)
//                    {"type":"cmd","action":"update"}       (Update in the pull-down panel)
//                    {"type":"cmd","action":"check_updates"} (the panel was opened)
//                    {"type":"talk"}     (face tapped: start a conversation)
//                    {"type":"cancel"}   (face tapped again: stop it)
//                    {"type":"weather","city":"Nahariya","now":{...},"days":[...]}
//                    {"type":"time_list","timers":[...],"reminders":[...],"focus_today":2}
//                    {"type":"due","kind":"timer"|"focus"|"reminder"|"missed","text":"Ding! ..."}
#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include <functional>

#include "ui.h"

// Shown at startup and sent to the helper, so it's easy to see which version is running
#define GORGI_VERSION "4.5"

namespace pclink {

constexpr const char* SETUP_NETWORK = "Gorgi-Setup";
constexpr uint16_t PORT = 81;
constexpr uint16_t UPDATE_PORT = 8080;  // firmware updates (HTTP POST /update)

enum class WifiState { Connecting, SetupPortal, Online };

struct Handlers {
  std::function<void(const ui::MediaInfo&)> onMedia;
  // The PC's clock: "21:45" and seconds since 1970 (0 if the helper is older)
  std::function<void(const String& text, uint32_t epoch)> onClock;
  std::function<void(bool connected, const String& pcName)> onHelper;
  std::function<void(WifiState)> onWifi;
  // Conversation messages: "voice", "heard", "say" and "level"
  std::function<void(const char* type, JsonObjectConst msg)> onVoice;
  // Timers and reminders set by voice: "time_add" and "time_cancel"
  std::function<void(const char* type, JsonObjectConst msg)> onTime;
  // Anything else: "claude" (sessions + usage), "claude_alert", "call" (Teams call on/off)
  std::function<void(const char* type, JsonObjectConst msg)> onOther;
  // A piece of album art (see music_screen.h)
  std::function<void(uint32_t id, uint32_t total, uint32_t offset, const uint8_t* data, size_t len)> onArt;
  // Firmware update over Wi-Fi: percent 0..100, or -1 if it failed
  std::function<void(int percent, const String& message)> onUpdate;
};

void begin(const Handlers& handlers);
void loop();

WifiState wifiState();
bool helperConnected();
String ipAddress();
int wifiRssi();  // Wi-Fi signal in dBm (0 if not connected)

// Send a command to the PC. Does nothing if the helper isn't connected.
void sendCommand(const char* action, int value = -1);

// Send a message with only a type, like "talk" or "cancel".
void sendSimple(const char* type);

// Send any message (it must have a "type"). Does nothing if the helper isn't connected.
void send(JsonDocument& doc);
// The same with a message already written as JSON text (for big ones, like handwriting)
void sendText(const String& json);

// Firmware updates: the helper sends the new firmware with
//   POST http://gorgi.local:8080/update?key=<pairing secret>&size=<bytes>&md5=<hex>
// (a normal file upload form). Gorgi installs it and restarts. Only the helper
// that paired first (its key) is accepted. Forgetting Wi-Fi also forgets it.

// Forget the saved Wi-Fi network (and the paired helper) and restart into setup mode.
void forgetWifi();

}  // namespace pclink
