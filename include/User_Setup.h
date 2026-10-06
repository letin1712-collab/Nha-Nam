// ============================================================
// User_Setup.h cho ESP32-2432S028 (CYD - Cheap Yellow Display)
// ILI9341, 240x320 pixels
// Đặt file này trong thư mục include/ của project
// ============================================================

#define USER_SETUP_LOADED    // Báo cho TFT_eSPI biết dùng file này

// Driver
#define ILI9341_DRIVER

// Screen resolution (portrait gốc - library xử lý rotation)
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

#define TFT_MOSI  13
#define TFT_SCLK  14
#define TFT_CS    15
#define TFT_DC     2
#define TFT_RST   -1  

#define TFT_BL    21
#define TFT_BACKLIGHT_ON HIGH

// Fonts cần load
#define LOAD_GLCD    // Font 1. Original Adafruit 8 pixel font
#define LOAD_FONT2   // Font 2. Small 16 pixel high font
#define LOAD_FONT4   // Font 4. Medium 26 pixel high font
#define LOAD_FONT6   // Font 6. Large 48 pixel font
#define LOAD_FONT7   // Font 7. 7 segment 48 pixel font
#define LOAD_FONT8   // Font 8. Large 75 pixel font
#define LOAD_GFXFF   // FreeFonts (Adafruit compatible)

#define SMOOTH_FONT

// SPI speed
#define SPI_FREQUENCY        55000000
#define SPI_READ_FREQUENCY    5000000
#define SPI_TOUCH_FREQUENCY   2500000
