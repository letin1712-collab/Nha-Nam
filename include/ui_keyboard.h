#pragma once
#include <TFT_eSPI.h>
#include "ui_config.h"

// ============================================================
// Keyboard — portrait 240×320
// KBD_Y=54, NAV_Y=272 → available=218px → 4 rows × 54px
// KBD_KEY_W=24 (240/10=24), KBD_KEY_H=50
// ============================================================
#define KBD_MAX_LEN  40
#define KBD_INPUT_Y  (CONTENT_Y + 4)
#define KBD_INPUT_H  24
#define KBD_Y        (CONTENT_Y + 32)

#define KBD_KEY_W    24    // 10 × 24 = 240px exact
#define KBD_KEY_H    50    // Portrait bonus: tall keys = easy to tap
#define KBD_ROW_H    54    // 50px key + 4px gap

struct KeyboardState {
    char    text[KBD_MAX_LEN + 1] = {0};
    int     mode      = 0;     // 0=abc 1=ABC 2=sym
    bool    done      = false;
    bool    cancelled = false; // ESC: giữ nguyên giá trị cũ
    bool    isPass    = false;
    uint8_t maxLen    = KBD_MAX_LEN;
    const char* label = "";
};

static const char* _r[3][3] = {
    { "qwertyuiop", "QWERTYUIOP", "1234567890" },
    { "asdfghjkl",  "ASDFGHJKL",  "-+=@#$%^&*" },
    { "zxcvbnm",    "ZXCVBNM",    ".,;:!?/_"   }  // 8 chars: sideW=(240-192)/2-2=22px
};

static void _key(TFT_eSPI& tft, int x, int y, int w, int h,
                 const char* lbl, uint16_t bg, uint16_t br, uint16_t fg) {
    if (w <= 0 || h <= 0) return;
    tft.fillRoundRect(x, y, w, h, 5, bg);
    tft.drawRoundRect(x, y, w, h, 5, br);
    tft.setTextSize(1); tft.setTextColor(fg);
    int lw = strlen(lbl) * 6;
    tft.setCursor(x + w/2 - lw/2, y + h/2 - 3);
    tft.print(lbl);
}

inline void ui_drawKeyboard(TFT_eSPI& tft, const KeyboardState& k) {
    tft.fillRect(0, CONTENT_Y, SCREEN_W, NAV_Y - CONTENT_Y, COL_BG);

    // ── Input field ──────────────────────────────────────
    int lblW = strlen(k.label) * 6 + 8;
    tft.setTextSize(1); tft.setTextColor(COL_CYAN_DIM);
    tft.setCursor(4, KBD_INPUT_Y + 8); tft.print(k.label); tft.print(":");
    tft.fillRoundRect(lblW, KBD_INPUT_Y, SCREEN_W - lblW - 2, KBD_INPUT_H, 3,
                      tft.color565(10, 22, 40));
    tft.drawRoundRect(lblW, KBD_INPUT_Y, SCREEN_W - lblW - 2, KBD_INPUT_H, 3, COL_CYAN);

    int maxCh = (SCREEN_W - lblW - 14) / 6;
    int len   = strlen(k.text);
    tft.setTextColor(COL_WHITE);
    tft.setCursor(lblW + 5, KBD_INPUT_Y + 8);
    { const char* s = k.text; if (len > maxCh) s += (len - maxCh); tft.print(s); }
    tft.drawFastVLine(lblW + 5 + min(len, maxCh)*6, KBD_INPUT_Y + 5, 14, COL_CYAN);

    tft.drawFastHLine(0, KBD_Y - 2, SCREEN_W, tft.color565(20, 40, 70));

    // ── Rows 0-2 ─────────────────────────────────────────
    for (int r = 0; r < 3; r++) {
        const char* row = _r[r][k.mode];
        int n  = strlen(row);
        int ky = KBD_Y + r * KBD_ROW_H;

        if (r == 0) {
            for (int i = 0; i < n; i++) {
                char s[2] = {row[i], 0};
                _key(tft, i*KBD_KEY_W, ky+1, KBD_KEY_W-1, KBD_KEY_H-1, s,
                     tft.color565(14,28,52), tft.color565(30,55,95), COL_WHITE);
            }
        } else if (r == 1) {
            int st = (SCREEN_W - n * KBD_KEY_W) / 2;
            for (int i = 0; i < n; i++) {
                char s[2] = {row[i], 0};
                _key(tft, st + i*KBD_KEY_W, ky+1, KBD_KEY_W-1, KBD_KEY_H-1, s,
                     tft.color565(14,28,52), tft.color565(30,55,95), COL_WHITE);
            }
        } else { // r==2, 8 chars
            int n2 = n;
            int sideW = (SCREEN_W - n2 * KBD_KEY_W) / 2 - 2;
            int st2   = sideW + 2;
            // SHIFT
            const char* shLbl = (k.mode==1) ? "^" : (k.mode==0) ? "^" : "abc";
            uint16_t shBg = (k.mode==1) ? COL_CYAN : tft.color565(20,38,65);
            _key(tft, 1, ky+1, sideW, KBD_KEY_H-1, shLbl, shBg,
                 COL_CYAN_DIM, (k.mode==1) ? COL_BG : COL_CYAN);
            for (int i = 0; i < n2; i++) {
                char s[2] = {row[i], 0};
                _key(tft, st2 + i*KBD_KEY_W, ky+1, KBD_KEY_W-1, KBD_KEY_H-1, s,
                     tft.color565(14,28,52), tft.color565(30,55,95), COL_WHITE);
            }
            // DEL
            _key(tft, st2 + n2*KBD_KEY_W + 1, ky+1, sideW, KBD_KEY_H-1, "DEL",
                 tft.color565(55,10,10), COL_RED, COL_RED);
        }
    }

    // ── Row 3: [?123] [   SPACE   ] [ESC] [OK] ───────────
    // Layout: 1+46 + 3+88 + 3+44 + 3+50+1 = 239
    int y3 = KBD_Y + KBD_ROW_H * 3;
    const char* modLbl = (k.mode == 2) ? "abc" : "?123";
    _key(tft, 1,   y3+1,  46, KBD_KEY_H-1, modLbl,
         tft.color565(14,28,52), tft.color565(30,55,95), COL_CYAN);
    _key(tft, 50,  y3+1,  88, KBD_KEY_H-1, "SPACE",
         tft.color565(14,28,52), tft.color565(30,55,95), COL_GRAY);
    _key(tft, 141, y3+1,  44, KBD_KEY_H-1, "ESC",
         tft.color565(55,20,8), COL_ORANGE, COL_ORANGE);
    _key(tft, 188, y3+1,  51, KBD_KEY_H-1, " OK ",
         tft.color565(10,50,20), COL_GREEN, COL_GREEN);
}

inline bool ui_keyboardTouch(int tx, int ty, KeyboardState& k) {
    int len = strlen(k.text);

    for (int r = 0; r < 3; r++) {
        const char* row = _r[r][k.mode];
        int n  = strlen(row);
        int ky = KBD_Y + r * KBD_ROW_H;
        if (ty < ky || ty >= ky + KBD_ROW_H) continue;

        auto addChar = [&](char c) {
            if (len < (int)k.maxLen) { k.text[len]=c; k.text[len+1]='\0'; }
            if (k.mode == 1) k.mode = 0;
        };

        if (r == 0) {
            addChar(row[constrain(tx / KBD_KEY_W, 0, n-1)]);
        } else if (r == 1) {
            int st = (SCREEN_W - n * KBD_KEY_W) / 2;
            addChar(row[constrain((tx - st) / KBD_KEY_W, 0, n-1)]);
        } else {
            int n2    = n;
            int sideW = (SCREEN_W - n2 * KBD_KEY_W) / 2 - 2;
            int st2   = sideW + 2;
            int delX  = st2 + n2 * KBD_KEY_W;
            if (tx < st2) {
                if (k.mode==0) k.mode=1; else if (k.mode==1) k.mode=0; else k.mode=0;
            } else if (tx >= delX) {
                if (len > 0) k.text[len-1] = '\0';
            } else {
                addChar(row[constrain((tx - st2) / KBD_KEY_W, 0, n2-1)]);
            }
        }
        return true;
    }

    // Row 3: [?123 x=1..46] [SPACE x=50..137] [ESC x=141..184] [OK x=188..239]
    int y3 = KBD_Y + KBD_ROW_H * 3;
    if (ty >= y3) {
        if      (tx < 50)  { k.mode = (k.mode==2) ? 0 : 2; }
        else if (tx < 141) { if (len < (int)k.maxLen) { k.text[len]=' '; k.text[len+1]='\0'; } }
        else if (tx < 188) { k.cancelled = true; }   // ESC — huỷ
        else               { k.done = true; }          // OK — lưu
        return true;
    }
    return false;
}
