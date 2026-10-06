#include <Arduino.h>
#include <FS.h>
#include <Wire.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "bh1750_sensor.h"
#include "scd30_sensor.h"
#include "wifi_manager.h"
#include "thingsboard_client.h"
#include "mqtt_client.h"
#include "ota_manager.h"
#include "warranty_manager.h"
#include "espnow_sender.h"
#include "ui_config.h"
#include "ui_draw.h"
#include "ui_keyboard.h"
#include <Preferences.h>

#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

#define TS_X_MIN   633  
#define TS_X_MAX  3375   // portrait right
#define TS_Y_MIN   517   // portrait top
#define TS_Y_MAX  3593   // portrait bottom

#define BOOT_BUTTON 0

static constexpr unsigned long SENSOR_READ_INTERVAL_MS     = 2000;
static constexpr unsigned long CLOUD_PUBLISH_INTERVAL_MS   = 10000;

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft;
bool wifiWasConnected = false;
bool inConfigMode     = false;
bool scd30Available   = false;
bool bh1750Available  = false;

int  currentPage    = PAGE_DASHBOARD;
bool needFullRedraw = true;

unsigned long previousMillis  = 0;
unsigned long lastWiFiCheck   = 0;
unsigned long lastMqttPublish = 0;
unsigned long wifiLostTime    = 0;
unsigned long lastTouchMs     = 0;
unsigned long lastActivityMs  = 0; 
unsigned long lastTimeUpdate  = 0; 
bool isScreenOn               = true;

static constexpr unsigned long SCREEN_TIMEOUT_MS = 3600000UL; 

SensorData sensors;
NetData    network;

// Keyboard
bool          kbdActive = false;
KeyboardState kbdState;
int           kbdField  = -1;  

char pendingSSID[40] = {0};

char timeStr[10] = "";


void updateTime() {
    struct tm t;
    if (getLocalTime(&t, 10))
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d", t.tm_hour, t.tm_min);
}

void syncNetData(bool mqttOk) {
    network.wifiOk = wifi_isConnected();
    network.mqttOk = mqttOk;
    if (network.wifiOk) {
        network.ssid = WiFi.SSID();
        network.ip   = wifi_getIP();
    }
    network.mqttHost  = mqtt_getServer();
    network.mqttPort  = mqtt_getPort();
    network.mqttUser  = mqtt_getUsername();
    network.mqttPass  = mqtt_getPassword();
    network.mqttTopic = mqtt_getTopic();
    network.deviceId  = wifi_getDeviceId();
    network.userMqttEnabled = mqtt_userEnabled();
}

bool getTouch(int& tx, int& ty) {
    if (!ts.touched()) return false;
    TS_Point p = ts.getPoint();
    if (p.z < 150) return false;

    tx = map(p.x, TS_X_MIN, TS_X_MAX, 0, SCREEN_W - 1);
    ty = map(p.y, TS_Y_MIN, TS_Y_MAX, 0, SCREEN_H - 1);
    Serial.printf("TOUCH: raw(%d,%d,z=%d) -> (%d,%d)\n", p.x, p.y, p.z, tx, ty);
    tx = constrain(tx, 0, SCREEN_W - 1);
    ty = constrain(ty, 0, SCREEN_H - 1);
    return true;
}

void openKeyboard(int field, const char* lbl, const char* cur, bool pass = false, int maxL = KBD_MAX_LEN) {
    kbdField  = field;
    kbdActive = true;
    kbdState  = KeyboardState{};
    kbdState.label  = lbl;
    kbdState.isPass = pass;
    kbdState.maxLen = min(maxL, (int)KBD_MAX_LEN);
    strncpy(kbdState.text, cur, KBD_MAX_LEN);
    kbdState.text[KBD_MAX_LEN] = '\0';
}

void applyKeyboardResult() {
    Preferences prefs;
    String val = String(kbdState.text);

    switch (kbdField) {
        case 0: 
            strncpy(pendingSSID, kbdState.text, sizeof(pendingSSID)-1);
            pendingSSID[sizeof(pendingSSID)-1] = '\0';
            network.ssid = String(pendingSSID);  
            openKeyboard(1, "PASS", "", false, 32);
            ui_drawKeyboard(tft, kbdState);     
            return;

        case 1:
            Serial.printf("Connecting to WiFi: %s\n", pendingSSID);
            
            prefs.begin("wifi-config", false);
            prefs.putString("ssid", pendingSSID);
            prefs.putString("pass", kbdState.text);
            prefs.end();

            WiFi.disconnect(false, true); // Xoá kết nối cũ
            WiFi.begin(pendingSSID, kbdState.text);
            network.ssid = String(pendingSSID);
            break;

        case 2: // User MQTT Host
            mqtt_setUserServer(val.c_str());
            network.mqttHost = val; break;

        case 3: // User MQTT Port
            mqtt_setUserPort(val.toInt());
            network.mqttPort = val.toInt(); break;

        case 4: // User MQTT Username
            mqtt_setUserUsername(val.c_str());
            network.mqttUser = val; break;

        case 5: // User MQTT Password
            mqtt_setUserPassword(val.c_str());
            network.mqttPass = val; break;

        case 6: // User MQTT Topic
            mqtt_setUserTopic(val.c_str());
            network.mqttTopic = val; break;
    }
    if (kbdField >= 2) {
        mqtt_connect();
        syncNetData(tb_isConnected());
    }
    kbdActive = false;
    kbdField  = -1;
    needFullRedraw = true;
}

// ---- Field tap positions on WiFi page (Portrait) ----
#define WIFI_FIELD_Y0  (CONTENT_Y + 100) // SSID row
#define WIFI_FIELD_H   26

// ---- Field tap positions on MQTT page (Portrait) ----
#define MQTT_SWITCH_X  120
#define MQTT_SWITCH_Y  (CONTENT_Y + 54)
#define MQTT_SWITCH_W  106
#define MQTT_SWITCH_H  34
#define MQTT_FIELD_Y0  (CONTENT_Y + 98)
#define MQTT_FIELD_H   25
#define MQTT_FIELD_STEP 28

// ============================================================
// Handle all touches
// ============================================================
void handleTouch(int tx, int ty) {
    // ── Keyboard active ──────────────────────────────────────
    if (kbdActive) {
        if (ui_keyboardTouch(tx, ty, kbdState)) {
            if (kbdState.done) {
                applyKeyboardResult();
            } else if (kbdState.cancelled) {
                // ESC: đóng bàn phím, giữ nguyên giá trị cũ
                kbdActive = false;
                kbdField  = -1;
                needFullRedraw = true;
            } else {
                ui_drawKeyboard(tft, kbdState);
            }
        }
        return;
    }

    // ── Tap góc trái TopBar (vùng ẩn) → mở trang thông tin ──
    if (ty < TOPBAR_H && tx < 80) {
        currentPage = PAGE_INFO;
        needFullRedraw = true;
        return;
    }

    // ── Nav bar ──────────────────────────────────────────────
    if (ty >= NAV_Y) {
        int tab = constrain(tx / NAV_ITEM_W, 0, 2);
        if (tab != currentPage) { currentPage = tab; needFullRedraw = true; }
        return;
    }

    // ── Page INFO: nút BACK / UPDATE ─────────────────────────
    if (currentPage == PAGE_INFO) {
        if (ui_tapSetupBtn(tx, ty)) {
            if (tx < SCREEN_W / 2) {
                // Nửa trái -> BACK
                currentPage = PAGE_DASHBOARD;
                needFullRedraw = true;
            } else {
                // Nửa phải -> UPDATE FW
                Serial.println("Triggering OTA Update...");
                // Mày cấu hình URL AWS S3 ở đây
                Serial.println("Triggering OTA check...");
                ota_checkAndUpdate("https://fuviair-ota.leanhduong1201.workers.dev/");
            }
        }
        return;
    }

    if (currentPage == PAGE_WIFI) {
        if ((ty >= WIFI_FIELD_Y0 && ty < WIFI_FIELD_Y0 + WIFI_FIELD_H) || ui_tapSetupBtn(tx, ty)) {
            openKeyboard(0, "SSID", network.ssid.c_str());
            ui_drawKeyboard(tft, kbdState);
        }
        return;
    }

    // ── MQTT page field taps ─────────────────────────────────
    if (currentPage == PAGE_MQTT) {
        // User MQTT switch
        if (tx >= MQTT_SWITCH_X && tx <= MQTT_SWITCH_X + MQTT_SWITCH_W &&
            ty >= MQTT_SWITCH_Y && ty <= MQTT_SWITCH_Y + MQTT_SWITCH_H) {
            mqtt_setUserEnabled(!mqtt_userEnabled());
            syncNetData(tb_isConnected());
            mqtt_connect();
            needFullRedraw = true;
            return;
        }

        const char* labels[] = { "HOST", "PORT", "USER", "PASS", "TOPIC" };
        String vals[] = { network.mqttHost, String(network.mqttPort),
                          network.mqttUser, network.mqttPass, network.mqttTopic };
        for (int i = 0; i < 5; i++) {
            int fy = MQTT_FIELD_Y0 + i * MQTT_FIELD_STEP;
            if (ty >= fy && ty < fy + MQTT_FIELD_H) {
                openKeyboard(2 + i, labels[i], vals[i].c_str(), i == 3,
                             (i == 1) ? 6 : KBD_MAX_LEN);
                ui_drawKeyboard(tft, kbdState);
                return;
            }
        }
    }
}



// ============================================================
// Splash screen on startup
// ============================================================
// ============================================================
// Splash screen — PORTRAIT 240×320
// ============================================================
void showSplash() {
    // 1. Nền Splash là Xám đen (Gunmetal) sang trọng, không chói mắt
    uint16_t COL_SPLASH_BG = tft.color565(30, 35, 42); // Xám đen đậm
    tft.fillScreen(COL_SPLASH_BG);

    // Xóa hiệu ứng gradient cũ (vì nó vẽ nền tối)
    // Vẽ hai đường sọc trên/dưới tạo điểm nhấn
    tft.drawFastHLine(0, 0, SCREEN_W, COL_CYAN);
    tft.drawFastHLine(0, 1, SCREEN_W, COL_CYAN);
    tft.drawFastHLine(0, SCREEN_H - 1, SCREEN_W, COL_CYAN);
    tft.drawFastHLine(0, SCREEN_H - 2, SCREEN_W, COL_CYAN);

    // 2. Tiêu đề (đã bỏ logo + chữ FUVIAIR)
    tft.setTextDatum(MC_DATUM);

    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor(COL_CYAN);
    tft.drawString("AIR MONITOR V2", SCREEN_W / 2, 128);

    // 4. Badges (cảm biến)
    int badgeY = 154;
    tft.setFreeFont(nullptr); 
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COL_GRAY);
    tft.setCursor(34, badgeY); tft.print("CO2");
    tft.setCursor(72, badgeY); tft.print("TEMP");
    tft.setCursor(118, badgeY); tft.print("HUMI");
    tft.setCursor(164, badgeY); tft.print("LUX");

    // 5. Thanh loading bar
    int barX = 24, barY = 184, barW = SCREEN_W - 48;
    tft.drawRoundRect(barX - 1, barY - 1, barW + 2, 14, 6, COL_GRAY_DIM);
    tft.setTextColor(COL_GRAY);
    tft.setCursor(barX, barY - 14);
    tft.print("Booting system...");

    for (int w = 0; w <= barW; w += 3) {
        // Đổi màu loading bar sang Xanh ngọc
        tft.fillRoundRect(barX, barY, w, 12, 5, COL_CYAN);
        if ((w % 18) == 0) delay(10);
    }
    delay(180);
}
// ============================================================
// setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(100);

    pinMode(BOOT_BUTTON, INPUT);
    pinMode(21, OUTPUT); // TFT_BL
    digitalWrite(21, HIGH); // Bật sáng đèn nền màn hình

    // TFT init first (for splash)
    tft.init();
    tft.setRotation(0);  // PORTRAIT — USB ở dưới
    tft.fillScreen(COL_BG);
    showSplash();

    // Touchscreen
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(0);  // Match portrait

    // I2C sensors (SDA=27, SCL=22)
    Serial.println("Initializing I2C and sensors...");
    Wire.begin(27, 22);
    Wire.setClock(100000);
    Wire.setTimeout(5000);
    delay(3000);

    Serial.println("Initializing SCD30 (CO2 + temperature/humidity)...");
    scd30Available = scd30_init();
    if (!scd30Available)
        Serial.println("Warning: SCD30 not found, continuing without CO2/temp/humidity...");
    delay(500);

    Serial.println("Initializing BH1750...");
    bh1750Available = bh1750_init();
    if (!bh1750Available)
        Serial.println("Warning: BH1750 not found, continuing without it...");
    delay(500);

    // WiFi
    Serial.println("Initializing WiFi...");
    wifi_init("ESP32-Sensor", NULL);
    wifiWasConnected = true;
    delay(1000);

    // NTP
    Serial.println("Syncing NTP...");
    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    struct tm ti;
    if (getLocalTime(&ti)) {
        Serial.println("Time synced!");
        updateTime();
    }

    // ESP-NOW (gọi trước mqtt_init để ctrlId được load cho topic MAC exchange)
    Serial.println("Initializing ESP-NOW...");
    espNowInit();

    // MQTT dev/user
    Serial.println("Initializing MQTT...");
    mqtt_init();
    if (wifi_isConnected()) mqtt_connect();

    // ThingsBoard
    Serial.println("Initializing ThingsBoard...");
    warranty_init(); // Load trạng thái bảo hành từ NVS
    tb_init();
    bool tbOk = wifi_isConnected() && tb_connect();
    if (tbOk) Serial.println("ThingsBoard connected!");
    else      Serial.println("ThingsBoard failed, will retry...");

    syncNetData(tbOk);
    sensors.scd30Ok  = scd30Available;
    sensors.bh1750Ok = bh1750Available;

    needFullRedraw = true;
    Serial.println("Setup complete!");
}

// ============================================================
// loop()
// ============================================================
void loop() {
    unsigned long now = millis();

    // ---- BOOT button: hold 3s -> pairing mode | hold 6s -> WiFi config | hold 10s + 3 taps -> reset warranty ----
    static unsigned long bootPressTime = 0;
    static bool bootPressed = false;
    static bool actionTriggered = false;
    static bool warrantyResetReady = false;
    static int warrantyTapCount = 0;
    static unsigned long lastWarrantyTap = 0;

    if (digitalRead(BOOT_BUTTON) == LOW) {
        if (!bootPressed) {
            bootPressed = true;
            bootPressTime = now;
            actionTriggered = false;

            if (warrantyResetReady) {
                // Nhận tap để reset warranty
                if (now - lastWarrantyTap < 2000) {
                    warrantyTapCount++;
                    lastWarrantyTap = now;
                    Serial.printf("Warranty Reset Tap: %d\n", warrantyTapCount);
                    if (warrantyTapCount >= 3) {
                        warranty_reset();
                        warrantyResetReady = false;
                        warrantyTapCount = 0;
                        needFullRedraw = true;
                    }
                } else {
                    // Quá thời gian tap, reset lại biến tap
                    warrantyTapCount = 1;
                    lastWarrantyTap = now;
                }
                actionTriggered = true; // Tránh chạy vô các logic hold bên dưới
            }
        } else if (!actionTriggered) {
            if (now - bootPressTime >= 10000) {
                Serial.println("BOOT held 10s -> Ready for Warranty Reset. Tap 3 times to confirm.");
                warrantyResetReady = true;
                warrantyTapCount = 0;
                lastWarrantyTap = now;
                actionTriggered = true;
            } else if (now - bootPressTime >= 6000 && !warrantyResetReady) {
                Serial.println("BOOT held 6s -> WiFi config portal");
                inConfigMode = true;
                if (wifi_startConfigPortal("ESP32-Sensor", NULL)) {
                    wifiWasConnected = true; wifiLostTime = 0;
                    mqtt_connect();
                    bool ok = tb_connect();
                    syncNetData(ok);
                }
                delay(1000); inConfigMode = false; bootPressed = false;
                actionTriggered = true;
                needFullRedraw = true;
            } else if (now - bootPressTime >= 3000 && !warrantyResetReady) {
                Serial.println("BOOT held 3s -> Pairing mode 5 minutes");
                tb_startPairingWindow(300000UL);
                actionTriggered = true;
                needFullRedraw = true;
            }
        }
    } else {
        bootPressed = false;
        // Nếu đã quá 5s từ lần tap cuối mà chưa đủ 3 tap thì hủy trạng thái ready
        if (warrantyResetReady && now - lastWarrantyTap > 5000) {
            Serial.println("Warranty Reset aborted (timeout)");
            warrantyResetReady = false;
            warrantyTapCount = 0;
        }
    }

    // ---- WiFi watchdog (every 5s) ----
    if (now - lastWiFiCheck >= 5000 && !inConfigMode) {
        lastWiFiCheck = now;
        if (!wifi_isConnected()) {
            if (wifiWasConnected) {
                Serial.println("WiFi disconnected!");
                wifiLostTime = now; wifiWasConnected = false;
                syncNetData(false); needFullRedraw = true;
            } else {
                // Không auto-portal để tránh block touch
                // User tự vào trang WiFi nhấn nút setup nếu cần
                Serial.println("WiFi still lost...");
            }
        } else {
            espNowOnWifiConnected(); // lưu kênh AP cho ESP-NOW fallback
            if (!wifiWasConnected) {
                Serial.println("WiFi reconnected!");
                wifiWasConnected = true; wifiLostTime = 0;
                espNowOnWifiReconnected(); // peer ESP-NOW quay lại channel của AP
                bool ok = tb_connect();
                syncNetData(ok); needFullRedraw = true;
            }
        }
    }

    // ---- Cloud keep-alive ----
    if (!inConfigMode && wifi_isConnected()) {
        tb_loop();
        mqtt_loop();
    }

    // ---- ESP-NOW: dò kênh router khi offline ----
    if (!inConfigMode) espnow_loop();

    // ---- Read sensors ----
    if (!inConfigMode && !kbdActive) {
        // Read and log every 2s
        if (now - previousMillis >= SENSOR_READ_INTERVAL_MS) {
            previousMillis = now;
            bool sensorUpdated = false;

            if (scd30Available && scd30_isDataReady()) {
                if (scd30_readData()) {
                    sensors.co2  = scd30_getCO2();
                    sensors.temp = scd30_getTemperature();
                    sensors.humi = scd30_getHumidity();
                    sensorUpdated = true;
                }
            }
            if (bh1750Available) {
                sensors.lux = bh1750_readLight();
                sensorUpdated = true;
            }

            if (sensorUpdated) {
                Serial.printf("Sensors -> Temp:%.2f C  Hum:%.2f%%  CO2:%.0fppm  Lux:%.1f\n",
                              sensors.temp, sensors.humi, sensors.co2, sensors.lux);
                // Cập nhật màn hình ngay sau khi có data mới
                if (!needFullRedraw && currentPage == PAGE_DASHBOARD) {
                    ui_updateDashboardValues(tft, sensors);
                }
            }
        }

        // ThingsBoard publish every 60s
        if (now - lastMqttPublish >= CLOUD_PUBLISH_INTERVAL_MS) {
            lastMqttPublish = now;
            if (scd30Available || bh1750Available) {
                if (wifi_isConnected()) {
                    bool tbOk = tb_publishSensorData(sensors.temp, sensors.humi,
                                                     sensors.co2, sensors.lux);
                    bool mqttOk = mqtt_publishSensorData(sensors.temp, sensors.humi,
                                                         sensors.co2, sensors.lux);
                    if (tbOk || mqttOk) {
                        Serial.println("Data published OK");
                        if (mqttOk) {
                            warranty_startIfNeeded(); // Kích hoạt bảo hành khi publish lên MQTT dev thành công
                        }
                        // Ctrl mất WiFi một mình (sensor vẫn online): publish cloud OK
                        // nhưng lâu không nghe ctrl heartbeat MAC qua MQTT -> gửi thêm
                        // ESP-NOW để board ctrl (đang offline) vẫn nhận được dữ liệu.
                        // Gate espNowRedundancyAllowed(): nếu ESP-NOW FAIL liên tiếp
                        // (ctrl tắt/không chạy ESP-NOW) -> ngừng redundancy, giữ WiFi
                        // cho cloud. Có ACK lại hoặc nhận MAC mới -> tự mở.
                        if (espNowCtrlMacStale(60000UL) && espNowRedundancyAllowed()) {
                            Serial.println("Ctrl MAC stale > 60s -> ESP-NOW redundancy");
                            espNowSendSensorData(sensors.temp, sensors.humi,
                                                 sensors.co2, sensors.lux);
                        }
                    } else {
                        // WiFi OK nhưng MQTT thất bại (mất internet) -> fallback ESP-NOW
                        Serial.println("Publish failed -> ESP-NOW fallback");
                        espNowSendSensorData(sensors.temp, sensors.humi,
                                             sensors.co2, sensors.lux);
                    }
                } else {
                    // Mất WiFi -> fallback ESP-NOW sang board điều khiển (ESPNOW_SENSOR_GUIDE.md)
                    espNowSendSensorData(sensors.temp, sensors.humi,
                                          sensors.co2, sensors.lux);
                }
            }
        }
    }

    // ---- Touch (debounce 150ms) ----
    if (now - lastTouchMs > 150) {
        int tx, ty;
        if (getTouch(tx, ty)) {
            lastTouchMs = now;
            lastActivityMs = now; // Cập nhật thời gian hoạt động

            if (!isScreenOn) {
                // Nếu màn hình đang tắt, chạm vào chỉ để bật lên
                Serial.println("Screen WAKE UP");
                digitalWrite(21, HIGH);
                isScreenOn = true;
            } else {
                // Nếu đang bật, xử lý touch như bình thường
                handleTouch(tx, ty);
            }
        }
    }

    // ---- Auto Screen Off (1 hour timeout) ----
    if (isScreenOn && (now - lastActivityMs >= SCREEN_TIMEOUT_MS)) {
        Serial.println("Screen TIMEOUT -> Turning OFF");
        digitalWrite(21, LOW); // TFT_BL = 21
        isScreenOn = false;
    }

    // ---- Display refresh ----
    if (needFullRedraw) {
        updateTime();
        syncNetData(network.mqttOk);
        ui_drawPage(tft, currentPage, sensors, network, timeStr);
        needFullRedraw = false;
        lastTimeUpdate = now;
    }

    // ---- Cập nhật đồng hồ mỗi 60s (không cần full redraw) ----
    if (!needFullRedraw && isScreenOn && now - lastTimeUpdate >= 60000) {
        char oldTime[10];
        strcpy(oldTime, timeStr);
        updateTime();
        if (strcmp(oldTime, timeStr) != 0) {
            ui_updateTopBarTime(tft, timeStr);
        }
        lastTimeUpdate = now;
    }
}