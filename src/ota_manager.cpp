#include "ota_manager.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include <TFT_eSPI.h>
#include "ui_config.h"
#include "thingsboard_client.h"  // TB_HW_ID, FIRMWARE_VERSION

extern TFT_eSPI tft;

// ---- Progress UI ----
static void _drawProgress(int percent) {
    int barX = 20, barY = SCREEN_H / 2, barW = SCREEN_W - 40, barH = 20;

    if (percent == 0) {
        tft.fillScreen(COL_BG);
        tft.setTextSize(2);
        tft.setTextColor(COL_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("UPDATING FIRMWARE", SCREEN_W / 2, barY - 40);
        tft.drawString("Do not power off!", SCREEN_W / 2, barY - 15);
        tft.setTextDatum(TL_DATUM);
        tft.drawRoundRect(barX - 2, barY - 2, barW + 4, barH + 4, 6, COL_GRAY);
    }

    tft.fillRoundRect(barX, barY, (barW * percent) / 100, barH, 4, COL_GREEN);
    tft.setTextSize(1);
    tft.setTextColor(COL_WHITE, COL_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String(percent) + "%", SCREEN_W / 2, barY + barH + 20);
    tft.setTextDatum(TL_DATUM);
}

// ---- Core download ----
static void _doOta(const char* url) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected!");
        return;
    }

    Serial.printf("[OTA] Starting: %s\n", url);
    _drawProgress(0);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP %d\n", httpCode);
        tft.setTextColor(COL_RED);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("OTA Error " + String(httpCode), SCREEN_W/2, SCREEN_H/2 + 50);
        http.end();
        delay(3000);
        return;
    }

    int total = http.getSize();
    if (total <= 0) { http.end(); return; }

    if (!Update.begin(total)) {
        Update.printError(Serial);
        http.end();
        return;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0, lastPct = -1;

    while (http.connected() && total > 0) {
        size_t avail = stream->available();
        if (!avail) { delay(1); continue; }
        int c = stream->readBytes(buf, min(avail, sizeof(buf)));
        Update.write(buf, c);
        written += c;
        int pct = (written * 100LL) / (total > 0 ? total : 1);
        if (pct != lastPct && pct % 5 == 0) {
            _drawProgress(pct);
            Serial.printf("[OTA] %d%%\n", pct);
            lastPct = pct;
        }
    }

    if (Update.end() && Update.isFinished()) {
        _drawProgress(100);
        tft.setTextColor(COL_CYAN);
        tft.drawString("SUCCESS! Rebooting...", SCREEN_W/2, SCREEN_H/2 + 50);
        delay(2000);
        ESP.restart();
    } else {
        Serial.printf("[OTA] Error #%d\n", Update.getError());
        tft.setTextColor(COL_RED);
        tft.drawString("OTA Failed!", SCREEN_W/2, SCREEN_H/2 + 50);
        delay(3000);
    }
    http.end();
}

// ---- Public API ----

void ota_startUpdateFromUrl(const char* url) {
    _doOta(url);
}

void ota_checkAndUpdate(const char* baseUrl) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected!");
        return;
    }

    // Build URL: <base>?id=<hw_id>&v=<version>
    String url = String(baseUrl) + "?id=" + TB_HW_ID + "&v=" + FIRMWARE_VERSION;
    Serial.printf("[OTA] Check: %s\n", url.c_str());

    HTTPClient http;
    http.begin(url.c_str());
    http.setTimeout(10000);
    int httpCode = http.GET();

    if (httpCode == 304) {
        Serial.println("[OTA] Firmware is up-to-date (304)");
        tft.setTextColor(COL_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Already latest version", SCREEN_W/2, SCREEN_H/2);
        delay(1500);
    } else if (httpCode == 200) {
        String redirectUrl = http.getString();
        redirectUrl.trim();
        if (redirectUrl.startsWith("http")) {
            // Server trả về URL firmware
            Serial.printf("[OTA] Redirect URL: %s\n", redirectUrl.c_str());
            http.end();
            _doOta(redirectUrl.c_str());
            return;
        }
        // Nếu response là firmware binary (dù ít xảy ra với query param)
        Serial.println("[OTA] 200 but not a URL — downloading directly");
        http.end();
        _doOta(url.c_str());
        return;
    } else if (httpCode == 404) {
        Serial.println("[OTA] No firmware for this device (404)");
        tft.setTextColor(COL_RED);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("No firmware found", SCREEN_W/2, SCREEN_H/2);
        delay(1500);
    } else {
        Serial.printf("[OTA] Unexpected HTTP %d\n", httpCode);
    }

    http.end();
}
