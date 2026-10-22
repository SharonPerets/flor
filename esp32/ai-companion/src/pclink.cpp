#include "pclink.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace pclink {
namespace {

constexpr const char* FIRMWARE_VERSION = "step2";

WiFiManager wm;
WebSocketsServer server(PORT);
Handlers handlers;

WifiState state = WifiState::Connecting;
bool serverStarted = false;
int helperClient = -1;  // WebSocket client number of the helper, -1 = none

void setState(WifiState s) {
  if (s == state) return;
  state = s;
  if (handlers.onWifi) handlers.onWifi(s);
}

void sendJson(int client, JsonDocument& doc) {
  if (client < 0) return;
  String out;
  serializeJson(doc, out);
  server.sendTXT((uint8_t)client, out);
}

void dropHelper() {
  if (helperClient < 0) return;
  helperClient = -1;
  if (handlers.onHelper) handlers.onHelper(false, "");
}

void handleMessage(uint8_t client, const uint8_t* payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) {
    Serial.println("pclink: bad JSON from helper");
    return;
  }
  const char* type = doc["type"] | "";

  if (strcmp(type, "hello") == 0) {
    helperClient = client;
    String name = doc["name"] | "PC";
    Serial.printf("pclink: helper connected (%s)\n", name.c_str());
    JsonDocument reply;
    reply["type"] = "hello";
    reply["version"] = FIRMWARE_VERSION;
    sendJson(client, reply);
    if (handlers.onHelper) handlers.onHelper(true, name);
    return;
  }
  if (client != helperClient) return;  // ignore anyone who didn't say hello

  if (strcmp(type, "media") == 0) {
    ui::MediaInfo m;
    m.title = (const char*)(doc["title"] | "");
    m.artist = (const char*)(doc["artist"] | "");
    m.playing = doc["playing"] | false;
    m.volume = doc["volume"] | 50;
    if (handlers.onMedia) handlers.onMedia(m);
  } else if (strcmp(type, "clock") == 0) {
    if (handlers.onClock) handlers.onClock(String((const char*)(doc["text"] | "")));
  }
}

void onSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("pclink: client %u connected from %s\n", client, server.remoteIP(client).toString().c_str());
      break;
    case WStype_DISCONNECTED:
      Serial.printf("pclink: client %u disconnected\n", client);
      if ((int)client == helperClient) dropHelper();
      break;
    case WStype_TEXT:
      handleMessage(client, payload, length);
      break;
    default:
      break;
  }
}

void goOnline() {
  setState(WifiState::Online);
  Serial.printf("pclink: Wi-Fi connected, IP %s\n", WiFi.localIP().toString().c_str());
  if (!serverStarted) {
    if (MDNS.begin("gorgi")) {
      MDNS.addService("gorgi", "tcp", PORT);
    } else {
      Serial.println("pclink: mDNS failed to start");
    }
    server.begin();
    server.onEvent(onSocketEvent);
    // Notice a PC that went to sleep without saying goodbye
    server.enableHeartbeat(15000, 4000, 2);
    serverStarted = true;
  }
}

}  // namespace

void begin(const Handlers& h) {
  handlers = h;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // keeps replies to the helper snappy
  wm.setTitle("Gorgi");
  wm.setHostname("gorgi");
  wm.setConfigPortalBlocking(false);
  wm.setConnectTimeout(15);
  wm.setShowInfoUpdate(false);

  setState(WifiState::Connecting);
  if (wm.autoConnect(SETUP_NETWORK)) {
    goOnline();
  } else {
    Serial.printf("pclink: no saved Wi-Fi, setup network \"%s\" is open\n", SETUP_NETWORK);
    setState(WifiState::SetupPortal);
  }
}

void loop() {
  if (state == WifiState::SetupPortal) {
    if (wm.process()) goOnline();  // true once the user saved a working network
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (state != WifiState::Online) goOnline();
  } else if (state == WifiState::Online) {
    Serial.println("pclink: Wi-Fi lost, reconnecting...");
    dropHelper();
    setState(WifiState::Connecting);  // the ESP32 reconnects on its own
  }

  if (serverStarted) server.loop();
}

WifiState wifiState() { return state; }
bool helperConnected() { return helperClient >= 0; }
String ipAddress() { return WiFi.localIP().toString(); }

void sendCommand(const char* action, int value) {
  if (helperClient < 0) return;
  JsonDocument doc;
  doc["type"] = "cmd";
  doc["action"] = action;
  if (value >= 0) doc["value"] = value;
  sendJson(helperClient, doc);
}

void forgetWifi() {
  Serial.println("pclink: forgetting Wi-Fi and restarting");
  wm.resetSettings();
  delay(300);
  ESP.restart();
}

}  // namespace pclink
