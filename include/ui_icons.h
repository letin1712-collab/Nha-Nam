#pragma once
#include <TFT_eSPI.h>
#include "ui_config.h"

// ============================================================
// All icons centred at (cx, cy), half-size 'r'
// Designed for r=8..10 on dark backgrounds
// ============================================================

// ---- CO2 icon — cloud only ----
inline void icon_co2(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    int cloudR = max(r / 3, 2);
    int midY = cy;

    tft.fillCircle(cx - cloudR - 1, midY + 1, cloudR, col);
    tft.fillCircle(cx + cloudR + 1, midY + 1, cloudR, col);
    tft.fillCircle(cx, midY - cloudR + 1, cloudR + 1, col);
    tft.fillRoundRect(cx - (r - 1), midY, (r - 1) * 2 + 1, cloudR + 3, 3, col);

    tft.fillCircle(cx - cloudR - 1, midY + 1, max(cloudR - 1, 1), COL_BG);
    tft.fillCircle(cx + cloudR + 1, midY + 1, max(cloudR - 1, 1), COL_BG);
    tft.fillCircle(cx, midY - cloudR + 1, max(cloudR, 1), COL_BG);
    tft.fillRoundRect(cx - (r - 2), midY + 1, (r - 2) * 2 + 1, cloudR + 1, 2, COL_BG);
}
// ---- Thermometer (outline + mercury) ----
inline void icon_temp(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    int bulbR   = max(r / 3, 3);
    int tubeW   = max(r / 4, 2);
    int tubeTop = cy - r;
    int tubeH   = r + bulbR + 1;
    int bulbY   = tubeTop + tubeH;

    tft.drawRoundRect(cx - tubeW, tubeTop, tubeW * 2 + 1, tubeH, tubeW, col);
    tft.drawCircle(cx, bulbY, bulbR, col);

    int inW = max(tubeW - 1, 1);
    tft.fillRect(cx - inW, tubeTop + 2, inW * 2 + 1, tubeH - 5, COL_BG);
    tft.fillCircle(cx, bulbY, max(bulbR - 1, 1), COL_BG);

    int mercuryH = max(tubeH / 2, 3);
    tft.fillRect(cx - inW + 1, tubeTop + tubeH - mercuryH - 1, max(inW * 2 - 1, 1), mercuryH, col);
    tft.fillCircle(cx, bulbY, max(bulbR - 2, 1), col);

    tft.drawFastHLine(cx + tubeW + 2, tubeTop + 2, 2, col);
    tft.drawFastHLine(cx + tubeW + 2, tubeTop + tubeH / 2, 2, col);
}

// ---- Water drop ----
inline void icon_humi(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    for (int dy = -r; dy <= r; dy++) {
        float n = (float)dy / r;
        float w = (dy < 0) ? (1.0f + n) * r * 0.55f
                           : sqrtf(1.0f - n*n) * r * 0.65f;
        if (w > 0)
            tft.drawFastHLine(cx - (int)w, cy + dy, (int)(w*2) + 1, col);
    }
}

// ---- Sun ----
inline void icon_lux(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    tft.fillCircle(cx, cy, r / 3, col);
    for (int i = 0; i < 8; i++) {
        float a = i * 0.7854f;
        int x1 = cx + (int)(cosf(a) * (r / 2.2f));
        int y1 = cy + (int)(sinf(a) * (r / 2.2f));
        int x2 = cx + (int)(cosf(a) * r);
        int y2 = cy + (int)(sinf(a) * r);
        tft.drawLine(x1, y1, x2, y2, col);
    }
}

// ---- WiFi (3 separate concentric arcs + dot) ----
inline void icon_wifi(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    int bx = cx, by = cy + r / 2 + 1;
    tft.fillCircle(bx, by, max(r / 7, 1), col);
    int radii[3] = { max(r / 2, 4), max((r * 3) / 4, 6), max(r, 8) };
    for (int i = 0; i < 3; i++) {
        int rd = radii[i];
        for (float a = 222.0f; a <= 318.0f; a += 3.0f) {
            float rad = a * 0.01745f;
            int px = bx + (int)roundf(cosf(rad) * rd);
            int py = by + (int)roundf(sinf(rad) * rd);
            tft.drawPixel(px, py, col);
        }
    }
}

// ---- WiFi off ----
inline void icon_wifi_off(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    icon_wifi(tft, cx, cy, r, COL_GRAY_DIM);
    tft.drawLine(cx - r/2, cy - r/2, cx + r/2, cy + r/2, COL_RED);
    tft.drawLine(cx + r/2, cy - r/2, cx - r/2, cy + r/2, COL_RED);
}

// ---- MQTT server stack ----
inline void icon_mqtt(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    int sh = max(r / 4, 2);
    for (int i = 0; i < 3; i++) {
        int ry = cy - r/2 + i * (sh + 3);
        tft.fillRoundRect(cx - r + 1, ry, r*2 - 2, sh, 2, col);
    }
}

// ---- Home ----
inline void icon_home(TFT_eSPI& tft, int cx, int cy, int r, uint16_t col) {
    tft.fillTriangle(cx - r, cy, cx + r, cy, cx, cy - r, col);
    tft.fillRect(cx - r*2/3, cy, r*4/3 + 1, r, col);
    tft.fillRect(cx - r/5, cy + r/2, r*2/5, r/2, COL_BG);
}



