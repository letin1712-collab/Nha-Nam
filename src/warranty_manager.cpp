#include "warranty_manager.h"
#include <Preferences.h>
#include <Arduino.h>
#include <time.h>

static bool   s_activated  = false;
static time_t s_startTime  = 0;

void warranty_init() {
    Preferences prefs;
    prefs.begin("warranty", true);
    s_startTime = (time_t)prefs.getLong64("start", 0);
    prefs.end();

    if (s_startTime > 0) {
        s_activated = true;
        Serial.printf("[WARRANTY] Activated. Start: %ld, Days left: %d\n",
                      (long)s_startTime, warranty_daysLeft());
    } else {
        Serial.println("[WARRANTY] Not yet activated.");
    }
}

void warranty_startIfNeeded() {
    if (s_activated) return;

    struct tm ti;
    if (!getLocalTime(&ti, 100)) {
        Serial.println("[WARRANTY] Cannot start: NTP not synced yet");
        return;
    }

    s_startTime = mktime(&ti);
    s_activated = true;

    Preferences prefs;
    prefs.begin("warranty", false);
    prefs.putLong64("start", (int64_t)s_startTime);
    prefs.end();

    Serial.printf("[WARRANTY] ACTIVATED at timestamp %ld\n", (long)s_startTime);
}

int warranty_daysLeft() {
    if (!s_activated || s_startTime == 0) return -1;

    struct tm ti;
    if (!getLocalTime(&ti, 100)) {
        // NTP chưa sync — tính theo millis tạm
        return WARRANTY_DAYS_TOTAL; // Fallback: trả về tối đa
    }

    time_t now = mktime(&ti);
    long elapsedSec = (long)(now - s_startTime);
    if (elapsedSec < 0) elapsedSec = 0;

    int elapsedDays = (int)(elapsedSec / 86400);
    int remaining   = WARRANTY_DAYS_TOTAL - elapsedDays;
    return (remaining > 0) ? remaining : 0;
}

void warranty_reset() {
    s_activated = false;
    s_startTime = 0;

    Preferences prefs;
    prefs.begin("warranty", false);
    prefs.remove("start");
    prefs.end();

    Serial.println("[WARRANTY] RESET — warranty cleared.");
}
