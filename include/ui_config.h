#pragma once

// ============================================================
// Screen dimensions — PORTRAIT 240×320
// ============================================================
#define SCREEN_W  240
#define SCREEN_H  320

// ============================================================
// Colour palette (RGB565)
// ============================================================
#define COL_BG          0x0841
#define COL_CARD        0x10C3
#define COL_CARD2       0x0882
#define COL_CYAN        0x07FF
#define COL_CYAN_DIM    0x0577
#define COL_TEAL        0x03EF
#define COL_WHITE       0xFFFF
#define COL_GRAY        0x8410
#define COL_GRAY_DIM    0x4208
#define COL_GREEN       0x07E0
#define COL_RED         0xF800
#define COL_ORANGE      0xFBE0
#define COL_YELLOW      0xFFE0
#define COL_NAVBG       0x0861
#define COL_NAV_ACTIVE  0x07FF

// ============================================================
// Layout — portrait
// ============================================================
#define TOPBAR_H    30
#define NAVBAR_H    48
#define CONTENT_Y   (TOPBAR_H + 2)
#define CONTENT_H   (SCREEN_H - TOPBAR_H - NAVBAR_H - 2)
// CONTENT_H = 320 - 30 - 48 - 2 = 240px

// Dashboard 2×2 cards
#define CARD_COLS   2
#define CARD_ROWS   2
#define CARD_MARGIN 5
#define CARD_W      ((SCREEN_W - CARD_MARGIN * 3) / 2)   // (240-15)/2 = 112
#define CARD_H      ((CONTENT_H - CARD_MARGIN * 3) / 2)  // (240-15)/2 = 112
#define CARD_RADIUS 8

// Nav bar
#define NAV_Y       (SCREEN_H - NAVBAR_H)    // 320-48 = 272
#define NAV_ITEM_W  (SCREEN_W / 3)           // 80px each (3 tab)

// ============================================================
// Pages
// ============================================================
#define PAGE_DASHBOARD  0
#define PAGE_WIFI       1
#define PAGE_MQTT       2
#define PAGE_INFO       3