#include "wallclock.h"

#include <Arduino.h>
#include <esp_sntp.h>
#include <sys/time.h>

namespace wallclock {
namespace {

// Israel: UTC+2, summer time (UTC+3) from the Friday before the last Sunday
// of March at 02:00 until the last Sunday of October at 02:00.
constexpr const char* TZ_ISRAEL = "IST-2IDT,M3.4.4/26,M10.5.0";
constexpr time_t SANE = 1700000000;  // Nov 2023: anything before this is "not set yet"

volatile bool fromInternet = false;

void onSynced(struct timeval*) {
  if (!fromInternet) Serial.println("wallclock: got the time from the internet");
  fromInternet = true;
}

}  // namespace

void begin() {
  setenv("TZ", TZ_ISRAEL, 1);
  tzset();
  sntp_set_time_sync_notification_cb(onSynced);
  configTzTime(TZ_ISRAEL, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

bool valid() { return time(nullptr) > SANE; }

time_t now() { return time(nullptr); }

void setFromPC(time_t epoch) {
  if (fromInternet || epoch < SANE) return;
  const time_t mine = time(nullptr);
  if (mine > SANE && labs((long)(mine - epoch)) < 30) return;  // close enough
  timeval tv{epoch, 0};
  settimeofday(&tv, nullptr);
  Serial.println("wallclock: using the PC's time until the internet answers");
}

void hhmm(char* out, int size) {
  if (!valid()) {
    if (size) out[0] = 0;
    return;
  }
  const time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(out, size, "%H:%M", &lt);
}

}  // namespace wallclock
