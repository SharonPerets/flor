#include "weather_net.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

namespace weather {
namespace {

SemaphoreHandle_t lock = nullptr;
TaskHandle_t task = nullptr;
volatile int wantCity = DEFAULT_CITY;
volatile bool running = false;

Data result;
bool resultOk = false, haveNews = false;
String resultError;

// Day of the week for a date: 0 = Sunday (Sakamoto's method)
int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

bool fetch(int city, Data& out, String& error) {
  const City& c = CITIES[city];
  char url[420];
  snprintf(url, sizeof url,
           "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,is_day"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&timezone=Asia%%2FJerusalem&forecast_days=7",
           c.lat, c.lon);

  WiFiClientSecure client;
  client.setInsecure();  // public weather data; nothing secret is sent
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) {
    error = "couldn't start the request";
    return false;
  }
  const int status = http.GET();
  if (status != 200) {
    error = status < 0 ? String("no connection (") + http.errorToString(status) + ")" : String("server said ") + status;
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    error = "couldn't read the forecast";
    return false;
  }
  JsonObject cur = doc["current"];
  JsonObject daily = doc["daily"];
  if (cur.isNull() || daily.isNull()) {
    error = "the forecast was empty";
    return false;
  }

  out = Data();
  out.city = city;
  out.temp = cur["temperature_2m"] | 0.0f;
  out.feels = cur["apparent_temperature"] | out.temp;
  out.humidity = cur["relative_humidity_2m"] | 0;
  out.wind = cur["wind_speed_10m"] | 0.0f;
  out.code = cur["weather_code"] | 0;
  out.isDay = (cur["is_day"] | 1) != 0;
  const char* t = cur["time"] | "";  // "2026-10-22T21:30"
  if (strlen(t) >= 16) {
    memcpy(out.updated, t + 11, 5);
    out.updated[5] = 0;
  }

  static const char* NAMES[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  JsonArray days = daily["time"];
  const int n = days.size() < 7 ? (int)days.size() : 7;
  for (int i = 0; i < n; ++i) {
    Day& d = out.days[i];
    const char* date = days[i] | "";  // "2026-10-22"
    int y = 0, m = 1, dd = 1;
    sscanf(date, "%d-%d-%d", &y, &m, &dd);
    strncpy(d.name, i == 0 ? "Today" : NAMES[weekday(y, m, dd)], sizeof d.name - 1);
    d.code = daily["weather_code"][i] | 0;
    d.hi = (int)lroundf(daily["temperature_2m_max"][i] | 0.0f);
    d.lo = (int)lroundf(daily["temperature_2m_min"][i] | 0.0f);
    d.rain = daily["precipitation_probability_max"][i] | 0;
  }
  out.dayCount = n;
  out.valid = n > 0;
  return out.valid;
}

void worker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    running = true;
    const int city = wantCity;
    Data d;
    String error;
    const bool ok = fetch(city, d, error);
    Serial.printf("weather: %s %s\n", CITIES[city].name, ok ? "updated" : error.c_str());
    xSemaphoreTake(lock, portMAX_DELAY);
    if (ok) result = d;
    resultOk = ok;
    resultError = error;
    haveNews = true;
    xSemaphoreGive(lock);
    running = false;
  }
}

}  // namespace

void beginFetching() {
  if (task) return;
  lock = xSemaphoreCreateMutex();
  // on core 0, next to Wi-Fi; the face is drawn on core 1
  xTaskCreatePinnedToCore(worker, "weather", 16384, nullptr, 1, &task, 0);
}

void request(int city) {
  if (!task || city < 0 || city >= CITY_COUNT) return;
  wantCity = city;
  xTaskNotifyGive(task);
}

bool busy() { return running; }

bool takeResult(Data& out, bool& ok, String& error) {
  if (!lock) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  const bool news = haveNews;
  if (news) {
    if (resultOk) out = result;
    ok = resultOk;
    error = resultError;
    haveNews = false;
  }
  xSemaphoreGive(lock);
  return news;
}

}  // namespace weather
