#include "wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>

static WiFiManager wifiManager;
static Preferences preferences;

// MQTT & Device parameters lưu trong NVS
static char mqtt_server[40] = "mqtt.fuvitech.vn";
static char mqtt_port[6] = "1883";
static char mqtt_topic[100] = "NhaNamFV/dataSensor";
static char device_id[30] = "TEST123";  // 

// WiFiManager custom parameters
static WiFiManagerParameter custom_mqtt_server("server", "MQTT Server", mqtt_server, 40);
static WiFiManagerParameter custom_mqtt_port("port", "MQTT Port", mqtt_port, 6);
static WiFiManagerParameter custom_mqtt_topic("topic", "MQTT Topic", mqtt_topic, 100);
static WiFiManagerParameter custom_device_id("device_id", "Device ID", device_id, 30);  // ⭐ Thêm parameter

// Load config từ NVS
void loadMqttConfig() {
  preferences.begin("mqtt-config", true); // read-only
  
  String server = preferences.getString("server", "mqtt.fuvitech.vn");
  server.toCharArray(mqtt_server, 40);
  
  int port = preferences.getInt("port", 1883);
  snprintf(mqtt_port, 6, "%d", port);
  
  String topic = preferences.getString("topic", "NhaNamFV/dataSensor");
  topic.toCharArray(mqtt_topic, 100);
  
  // ⭐ Load Device ID
  String devId = preferences.getString("device_id", "TEST123");
  devId.toCharArray(device_id, 30);
  
  preferences.end();
  
  Serial.println("Loaded MQTT config from NVS:");
  Serial.print("  Server: "); Serial.println(mqtt_server);
  Serial.print("  Port: "); Serial.println(mqtt_port);
  Serial.print("  Topic: "); Serial.println(mqtt_topic);
  Serial.print("  Device ID: "); Serial.println(device_id);  // ⭐
}

// Save config vào NVS
void saveMqttConfig() {
  preferences.begin("mqtt-config", false); // read-write
  
  preferences.putString("server", mqtt_server);
  preferences.putInt("port", atoi(mqtt_port));
  preferences.putString("topic", mqtt_topic);
  preferences.putString("device_id", device_id);  // ⭐ Lưu Device ID
  
  preferences.end();
  
  Serial.println("Saved MQTT config to NVS");
}

// Callback khi bắt đầu config portal
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("\n========================================");
  Serial.println("         CONFIG MODE STARTED");
  Serial.println("========================================");
  Serial.print("AP Name: ");
  Serial.println(myWiFiManager->getConfigPortalSSID());
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("\n--- INSTRUCTIONS ---");
  Serial.println("1. Connect to WiFi: " + myWiFiManager->getConfigPortalSSID());
  Serial.println("2. Open browser: http://192.168.4.1");
  Serial.println("3. Configure settings:");
  Serial.println("   - WiFi SSID & Password");
  Serial.println("   - Device ID (default: TEST123)");  // ⭐
  Serial.println("   - MQTT Server (default: mqtt.fuvitech.vn)");
  Serial.println("   - MQTT Port (default: 1883)");
  Serial.println("   - MQTT Topic (default: NhaNamFV/dataSensor)");
  Serial.println("4. Click 'Save' button");
  Serial.println("========================================\n");
}

bool wifi_init(const char *ap_name, const char *ap_password) {
  Serial.println("Starting WiFi setup...");

  loadMqttConfig();

  // Đọc WiFi config từ NVS
  Preferences prefs;
  prefs.begin("wifi-config", true);
  String savedSSID = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  WiFi.mode(WIFI_STA);
  if (savedSSID == "") {
    Serial.println("\nNo saved WiFi found, using default Fuvitech");
    WiFi.begin("Fuvitech", "fuvitech.vn");
  } else {
    Serial.printf("\nConnecting to saved WiFi: %s\n", savedSSID.c_str());
    WiFi.begin(savedSSID.c_str(), savedPass.c_str());
  }

  // Chờ tối đa 5 giây (10 * 500ms) để không block thiết bị quá lâu
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n⚠ WiFi not connected! Continuing in offline mode...");
    return false;
  }

  Serial.println("\n✓ WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  return true;
}

bool wifi_isConnected() { 
  return WiFi.status() == WL_CONNECTED; 
}

void wifi_reset() {
  Serial.println("Resetting WiFi settings...");
  wifiManager.resetSettings();
  
  // Xóa cả MQTT config
  preferences.begin("mqtt-config", false);
  preferences.clear();
  preferences.end();
  
  delay(1000);
  ESP.restart();
}

String wifi_getIP() {
  if (wifi_isConnected()) {
    return WiFi.localIP().toString();
  }
  return "Not connected";
}

bool wifi_startConfigPortal(const char *ap_name, const char *ap_password) {
  Serial.println("Starting WiFi Config Portal...");
  
  WiFi.disconnect(true);
  delay(1000);
  
  // Load config hiện tại
  loadMqttConfig();
  
  // ⭐ Biến flag để detect khi user nhấn Save
  static bool paramsSaved = false;
  paramsSaved = false;
  
  WiFiManager freshWifiManager;
  
  freshWifiManager.setAPCallback(configModeCallback);
  
  custom_device_id.setValue(device_id, 30);
  custom_mqtt_server.setValue(mqtt_server, 40);
  custom_mqtt_port.setValue(mqtt_port, 6);
  custom_mqtt_topic.setValue(mqtt_topic, 100);
  
  freshWifiManager.addParameter(&custom_device_id);
  freshWifiManager.addParameter(&custom_mqtt_server);
  freshWifiManager.addParameter(&custom_mqtt_port);
  freshWifiManager.addParameter(&custom_mqtt_topic);
  
  // ⭐ Callback khi user nhấn Save (chỉ save params, chưa connect WiFi)
  freshWifiManager.setSaveParamsCallback([&freshWifiManager]() {
    Serial.println("\n⭐ Save button pressed! Updating config...");
    
    // Đọc giá trị mới từ form
    strcpy(device_id, custom_device_id.getValue());
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(mqtt_topic, custom_mqtt_topic.getValue());
    
    // Lưu vào NVS
    saveMqttConfig();
    
    Serial.println("✓ Config saved to NVS:");
    Serial.print("  Device ID: "); Serial.println(device_id);
    Serial.print("  MQTT Server: "); Serial.println(mqtt_server);
    Serial.print("  MQTT Port: "); Serial.println(mqtt_port);
    Serial.print("  MQTT Topic: "); Serial.println(mqtt_topic);
    
    paramsSaved = true;
    
    // ⭐ Đóng config portal ngay lập tức
    Serial.println("Closing config portal...");
    // Trick: set timeout = 1 để portal tự thoát
    freshWifiManager.setConfigPortalTimeout(1);
  });
  
  // Callback khi WiFi credentials được save (connect WiFi mới)
  freshWifiManager.setSaveConfigCallback([]() {
    Serial.println("WiFi credentials saved");
  });
  
  freshWifiManager.setConfigPortalTimeout(180); // Timeout ban đầu
  
  // ⭐ Thêm menu với nút restart/exit
  std::vector<const char *> menu = {"wifi", "param", "sep", "restart", "exit"};
  freshWifiManager.setMenu(menu);
  
  bool connected;
  if (ap_password != NULL) {
    connected = freshWifiManager.startConfigPortal(ap_name, ap_password);
  } else {
    connected = freshWifiManager.startConfigPortal(ap_name);
  }
  
  // ⭐ Xử lý sau khi portal đóng
  Serial.println("\nConfig portal closed");
  
  if (connected) {
    // User đã connect WiFi mới
    Serial.println("✓ WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else if (paramsSaved) {
    // User chỉ save params, không đổi WiFi
    Serial.println("⚠ No WiFi change detected, reconnecting to previous network...");
    
    // Reconnect WiFi cũ
    WiFi.begin();
    
    // Đợi tối đa 10 giây
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      Serial.print(".");
      timeout++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✓ Reconnected to previous WiFi");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      return true;
    } else {
      Serial.println("✗ Failed to reconnect");
      return false;
    }
  } else {
    // Timeout hoặc user click Exit
    Serial.println("⚠ Portal timeout or exit");
    
    // Thử reconnect WiFi cũ
    WiFi.begin();
    delay(5000);
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✓ Reconnected to previous WiFi");
      return true;
    }
    return false;
  }
}

const char* wifi_getMqttServer() {
  return mqtt_server;
}

int wifi_getMqttPort() {
  return atoi(mqtt_port);
}

const char* wifi_getMqttTopic() {
  return mqtt_topic;
}

const char* wifi_getDeviceId() {
  return device_id;
}
