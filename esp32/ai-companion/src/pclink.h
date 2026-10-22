// Wi-Fi setup and the connection to the Gorgi Helper app on the PC.
//
// Wi-Fi: on first start (or after "forget Wi-Fi") Gorgi opens its own
// network called "Gorgi-Setup". Join it with a phone and pick your Wi-Fi.
//
// Helper: Gorgi runs a WebSocket server on port 81 and announces itself on
// the network as "gorgi.local" (service _gorgi._tcp), so the helper can find
// it without an IP address. Messages are small JSON objects:
//
//   helper -> Gorgi  {"type":"hello","name":"MY-PC"}
//                    {"type":"media","title":"..","artist":"..","playing":true,"volume":40}
//                    {"type":"clock","text":"21:45"}
//   Gorgi -> helper  {"type":"hello","version":".."}
//                    {"type":"cmd","action":"playpause"|"next"|"prev"}
//                    {"type":"cmd","action":"volume","value":40}
#pragma once

#include <Arduino.h>

#include <functional>

#include "ui.h"

namespace pclink {

constexpr const char* SETUP_NETWORK = "Gorgi-Setup";
constexpr uint16_t PORT = 81;

enum class WifiState { Connecting, SetupPortal, Online };

struct Handlers {
  std::function<void(const ui::MediaInfo&)> onMedia;
  std::function<void(const String&)> onClock;
  std::function<void(bool connected, const String& pcName)> onHelper;
  std::function<void(WifiState)> onWifi;
};

void begin(const Handlers& handlers);
void loop();

WifiState wifiState();
bool helperConnected();
String ipAddress();

// Send a command to the PC. Does nothing if the helper isn't connected.
void sendCommand(const char* action, int value = -1);

// Forget the saved Wi-Fi network and restart into setup mode.
void forgetWifi();

}  // namespace pclink
