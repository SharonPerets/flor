#include "pclink.h"

#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace pclink {
namespace {


WiFiManager wm;
WebSocketsServer server(PORT);
WebServer http(UPDATE_PORT);  // receives firmware updates
Handlers handlers;

// Pairing: the first helper that connects gives Gorgi a secret key. Only a
// helper with that key may send firmware updates.
Preferences prefs;
String pairedKey;

// Firmware update in progress
bool updateRejected = false;
String updateError;
size_t updateSize = 0, updateWritten = 0;
int updateLastPercent = -1;
uint32_t restartAtMs = 0;

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
    String key = doc["key"] | "";
    if (pairedKey.length() == 0 && key.length() >= 16) {
      pairedKey = key;  // first helper ever: pair with it
      prefs.putString("helperKey", pairedKey);
      Serial.println("pclink: paired with this helper");
    }
    const bool paired = key.length() >= 16 && key == pairedKey;
    Serial.printf("pclink: helper connected (%s)%s\n", name.c_str(), paired ? "" : " - not the paired helper");
    JsonDocument reply;
    reply["type"] = "hello";
    reply["version"] = GORGI_VERSION;
    reply["paired"] = paired;
    sendJson(client, reply);
    if (handlers.onHelper) handlers.onHelper(true, name);
    return;
  }
  if (client != helperClient) return;  // ignore anyone who didn't say hello

  if (strcmp(type, "media") == 0) {
    ui::MediaInfo m;
    m.title = (const char*)(doc["title"] | "");
    m.artist = (const char*)(doc["artist"] | "");
    m.album = (const char*)(doc["album"] | "");
    m.playing = doc["playing"] | false;
    m.volume = doc["volume"] | 50;
    m.hasPos = doc["pos"].is<float>() && doc["dur"].is<float>();
    m.pos = doc["pos"] | 0.0f;
    m.dur = doc["dur"] | 0.0f;
    m.posAtMs = millis();
    m.artId = doc["art"] | 0u;
    if (handlers.onMedia) handlers.onMedia(m);
  } else if (strcmp(type, "clock") == 0) {
    if (handlers.onClock) handlers.onClock(String((const char*)(doc["text"] | "")), doc["epoch"] | 0u);
  } else if (strcmp(type, "time_add") == 0 || strcmp(type, "time_cancel") == 0) {
    if (handlers.onTime) handlers.onTime(type, doc.as<JsonObjectConst>());
  } else if (handlers.onOther) {  // "claude", "claude_alert", "call"...
    handlers.onOther(type, doc.as<JsonObjectConst>());
  } else if (strcmp(type, "voice") == 0 || strcmp(type, "heard") == 0 || strcmp(type, "say") == 0 ||
             strcmp(type, "level") == 0) {
    if (handlers.onVoice) handlers.onVoice(type, doc.as<JsonObjectConst>());
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
    case WStype_BIN:
      // Album art in pieces: "GART", picture id, total bytes, offset (each 4 bytes, little-endian), data
      if ((int)client == helperClient && length > 16 && memcmp(payload, "GART", 4) == 0 && handlers.onArt) {
        uint32_t head[3];
        memcpy(head, payload + 4, sizeof head);
        handlers.onArt(head[0], head[1], head[2], payload + 16, length - 16);
      }
      break;
    default:
      break;
  }
}

void report(int percent, const String& message) {
  if (handlers.onUpdate) handlers.onUpdate(percent, message);
}

// Called again and again while the firmware file arrives, piece by piece
void onUpdateUpload() {
  HTTPUpload& up = http.upload();
  if (up.status == UPLOAD_FILE_START) {
    updateError = "";
    updateRejected = pairedKey.length() == 0 || http.arg("key") != pairedKey;
    updateLastPercent = -1;
    if (updateRejected) {
      Serial.println("pclink: update refused (not from the paired helper)");
      return;
    }
    updateSize = (size_t)http.arg("size").toInt();
    updateWritten = 0;
    Serial.printf("pclink: receiving firmware update (%u bytes)\n", (unsigned)updateSize);
    report(0, "Updating - back in a moment!");  // before anything is written to flash
    if (!Update.begin(updateSize > 0 ? updateSize : UPDATE_SIZE_UNKNOWN)) {
      updateError = Update.errorString();
      return;
    }
    if (http.hasArg("md5")) Update.setMD5(http.arg("md5").c_str());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (updateRejected || updateError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      updateError = Update.errorString();
      return;
    }
    updateWritten += up.currentSize;
    if (updateSize > 0) {
      const int pct = (int)(updateWritten * 100 / updateSize);
      if (pct != updateLastPercent && pct < 100) {
        updateLastPercent = pct;
        report(pct, String("Updating... ") + pct + "%");
      }
    }
  } else if (up.status == UPLOAD_FILE_END) {
    if (!updateRejected && updateError.length() == 0 && !Update.end(true)) updateError = Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    updateError = "the upload stopped";
  }
}

// Called once the whole update has arrived
void onUpdateDone() {
  if (updateRejected) {
    http.send(403, "text/plain", "Gorgi is paired with another helper");
    return;
  }
  if (updateError.length() || !Update.isFinished()) {
    Serial.printf("pclink: update failed: %s\n", updateError.c_str());
    http.send(500, "text/plain", String("update failed: ") + updateError);
    report(-1, String("Update failed: ") + updateError);
    return;
  }
  Serial.println("pclink: update installed, restarting");
  http.send(200, "text/plain", "ok");
  report(100, "Update done! Restarting...");
  restartAtMs = millis() + 1500;
}

void onInfo() {
  http.send(200, "application/json", String("{\"name\":\"Gorgi\",\"version\":\"") + GORGI_VERSION + "\"}");
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
    http.on("/info", HTTP_GET, onInfo);
    http.on("/update", HTTP_POST, onUpdateDone, onUpdateUpload);
    http.begin();
    serverStarted = true;
  }
}

}  // namespace

void begin(const Handlers& h) {
  handlers = h;
  prefs.begin("gorgi", false);
  pairedKey = prefs.getString("helperKey", "");
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

  if (serverStarted) {
    server.loop();
    http.handleClient();
  }
  if (restartAtMs && (int32_t)(millis() - restartAtMs) >= 0) ESP.restart();
}

WifiState wifiState() { return state; }
bool helperConnected() { return helperClient >= 0; }
String ipAddress() { return WiFi.localIP().toString(); }
int wifiRssi() { return WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0; }

void sendCommand(const char* action, int value) {
  if (helperClient < 0) return;
  JsonDocument doc;
  doc["type"] = "cmd";
  doc["action"] = action;
  if (value >= 0) doc["value"] = value;
  sendJson(helperClient, doc);
}

void sendSimple(const char* type) {
  if (helperClient < 0) return;
  JsonDocument doc;
  doc["type"] = type;
  sendJson(helperClient, doc);
}

void send(JsonDocument& doc) {
  if (helperClient < 0) return;
  sendJson(helperClient, doc);
}

void sendText(const String& json) {
  if (helperClient < 0) return;
  server.sendTXT((uint8_t)helperClient, json.c_str(), json.length());
}

void forgetWifi() {
  Serial.println("pclink: forgetting Wi-Fi and the paired helper, restarting");
  prefs.remove("helperKey");
  wm.resetSettings();
  delay(300);
  ESP.restart();
}

}  // namespace pclink
