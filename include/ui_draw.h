#pragma once
#include <TFT_eSPI.h>
#include "thingsboard_client.h"
#include "warranty_manager.h"
#include "ui_config.h"
#include "ui_icons.h"

struct SensorData {
    float co2     = 0;
    float temp    = 0;
    float humi    = 0;
    float lux     = 0;
    bool  scd30Ok  = false;
    bool  bh1750Ok = false;
};

struct NetData {
    bool   wifiOk  = false;
    bool   mqttOk  = false;
    String ssid    = "";
    String ip      = "";
    String mqttHost  = "";
    String mqttUser  = "";
    String mqttPass  = "";
    String mqttTopic = "";
    String deviceId  = "";
    int    mqttPort  = 1883;
    bool   userMqttEnabled = false;
};

#define BTN_SETUP_X   (SCREEN_W/2 - 80)
#define BTN_SETUP_Y   (CONTENT_Y + CONTENT_H - 32)
#define BTN_SETUP_W   160
#define BTN_SETUP_H   28

inline bool ui_tapSetupBtn(int tx, int ty) {
    return tx >= BTN_SETUP_X && tx <= BTN_SETUP_X + BTN_SETUP_W
        && ty >= BTN_SETUP_Y && ty <= BTN_SETUP_Y + BTN_SETUP_H;
}

static void _fillPanel(TFT_eSPI& tft, int x, int y, int w, int h) {
    tft.fillRoundRect(x, y, w, h, 8, COL_CARD);
    tft.drawRoundRect(x, y, w, h, 8, COL_GRAY_DIM);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 7, tft.color565(28, 44, 62));
}

static void _drawSetupBtn(TFT_eSPI& tft, const char* label) {
    const uint16_t btnFill = tft.color565(18, 34, 58);
    const uint16_t btnEdge = tft.color565(36, 64, 96);
    tft.fillRoundRect(BTN_SETUP_X, BTN_SETUP_Y, BTN_SETUP_W, BTN_SETUP_H, 6, btnFill);
    tft.drawRoundRect(BTN_SETUP_X, BTN_SETUP_Y, BTN_SETUP_W, BTN_SETUP_H, 6, btnEdge);
    tft.drawRoundRect(BTN_SETUP_X + 1, BTN_SETUP_Y + 1, BTN_SETUP_W - 2, BTN_SETUP_H - 2, 5, COL_CYAN_DIM);
    tft.setTextSize(1);
    tft.setTextColor(COL_WHITE);
    int lw = strlen(label) * 6;
    tft.setCursor(BTN_SETUP_X + (BTN_SETUP_W - lw) / 2, BTN_SETUP_Y + 11);
    tft.print(label);
}

inline void ui_drawTopBar(TFT_eSPI& tft, int page, const NetData& net, const char* timeStr) {
    (void)page;
    tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, COL_CARD);
    tft.drawFastHLine(0, TOPBAR_H - 1, SCREEN_W, COL_GRAY_DIM);

    // Thời gian — căn giữa thật sự
    if (strlen(timeStr) > 0) {
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(COL_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(timeStr, SCREEN_W / 2, TOPBAR_H / 2);
        tft.setFreeFont(nullptr);
        tft.setTextDatum(TL_DATUM);
    }

    // Bên phải: MQTT text + WiFi icon + TB text
    // Layout từ phải: [TB] [WiFi] [MQTT]
    // TB = trạng thái ThingsBoard
    uint16_t tbCol = net.mqttOk ? COL_CYAN : COL_GRAY_DIM;
    tft.setTextSize(1);
    tft.setTextColor(tbCol);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("TB", SCREEN_W - 4, TOPBAR_H / 2);

    uint16_t wCol = net.wifiOk ? COL_GREEN : COL_RED;
    icon_wifi(tft, SCREEN_W - 28, TOPBAR_H / 2, 7, wCol);

    // Chữ MQTT — xanh nếu user MQTT bật, xám nếu tắt
    uint16_t mCol = net.userMqttEnabled ? COL_GREEN : COL_GRAY_DIM;
    tft.setTextColor(mCol);
    tft.setTextDatum(MR_DATUM);
    tft.drawString("MQTT", SCREEN_W - 42, TOPBAR_H / 2);
    tft.setTextDatum(TL_DATUM);
}

inline void ui_updateTopBarTime(TFT_eSPI& tft, const char* timeStr) {
    // Xóa vùng thời gian cũ và vẽ mới — chỉ dùng 1 lần fillRect nhỏ
    tft.fillRect(SCREEN_W / 2 - 30, 0, 60, TOPBAR_H, COL_CARD);

    if (strlen(timeStr) > 0) {
        tft.setFreeFont(&FreeSansBold9pt7b);
        tft.setTextColor(COL_CYAN);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(timeStr, SCREEN_W / 2, TOPBAR_H / 2);
        tft.setFreeFont(nullptr);
        tft.setTextDatum(TL_DATUM);
    }
}

inline void ui_drawNavBar(TFT_eSPI& tft, int activePage) {
    tft.fillRect(0, NAV_Y, SCREEN_W, NAVBAR_H, COL_CARD);
    tft.drawFastHLine(0, NAV_Y, SCREEN_W, COL_GRAY_DIM);
    tft.drawFastVLine(NAV_ITEM_W, NAV_Y + 5, NAVBAR_H - 10, tft.color565(40, 52, 70));
    tft.drawFastVLine(NAV_ITEM_W * 2, NAV_Y + 5, NAVBAR_H - 10, tft.color565(40, 52, 70));

    struct { const char* label; int page; } tabs[] = {
        { "SENS", PAGE_DASHBOARD },
        { "WIFI", PAGE_WIFI      },
        { "MQTT", PAGE_MQTT      },
    };

    for (int i = 0; i < 3; i++) {
        int x0 = NAV_ITEM_W * i;
        int cx = x0 + NAV_ITEM_W / 2;
        int cy = NAV_Y + 16;
        bool active = (activePage == tabs[i].page);
        uint16_t col = active ? COL_CYAN : COL_GRAY;

        if (active) {
            tft.fillRoundRect(x0 + 8, NAV_Y + 4, NAV_ITEM_W - 16, NAVBAR_H - 8, 6, tft.color565(18, 34, 58));
            tft.drawFastHLine(x0 + 14, NAV_Y + 3, NAV_ITEM_W - 28, COL_CYAN);
        }

        switch (tabs[i].page) {
            case PAGE_DASHBOARD: icon_home(tft, cx, cy, 8, col); break;
            case PAGE_WIFI:      icon_wifi(tft, cx, cy, 8, col); break;
            case PAGE_MQTT:      icon_mqtt(tft, cx, cy, 8, col); break;
        }

        int lw = strlen(tabs[i].label) * 6;
        tft.setTextSize(1);
        tft.setTextColor(col);
        tft.setCursor(cx - lw / 2, NAV_Y + NAVBAR_H - 14);
        tft.print(tabs[i].label);
    }
}

struct CardDef {
    const char* title;
    const char* unit;
    float       value;
    bool        valid;
    uint8_t     iconType;
    uint16_t    accentCol;
    float       warnHigh;
    float       warnLow;
};

static void _drawCardShell(TFT_eSPI& tft, int x, int y, int w, int h, uint16_t accentCol, bool valid) {
    uint16_t acc = valid ? accentCol : COL_GRAY_DIM;
    _fillPanel(tft, x, y, w, h);
    tft.fillRect(x + 8, y + 8, w - 16, 2, acc);
}

static void _drawCardValueOnly(TFT_eSPI& tft, int x, int y, int w, int h, const CardDef& c) {
    // Vùng value: x=waste 40px giữa card. Vẽ bg trước rồi text lên — 1 lần fillRect.
    int vx = x + 6, vy = y + 32, vw = w - 12, vh = h - 38;
    tft.fillRect(vx, vy, vw, vh, COL_CARD);

    uint16_t acc = c.valid ? c.accentCol : COL_GRAY_DIM;

    if (!c.valid) {
        tft.setFreeFont(&FreeSansBold18pt7b);
        tft.setTextColor(COL_GRAY);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("---", x + w / 2, y + h / 2 - 6);
    } else {
        uint16_t valCol = COL_WHITE;
        if (c.warnHigh > 0 && c.value >= c.warnHigh)        valCol = COL_RED;
        else if (c.warnHigh > 0 && c.value >= c.warnHigh * 0.8f) valCol = COL_ORANGE;

        char buf[12];
        if (c.value < 10)        snprintf(buf, sizeof(buf), "%.1f", c.value);
        else if (c.value < 1000) snprintf(buf, sizeof(buf), "%.1f", c.value);
        else                     snprintf(buf, sizeof(buf), "%d", (int)c.value);

        tft.setFreeFont(strlen(buf) >= 4 ? &FreeSansBold12pt7b : &FreeSansBold18pt7b);
        tft.setTextColor(valCol);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(buf, x + w / 2, y + h / 2 - 8);
    }

    // Đơn vị
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(acc);
    tft.setTextDatum(MC_DATUM);

    const char* ustr = c.unit;
    bool hasDeg = ((uint8_t)ustr[0] == 0xB0);
    if (hasDeg) {
        String unitStr = String("\xB0") + String(ustr + 1);
        tft.drawString(unitStr.c_str(), x + w / 2, y + h - 14);
    } else {
        tft.drawString(ustr, x + w / 2, y + h - 14);
    }

    tft.setFreeFont(nullptr);
}

// Lưu giá trị cũ để clear bằng cách vẽ đè màu nền (không fillRect → không chớp)
static char _oldVals[4][8] = {{0}, {0}, {0}, {0}};

inline void ui_updateDashboardValues(TFT_eSPI& tft, const SensorData& s) {
    CardDef cards[4] = {
        { "TEMP",  "\xB0""C",   s.temp, s.scd30Ok,  1, COL_ORANGE, 35,    0  },
        { "HUMID", "%",         s.humi, s.scd30Ok,  2, COL_CYAN,   85,    30 },
        { "CO2",   "ppm",       s.co2,  s.scd30Ok,  0, COL_GREEN,  2000,  0  },
        { "LUX",   "lx",        s.lux,  s.bh1750Ok, 3, COL_YELLOW, 50000, 0  },
    };

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            int idx = row * 2 + col;
            int cx = CARD_MARGIN + col * (CARD_W + CARD_MARGIN);
            int cy = CONTENT_Y + CARD_MARGIN + row * (CARD_H + CARD_MARGIN);
            int cw = CARD_W;
            int ch = CARD_H;
            CardDef& c = cards[idx];

            char buf[12];
            if (c.valid) {
                if (c.value < 10)        snprintf(buf, sizeof(buf), "%.1f", c.value);
                else if (c.value < 1000) snprintf(buf, sizeof(buf), "%.1f", c.value);
                else                     snprintf(buf, sizeof(buf), "%d", (int)c.value);
            } else {
                strcpy(buf, "---");
            }

            // Skip nếu giá trị không đổi
            if (strcmp(buf, _oldVals[idx]) == 0) continue;

            // Lưu giá trị cũ để tính width xóa
            char oldBuf[8];
            strcpy(oldBuf, _oldVals[idx]);
            int oldLen = strlen(oldBuf);
            strcpy(_oldVals[idx], buf);

            uint16_t valCol = COL_WHITE;
            if (!c.valid) {
                valCol = COL_GRAY;
            } else if (c.warnHigh > 0 && c.value >= c.warnHigh) {
                valCol = COL_RED;
            } else if (c.warnHigh > 0 && c.value >= c.warnHigh * 0.8f) {
                valCol = COL_ORANGE;
            }

            // Tính width thực tế bằng textWidth
            const GFXfont* newFont = (strlen(buf) >= 4) ? &FreeSansBold12pt7b : &FreeSansBold18pt7b;
            const GFXfont* oldFont = (oldLen >= 4) ? &FreeSansBold12pt7b : &FreeSansBold18pt7b;
            tft.setFreeFont(oldFont);
            int oldW = tft.textWidth(oldBuf);
            tft.setFreeFont(newFont);
            int newW = tft.textWidth(buf);
            int eraseW = oldW > newW ? oldW : newW;
            tft.fillRect(cx + cw/2 - eraseW/2 - 4, cy + ch/2 - 22, eraseW + 8, 30, COL_CARD);

            tft.setFreeFont(newFont);
            tft.setTextColor(valCol);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(buf, cx + cw / 2, cy + ch / 2 - 8);
            tft.setFreeFont(nullptr);
        }
    }
}

static void _drawCard(TFT_eSPI& tft, int x, int y, int w, int h, const CardDef& c) {
    uint16_t acc = c.valid ? c.accentCol : COL_GRAY_DIM;

    _drawCardShell(tft, x, y, w, h, c.accentCol, c.valid);

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(c.valid ? COL_WHITE : COL_GRAY);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(c.title, x + 8, y + 15);
    tft.setFreeFont(nullptr);

    int ix = x + w - 16, iy = y + 18;
    switch (c.iconType) {
        case 0: icon_co2 (tft, ix, iy, 8, acc); break;
        case 1: icon_temp(tft, ix, iy, 8, acc); break;
        case 2: icon_humi(tft, ix, iy, 8, acc); break;
        case 3: icon_lux (tft, ix, iy, 8, acc); break;
    }

    _drawCardValueOnly(tft, x, y, w, h, c);
}

inline void ui_drawDashboard(TFT_eSPI& tft, const SensorData& s) {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);

    CardDef cards[4] = {
        { "TEMP",  "\xB0""C",   s.temp, s.scd30Ok,  1, COL_ORANGE, 35,    0  },
        { "HUMID", "%",         s.humi, s.scd30Ok,  2, COL_CYAN,   85,    30 },
        { "CO2",   "ppm",       s.co2,  s.scd30Ok,  0, COL_GREEN,  2000,  0  },
        { "LUX",   "lx",        s.lux,  s.bh1750Ok, 3, COL_YELLOW, 50000, 0  },
    };

    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            int idx = row * 2 + col;
            int cx = CARD_MARGIN + col * (CARD_W + CARD_MARGIN);
            int cy = CONTENT_Y + CARD_MARGIN + row * (CARD_H + CARD_MARGIN);
            _drawCard(tft, cx, cy, CARD_W, CARD_H, cards[idx]);
        }
    }
}

inline void ui_drawWifiPage(TFT_eSPI& tft, const NetData& net) {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);

    _fillPanel(tft, 6, CONTENT_Y + 6, SCREEN_W - 12, 44);

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_CYAN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("WIFI", 14, CONTENT_Y + 14);

    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(net.wifiOk ? COL_GREEN : COL_RED);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(net.wifiOk ? "ONLINE" : "OFFLINE", SCREEN_W - 14, CONTENT_Y + 14);

    tft.setFreeFont(nullptr);

    icon_wifi(tft, SCREEN_W / 2, CONTENT_Y + 78, 18, net.wifiOk ? COL_CYAN : COL_GRAY_DIM);

    struct Row { const char* label; String value; uint16_t col; };
    Row rows[] = {
        { "SSID",   net.ssid,                         COL_WHITE },
        { "IP",     net.ip,                           COL_CYAN  },
        { "STATUS", net.wifiOk ? "Connected" : "---", (uint16_t)(net.wifiOk ? COL_GREEN : COL_RED) },
    };

    int ry = CONTENT_Y + 106;
    for (auto& r : rows) {
        _fillPanel(tft, 10, ry, SCREEN_W - 20, 24);
        tft.setTextSize(1);
        tft.setTextColor(COL_GRAY);
        tft.setCursor(18, ry + 8);
        tft.print(r.label);

        tft.setTextColor(r.col);
        int vw = r.value.length() * 6;
        tft.setCursor(SCREEN_W - 18 - vw, ry + 8);
        tft.print(r.value);
        ry += 28;
    }

    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY);
    tft.setCursor(SCREEN_W / 2 - 48, ry + 6);
    tft.print("Tap to setup WiFi");

    _drawSetupBtn(tft, ">> EDIT WIFI");
}

inline void ui_drawMqttPage(TFT_eSPI& tft, const NetData& net) {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);

    _fillPanel(tft, 6, CONTENT_Y + 6, SCREEN_W - 12, 36);

    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_CYAN);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("MQTT USER", 34, CONTENT_Y + 14);

    // Dịch icon MQTT lên chung với text thay vì cạnh chữ ON/OFF
    icon_mqtt(tft, 20, CONTENT_Y + 18, 8, net.userMqttEnabled ? COL_CYAN : COL_GRAY_DIM);

    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(net.userMqttEnabled ? COL_GREEN : COL_GRAY);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(net.userMqttEnabled ? "ON" : "OFF", SCREEN_W - 14, CONTENT_Y + 14);

    tft.setFreeFont(nullptr);

    // Nút bật/tắt gửi thêm MQTT user
    int swX = 120, swY = CONTENT_Y + 54;
    uint16_t swBg = net.userMqttEnabled ? COL_GREEN : COL_RED;
    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY);
    tft.setCursor(12, swY + 12);
    tft.print("SEND USER");
    tft.fillRoundRect(swX, swY, 106, 34, 8, swBg);
    tft.drawRoundRect(swX, swY, 106, 34, 8, COL_WHITE);
    tft.setTextColor(COL_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(net.userMqttEnabled ? "ON" : "OFF", swX + 53, swY + 18);
    tft.setTextDatum(TL_DATUM);

    struct Row { const char* label; String value; } rows[] = {
        { "HOST",  net.mqttHost },
        { "PORT",  String(net.mqttPort) },
        { "USER",  net.mqttUser },
        { "PASS",  net.mqttPass.length() ? "***" : "" },
        { "TOPIC", net.mqttTopic },
    };

    int ry = CONTENT_Y + 98;
    for (auto& r : rows) {
        _fillPanel(tft, 8, ry, SCREEN_W - 16, 25);
        tft.setTextSize(1);
        tft.setTextColor(COL_GRAY);
        tft.setCursor(16, ry + 8);
        tft.print(r.label);
        String val = r.value.length() > 23 ? r.value.substring(0, 21) + ".." : r.value;
        int vw = val.length() * 6;
        tft.setTextColor(COL_WHITE);
        tft.setCursor(SCREEN_W - 16 - vw, ry + 8);
        tft.print(val);
        ry += 28;
    }
}

inline void ui_drawInfoPage(TFT_eSPI& tft) {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, CONTENT_H, COL_BG);

    // Header
    _fillPanel(tft, 6, CONTENT_Y + 6, SCREEN_W - 12, 36);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_CYAN);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("DEVICE INFO", SCREEN_W / 2, CONTENT_Y + 14);
    tft.setFreeFont(nullptr);

    // Logo nhỏ
    tft.fillCircle(SCREEN_W / 2, CONTENT_Y + 70, 22, COL_CARD);
    tft.drawCircle(SCREEN_W / 2, CONTENT_Y + 70, 22, COL_CYAN_DIM);
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextColor(COL_CYAN);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("FV", SCREEN_W / 2, CONTENT_Y + 70);
    tft.setFreeFont(nullptr);

    // Thông tin
    struct Row { const char* label; String value; };
    int days = warranty_daysLeft();
    String w_val;
    if (days < 0) w_val = "Not Active";
    else if (days == 0) w_val = "Expired";
    else w_val = String(days) + " Days";

    Row rows[] = {
        { "Device Name", "FUVIAIR"    },
        { "Version",     "V1.0"       },
        { "Warranty",    w_val        },
        { "Website",     "Fuvitech.vn"},
    };

    int ry = CONTENT_Y + 102;
    for (auto& r : rows) {
        _fillPanel(tft, 8, ry, SCREEN_W - 16, 22);
        tft.setTextSize(1);
        tft.setTextColor(COL_GRAY);
        tft.setCursor(16, ry + 7);
        tft.print(r.label);
        int vw = r.value.length() * 6;
        tft.setTextColor(COL_WHITE);
        tft.setCursor(SCREEN_W - 16 - vw, ry + 7);
        tft.print(r.value);
        ry += 26;
    }

    // Nút BACK và UPDATE
    _drawSetupBtn(tft, "  BACK     UPDATE FW  ");
}

inline void ui_drawPage(TFT_eSPI& tft, int page,
                        const SensorData& sensors, const NetData& net,
                        const char* timeStr) {
    ui_drawTopBar(tft, page, net, timeStr);
    if (page != PAGE_INFO) ui_drawNavBar(tft, page);
    switch (page) {
        case PAGE_DASHBOARD: ui_drawDashboard(tft, sensors); break;
        case PAGE_WIFI:      ui_drawWifiPage(tft, net);      break;
        case PAGE_MQTT:      ui_drawMqttPage(tft, net);      break;
        case PAGE_INFO:      ui_drawInfoPage(tft);            break;
    }
}