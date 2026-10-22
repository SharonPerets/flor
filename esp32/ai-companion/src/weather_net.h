// Fetches the forecast from Open-Meteo (free, no key) in the background, so
// the face never stops while it downloads.
#pragma once

#include <Arduino.h>

#include "weather.h"

namespace weather {

void beginFetching();       // once at startup
void request(int city);     // fetch now (in the background)
bool busy();                // a fetch is running

// New result since the last call? ok = it worked; error says why not.
bool takeResult(Data& out, bool& ok, String& error);

}  // namespace weather
