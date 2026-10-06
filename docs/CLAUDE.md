# CLAUDE.md — Hướng dẫn kiến trúc Project Aqua2 (STM32F407 + FreeRTOS + Arduino)

> File này dùng để AI assistant học cách project này được tổ chức, từ đó áp dụng đúng pattern khi làm việc với project tương tự.

---

## 1. Tổng quan Project

| Thông số | Giá trị |
|---|---|
| MCU | STM32F407VET6 |
| Framework | Arduino (STM32duino) + FreeRTOS |
| Build tool | PlatformIO |
| Model thiết bị | AQUA-I3 (thiết bị đo chất lượng nước ao nuôi) |
| Firmware version | V2.2.7 (xem `include/Version.h`) |

---

## 2. Cấu trúc thư mục

```
Aqua2/
├── src/                   ← File nguồn .cpp của từng module
│   ├── main.cpp           ← Entry point: init peripheral + start FreeRTOS
│   ├── Manager.cpp        ← Quản lý dữ liệu app, EEPROM, cấu hình
│   ├── TimeClock.cpp      ← RTC nội (STM32RTC) + FreeRTOS task
│   ├── Cloud.cpp          ← MQTT/ThingsBoard qua module 4G (BG95)
│   ├── SensorSchedule.cpp ← State machine đo cảm biến
│   ├── SerialConfigure.cpp← Nhận cấu hình qua UART
│   ├── UIView.cpp         ← Màn hình OLED (U8g2)
│   ├── UIButton.cpp       ← Xử lý nút bấm
│   ├── PHSen.cpp          ← Cảm biến pH (RS485/Modbus)
│   ├── DOSen.cpp          ← Cảm biến DO (RS485/Modbus)
│   ├── ECSen.cpp          ← Cảm biến EC (RS485/Modbus)
│   ├── Pump.cpp           ← Điều khiển bơm
│   ├── Valve.cpp          ← Điều khiển van
│   ├── FirmwareUpdate.cpp ← OTA firmware
│   └── ...                ← Các module khác
│
├── include/               ← Header .h tương ứng từng module
│   ├── Manager.h          ← Struct: app_data_t, device_authen_t, ...
│   ├── SensorSchedule.h   ← Struct + enum state machine
│   ├── Version.h          ← Định nghĩa version firmware/hardware
│   └── ...
│
├── lib/                   ← Thư viện local (C thuần)
│   ├── uart/
│   │   ├── usart.c/h      ← Khởi tạo UART1/2/3 bằng HAL
│   │   └── uart_interrupt.c/h ← IRQ handler USART1, USART2
│   ├── stm_log/
│   │   ├── stm_log.c/h    ← Hệ thống log màu kiểu ESP-IDF
│   └── cjson/             ← Thư viện JSON nhúng
│
├── platformio.ini         ← Cấu hình PlatformIO
├── custom_loader.ld       ← Linker script (bootloader offset 0x8000)
├── make_hex.py            ← Post-build: tạo file .hex
└── make_file.py           ← Post-build: tạo file release
```

---

## 3. Cách chia file và quản lý module

### Quy tắc chia file
- **1 chức năng = 1 cặp file** `ModuleName.cpp` + `ModuleName.h`
- Header đặt trong `include/`, source đặt trong `src/`
- Thư viện C thuần (không dùng Arduino class) đặt trong `lib/<tên_lib>/`
- Mỗi module có `static const char *TAG = "TÊN_MODULE"` để log có nhãn

### Quy tắc đặt tên hàm
```
<module><Action>()
```
Ví dụ:
- `managerInit()`, `managerStoreAppData()`, `managerLoadAuthen()`
- `timeClockInit()`, `timeClockGetHour()`, `timeClockSetTime()`
- `sensor_schedule_init()`, `sensor_schedule_send_control()`

### Thứ tự init trong `main.cpp`
```cpp
void setup() {
    initRegisterSystem();    // Set VTOR offset (bootloader)
    initPeripheral();        // UART, I2C, SPI
    cJSON_InitHooks(...);    // Cấp phát heap FreeRTOS cho cJSON
    stm_log_init(...);       // Chỉ định UART dùng để log
    managerInit();           // Load dữ liệu từ EEPROM
    firmwareInit();
    sensor_schedule_init();  // Tạo FreeRTOS task
    timeClockInit();
    feedMotorInit();
    uiButtonInit();
    uiViewInit();
    cloudInit();
    vTaskStartScheduler();   // Luôn gọi cuối cùng
}
void loop() { taskYIELD(); } // FreeRTOS chiếm quyền điều khiển
```

---

## 4. UART — Cấu hình và sử dụng

### Sơ đồ phân công UART

| UART | Instance | Baud | GPIO | Chức năng |
|------|----------|------|------|-----------|
| UART1 | `huart1` | 115200 | PA9(TX), PA10(RX) | Module 4G BG95 |
| UART2 | `huart2` | 9600 | PA2(TX), PA3(RX) | RS485 / Cấu hình |
| UART3 | `huart3` | 9600 | PD8(TX), PD9(RX) | **Debug log** |

### File khởi tạo UART (`lib/uart/usart.c`)
```c
// Khai báo extern handle — dùng ở mọi nơi
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

// Gọi trong initPeripheral()
MX_USART1_UART_Init();
MX_USART2_UART_Init();
MX_USART3_UART_Init();
```

### IRQ Handler (`lib/uart/uart_interrupt.c`)
```c
// Bắt buộc đăng ký handler để nhận interrupt
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart1); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }
```
> **Lưu ý quan trọng:** Khi dùng Arduino framework trên STM32duino, file interrupt phải là `.c` (không phải `.cpp`) và được đặt trong `lib/` để PlatformIO biên dịch đúng thứ tự.

### Enable UART trong platformio.ini
```ini
build_flags =
    -D HAL_UART_MODULE_ENABLED
    -DENABLE_HWSERIAL2
```

---

## 5. Debug Log — hệ thống STM_LOG

> Hệ thống log được thiết kế theo phong cách **ESP-IDF** với màu ANSI, timestamp, và tag module.

### Cấu hình (`lib/stm_log/stm_log.h`)
```c
#define CONFIG_UART_LOG_HANDLE   &huart3   // UART3 = cổng debug
#define CONFIG_UART_4G_HANDLE    &huart1   // UART1 = cổng 4G

#define CONFIG_LOG_MAXIMUM_LEVEL 5         // VERBOSE
#define CONFIG_LOG_ENABLE        1         // Bật/tắt toàn bộ log
```

### Khởi tạo (gọi trong `main.cpp`)
```c
stm_log_init(CONFIG_UART_LOG_HANDLE);  // &huart3
```

### Cách dùng — 5 mức log
```c
static const char *TAG = "MY_MODULE";

STM_LOGE(TAG, "Lỗi nghiêm trọng: %d", err_code);   // Đỏ
STM_LOGW(TAG, "Cảnh báo: giá trị %d", value);       // Vàng
STM_LOGI(TAG, "Thông tin: %s", message);             // Xanh lá
STM_LOGD(TAG, "Debug: pointer=0x%x", ptr);          // Trắng
STM_LOGV(TAG, "Verbose: raw data");                  // Trắng
```

### Output mẫu trên terminal (115200 baud, UART3)
```
I (1523) Manager: Load App success
W (1524) RTC: Time now: 12:0:0
E (1525) Manager: Invalid signature
```
Format: `[Level] (timestamp_ms) TAG: message`

### Tắt log từng mức (không xóa code)
```c
#define CONFIG_LOGD_ENABLE 0   // Tắt DEBUG
#define CONFIG_LOGV_ENABLE 0   // Tắt VERBOSE
```

---

## 6. RS485 — Giao tiếp Modbus với cảm biến

### Thư viện sử dụng
```ini
lib_deps =
    arduino-libraries/ArduinoModbus@^1.0.9
    arduino-libraries/ArduinoRS485@^1.1.0
```

### Pattern chuẩn cho cảm biến RS485 (PHSen, DOSen, ECSen)
```cpp
// include/PHSen.h
#ifndef PH_SENSOR_H
#define PH_SENSOR_H
#include "Arduino.h"

void phSensorInit(void);
float phSensorGetValue(void);
void phSensorCalib(float refValue);
#endif

// src/PHSen.cpp
#include "PHSen.h"
#include "stm_log.h"
#include <ArduinoModbus.h>
#include <ArduinoRS485.h>

static const char *TAG = "PH_SEN";
static float ph_value = 0.0f;

void phSensorInit(void) {
    // RS485 dùng UART2: PA2(TX), PA3(RX)
    // RS485Client.begin(baud, RS485_DEFAULT_DE_PIN);
    STM_LOGI(TAG, "PH Sensor init");
}
```

### Phân công UART2 cho RS485
- UART2 (PA2/PA3, 9600 baud) dùng cho RS485 Modbus
- RS485 cần chân DE/RE để chuyển hướng, cấu hình trong `HardwareSerial`

---

## 7. RTC — Đồng hồ thời gian thực

### Thư viện sử dụng
```ini
lib_deps =
    adafruit/RTClib@^2.1.4          # DS1307, DS3231 external RTC
    stm32duino/STM32duino RTC@^1.5.0 # RTC nội của STM32
```

### Cách dùng RTC nội STM32 (`src/TimeClock.cpp`)
```cpp
#include <STM32RTC.h>

STM32RTC &rtc = STM32RTC::getInstance();  // Singleton

void timeClockInit(void) {
    rtc.setClockSource(STM32RTC::LSE_CLOCK); // Dùng thạch anh ngoài 32.768kHz
    rtc.begin();                              // Khởi động, giữ nguyên thời gian

    if (!rtc.isConfigured()) {               // Chỉ set khi chưa có thời gian
        rtc.setHours(12);
        rtc.setMinutes(0);
        rtc.setSeconds(0);
        rtc.setDay(18);
        rtc.setMonth(3);
        rtc.setYear(2025);
    }
    xTaskCreate(timeTask, "RTC", 2*1024, NULL, 2, NULL);
}
```

### API đọc/ghi thời gian
```cpp
uint16_t h = timeClockGetHour();    // 0-23
uint16_t m = timeClockGetMinute();  // 0-59
timeClockSetTime(12, 30, 0);        // Set h:m:s

// Encode/decode thời gian thành index 30 phút (dùng cho lịch)
uint16_t idx = timeClockGetTimeNumber();     // 0=0h00, 1=0h30, 48=24h00
timeClockSetTimeNumber(idx);                 // Set theo index
timeClockSetCurrentTime((12<<8) | 30);      // High byte=hour, Low byte=minute
```

### Lưu ý LSE Clock
- Cần thạch anh 32.768 kHz ngoài (X2) kết nối PC14/PC15
- Nếu dùng LSI thì độ chính xác kém hơn, dùng `STM32RTC::LSI_CLOCK`

---

## 8. Quản lý bộ nhớ — EEPROM AT24C256 (I2C)

### Phần cứng
- IC: AT24C256 (I2C address 0x50, dung lượng 32KB)
- I2C: PB8(SCL), PB9(SDA), 100kHz

### Cấu hình I2C trong `main.cpp`
```cpp
Wire.setSCL(PB8);
Wire.setSDA(PB9);
Wire.begin();
Wire.setClock(100000);
```

### Phân vùng địa chỉ EEPROM
```c
#define ADDR_STORE_SCHEDULE_DATA  0       // Offset 0: lưu app_data_t
#define ADDR_STORE_AUTHEN         2048    // Offset 2048: lưu device_authen_t
```

### Pattern ghi EEPROM (page write, tối đa 32 byte/lần)
```cpp
#define EEPROM_PAGE_SIZE 32
#define EEPROM_ADDR      0x50

bool managerStoreAppData(app_data_t &param) {
    param.signature = SIGNATURE;  // 0xFEEDFEED — đánh dấu data hợp lệ
    uint8_t *dataPtr = (uint8_t*)&param;
    uint16_t dataSize = sizeof(app_data_t);
    uint16_t address = ADDR_STORE_SCHEDULE_DATA;
    uint16_t bytesWritten = 0;

    while (bytesWritten < dataSize) {
        uint16_t currentPageOffset = (address + bytesWritten) % EEPROM_PAGE_SIZE;
        uint16_t bytesToWrite = EEPROM_PAGE_SIZE - currentPageOffset;
        if (bytesToWrite > (dataSize - bytesWritten))
            bytesToWrite = dataSize - bytesWritten;

        Wire.beginTransmission(EEPROM_ADDR);
        Wire.write((address + bytesWritten) >> 8);   // Địa chỉ cao
        Wire.write((address + bytesWritten) & 0xFF); // Địa chỉ thấp
        Wire.write(dataPtr + bytesWritten, bytesToWrite);
        Wire.endTransmission();
        vTaskDelay(5 / portTICK_PERIOD_MS);  // Chờ EEPROM ghi xong (tmax=5ms)
        bytesWritten += bytesToWrite;
    }
    return true;
}
```

### Pattern đọc EEPROM
```cpp
bool managerLoadAppData(app_data_t &param) {
    uint8_t *dataPtr = (uint8_t*)&param;
    // ... đọc từng page
    Wire.requestFrom(EEPROM_ADDR, bytesToRead);

    // Kiểm tra signature sau khi đọc
    if (param.signature != SIGNATURE) {
        // Load giá trị mặc định
        param.schedule_setting.enable = 1;
        param.schedule_setting.measurement_interval = 0;
        return false;
    }
    return true;
}
```

### Struct dữ liệu lưu trữ
```cpp
// app_data_t: lưu cài đặt ứng dụng
typedef struct {
    uint32_t signature;                          // 0xFEEDFEED
    sensor_schedule_setting_t schedule_setting;  // Cài đặt lịch đo
} app_data_t;

// device_authen_t: lưu thông tin xác thực thiết bị
typedef struct {
    uint32_t signature;
    char device_id[32];     // Hardware ID
    char device_sk[32];     // Secret key
    char access_token[32];  // ThingsBoard token
    char qrcode[32];        // QR code string
} device_authen_t;
```

### Trigger lưu qua FreeRTOS Queue
```cpp
// Không ghi trực tiếp — gửi event vào queue
void managerSignalStore(void) {
    uint8_t event = STORE_APP_EVENT;
    xQueueSend(queueManager, &event, 0);  // Non-blocking
}

// Task riêng xử lý việc ghi để không block task khác
void managerTask(void *param) {
    while(1) {
        if (xQueueReceive(queueManager, &event, portMAX_DELAY) == pdTRUE) {
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Debounce 1s trước khi ghi
            managerStoreAppData(app_data);
        }
    }
}
```

---

## 9. FreeRTOS — Quản lý Task

### Stack size quy ước
```cpp
xTaskCreate(managerTask,   "Manager",  1024,    NULL, 2, NULL);
xTaskCreate(timeTask,      "RTC",      2*1024,  NULL, 2, NULL);
xTaskCreate(cloudTask,     "Cloud",    4096,    NULL, 2, NULL);  // Task nặng cần stack lớn
```

### Priority quy ước
- Priority 1: Task nền (ít quan trọng)
- Priority 2: Task chính (manager, RTC, sensor)
- Priority 3+: Task thời gian thực (không dùng trong project này)

### Giao tiếp giữa các task
```cpp
// Queue: gửi event từ task này sang task khác
QueueHandle_t queueManager = xQueueCreate(4, sizeof(uint8_t));
xQueueSend(queueManager, &event, 0);           // Non-blocking
xQueueReceive(queueManager, &event, portMAX_DELAY); // Blocking

// Semaphore: mutex truy cập tài nguyên dùng chung
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
xSemaphoreTake(mutex, portMAX_DELAY);
// ... critical section
xSemaphoreGive(mutex);
```

### cJSON với FreeRTOS heap
```cpp
// Bắt buộc hook cJSON dùng FreeRTOS heap, không dùng malloc thông thường
cJSON_Hooks hooks = {
    .malloc_fn = pvPortMalloc,
    .free_fn   = vPortFree,
};
cJSON_InitHooks(&hooks);  // Gọi trước khi dùng bất kỳ hàm cJSON nào
```

---

## 10. Bootloader — Offset địa chỉ

### Vector Table Offset
```cpp
// main.cpp — gọi đầu tiên trong setup()
void initRegisterSystem(void) {
    SCB->VTOR = 0x0800C000;  // Bootloader chiếm 48KB đầu (0x8000 = 32KB tối thiểu)
    __DSB();
}
```

### platformio.ini
```ini
board_build.ldscript = custom_loader.ld  # Linker script tuỳ chỉnh
extra_scripts =
    post:make_hex.py   # Tạo .hex sau khi build
    post:make_file.py  # Tạo file release
```

### custom_loader.ld
- Flash bắt đầu từ `0x0800C000` (offset 0xC000 = 48KB)
- Dành 48KB đầu cho bootloader OTA

---

## 11. Debug lỗi — Quy trình chuẩn

### Kiểm tra I2C devices
```cpp
void scanI2C() {
    for (uint8_t address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            STM_LOGW("App", "Found I2C device at 0x%x", address);
        }
    }
}
```

### Các lỗi thường gặp và cách debug

#### Lỗi 1: EEPROM không lưu được
```
// Symptom: log "Invalid signature" sau mỗi lần reset
E (xxx) Manager: Invalid signature
// Fix: Kiểm tra địa chỉ I2C, vDelay sau mỗi page write
```

#### Lỗi 2: Stack overflow FreeRTOS
```
// Symptom: hệ thống reset ngẫu nhiên, HardFault
// Fix: Tăng stack size của task bị nghi ngờ
// Debug: xTaskGetStackHighWaterMark(taskHandle) -> giá trị nhỏ = stack sắp hết
UBaseType_t watermark = uxTaskGetStackHighWaterMark(NULL);
STM_LOGD(TAG, "Stack remaining: %d words", watermark);
```

#### Lỗi 3: UART không nhận data
```
// Symptom: không có log output
// Fix checklist:
// 1. Kiểm tra GPIO pin mapping trong usart.c
// 2. Kiểm tra HAL_NVIC_EnableIRQ() đã được gọi chưa
// 3. Kiểm tra huart handle đúng chưa trong stm_log.h
// 4. Baud rate terminal = 9600 (UART3)
```

#### Lỗi 4: RTC không giữ thời gian sau khi mất điện
```
// Symptom: giờ reset về 12:00:00 sau mỗi lần reset
// Fix: Kiểm tra thạch anh LSE 32.768kHz
// Kiểm tra: rtc.isConfigured() trả về false
// Backup battery VBAT phải được cấp nguồn (pin CR2032)
```

#### Lỗi 5: HardFault khi dùng cJSON
```
// Symptom: crash khi parse JSON
// Fix: Đảm bảo cJSON_InitHooks() đã được gọi với FreeRTOS allocator
// Đảm bảo cJSON_Delete(json) sau khi dùng xong để tránh memory leak
```

### Factory Setting Mode
```cpp
// Giữ 2 nút PA5 + PB6 trong 3 giây khi boot
// → Vào mode nhận cấu hình qua UART3 (9600 baud)
// → Gửi JSON: {"secret_key":"xxx","hw_id":"xxx","qr_code":"xxx"}
bool isStartSetting(void) { ... }
```

---

## 12. Version Management

```c
// include/Version.h
#define FIRMWARE_VERSION_MAJOR  2
#define FIRMWARE_VERSION_MINOR  2
#define FIRMWARE_REVISION       7

// Tạo chuỗi: "V2.2.7 May 22 2026 10:30:00"
#define FIRMWARE_VER_FULL  FIRMWARE_VER " " FIRMWARE_DATE " " FIRMWARE_TIME

// In khi khởi động
STM_LOGW("Application", "_________Version: %s____________", FIRMWARE_VER_FULL);
```

---

## 13. Thư viện bên ngoài — Tóm tắt

| Thư viện | Dùng cho |
|---|---|
| `STM32duino FreeRTOS` | RTOS scheduler, task, queue, semaphore |
| `FlashStorage_STM32` | Lưu vào Flash nội MCU |
| `RTClib` | DS1307/DS3231 external RTC |
| `STM32duino RTC` | RTC nội STM32 |
| `U8g2` | Màn hình OLED |
| `HX711` | Cân nặng (load cell) |
| `ArduinoModbus` + `ArduinoRS485` | Giao tiếp RS485 Modbus |
| `TinyGSM` + `ThingsBoard` | Cloud IoT qua 4G |
| `cJSON` (local lib) | Parse/build JSON |
| `SPIMemory` + `SerialFlash` | SPI Flash ngoài |
| `at24cxxx` | EEPROM I2C AT24C256 |
| `QRCode` | Tạo QR code hiển thị màn hình |
| `CRC32` | Kiểm tra CRC firmware OTA |

---

## 14. Checklist khi tạo project mới từ pattern này

- [ ] Copy `lib/uart/` (usart.c/h, uart_interrupt.c/h) và chỉnh GPIO pins
- [ ] Copy `lib/stm_log/` và set `CONFIG_UART_LOG_HANDLE` đúng UART
- [ ] Cấu hình `platformio.ini`: platform, board, framework, build_flags
- [ ] Tạo `custom_loader.ld` nếu có bootloader, set offset VTOR
- [ ] Tạo `include/Version.h` với MAJOR.MINOR.REVISION
- [ ] Init UART trước khi gọi `stm_log_init()`
- [ ] Hook cJSON với FreeRTOS heap nếu dùng JSON
- [ ] Mỗi module có TAG riêng cho log
- [ ] Luôn kiểm tra signature khi load data từ EEPROM/Flash
- [ ] Dùng FreeRTOS Queue thay vì gọi store trực tiếp trong interrupt/task
- [ ] Stack size tối thiểu 1024 word (4KB), task Cloud cần 4096+
- [ ] Gọi `vTaskStartScheduler()` cuối cùng trong `setup()`
