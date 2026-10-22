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
//                    {"type":"media","title":"..","artist":"..","playing":true,"volume":40}
//                    {"type":"clock","text":"21:45"}
//                    {"type":"voice","state":"listening"|"thinking"|"idle","followup":false}
//                    {"type":"heard","text":"what you said"}
//                    {"type":"say","text":"Gorgi's answer","mood":"happy"}
//                    {"type":"level","value":0.4}       (voice loudness while speaking)
//   Gorgi -> helper  {"type":"hello","version":"3.4","paired":true}
//                    {"type":"cmd","action":"playpause"|"next"|"prev"}
//                    {"type":"cmd","action":"volume","value":40}
//                    {"type":"talk"}     (face tapped: start a conversation)
//                    {"type":"cancel"}   (face tapped again: stop it)
#pragma once

#include <Arduino.h>

#include <ArduinoJson.h>

#include <functional>

#include "ui.h"

// Shown at startup and sent to the helper, so it's easy to see which version is running
#define GORGI_VERSION "3.4"

namespace pclink {

constexpr const char* SETUP_NETWORK = "Gorgi-Setup";
constexpr uint16_t PORT = 81;
constexpr uint16_t UPDATE_PORT = 8080;  // firmware updates (HTTP POST /update)

enum class WifiState { Connecting, SetupPortal, Online };

struct Handlers {
  std::function<void(const ui::MediaInfo&)> onMedia;
  std::function<void(const String&)> onClock;
  std::function<void(bool connected, const String& pcName)> onHelper;
  std::function<void(WifiState)> onWifi;
  // Conversation messages: "voice", "heard", "say" and "level"
  std::function<void(const char* type, JsonObjectConst msg)> onVoice;
  // Firmware update over Wi-Fi: percent 0..100, or -1 if it failed
  std::function<void(int percent, const String& message)> onUpdate;
};

void begin(const Handlers& handlers);
void loop();

WifiState wifiState();
bool helperConnected();
String ipAddress();

// Send a command to the PC. Does nothing if the helper isn't connected.
void sendCommand(const char* action, int value = -1);

// Send a message with only a type, like "talk" or "cancel".
void sendSimple(const char* type);

// Firmware updates: the helper sends the new firmware with
//   POST http://gorgi.local:8080/update?key=<pairing secret>&size=<bytes>&md5=<hex>
// (a normal file upload form). Gorgi installs it and restarts. Only the helper
// that paired first (its key) is accepted. Forgetting Wi-Fi also forgets it.

// Forget the saved Wi-Fi network (and the paired helper) and restart into setup mode.
void forgetWifi();

}  // namespace pclink
