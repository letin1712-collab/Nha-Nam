# Hướng Dẫn Gửi Dữ Liệu Lên Cloud – Project Aqua2 (MegaAqua)

> **Mục tiêu:** Tài liệu này mô tả toàn bộ quy trình thêm một giá trị cảm biến mới vào hệ thống và gửi nó lên nền tảng IoT ThingsBoard.  
> **Áp dụng cho:** STM32F407VET6 · Framework Arduino · FreeRTOS · TinyGSM · ThingsBoard SDK

---

## 1. Kiến Trúc Tổng Quan

```
[Module Cảm Biến]
      │
      │  cloudThingsBoardSendEventData(event)   ← gửi vào Queue
      ▼
[FreeRTOS Queue: cloudQueueHandle]
      │
      │  xQueueReceive() trong cloudTask
      ▼
[cloudThingsBoardHandleEventData()]
      │
      ├─► sendTelemetryData()   → ThingsBoard (lưu lịch sử, vẽ đồ thị)
      └─► sendAttributeData()   → ThingsBoard (trạng thái hiện tại)
```

**Cơ chế:** Các module cảm biến **KHÔNG** gọi ThingsBoard trực tiếp. Chúng gửi một `event` vào **FreeRTOS Queue**. Task `cloudTask` (chạy riêng biệt) sẽ đọc queue và thực hiện gửi.

---

## 2. Các File Liên Quan

| File | Vai trò |
|---|---|
| `src/Cloud.cpp` | Toàn bộ logic kết nối, gửi/nhận dữ liệu ThingsBoard |
| `include/Cloud.h` | Khai báo enum event, struct data, và các hàm public |
| `src/<TênCảmBiến>.cpp` | Module cảm biến, nơi gọi `cloudThingsBoardSendEventData()` |

---

## 3. Quy Trình Thêm Giá Trị Mới – 4 Bước

### Bước 1: Khai Báo Key và Event Trong `Cloud.h`

**1a. Thêm event vào enum `event_update_t`:**

```cpp
// File: include/Cloud.h

typedef enum
{
    // ... các event hiện có ...
    EVENT_UPDATE_PH,
    EVENT_UPDATE_TEMP,
    EVENT_UPDATE_EC_TDS,

    // ✅ Thêm event mới tại đây:
    EVENT_UPDATE_CO2,
    EVENT_UPDATE_HUMIDITY,
    EVENT_UPDATE_AIR_TEMP,
    EVENT_UPDATE_LIGHT,

} event_update_t;
```

> **Lưu ý:** Giữ nguyên các event cũ, chỉ **append** thêm vào cuối enum.

---

### Bước 2: Định Nghĩa Key và Hàm Trong `Cloud.cpp`

**2a. Thêm key constants (theo pattern `{tên}_{đơn_vị}`):**

```cpp
// File: src/Cloud.cpp – phần khai báo constants (khoảng dòng 125–137)

// === Key hiện có ===
constexpr char PH_KEY[]   = "ph";
constexpr char DO_KEY[]   = "do_mgperl";
constexpr char TEMP_KEY[] = "temp_c";       // nhiệt độ NƯỚC (PT100)
constexpr char EC_KEY[]   = "ec_ms_cm";
constexpr char ORP_KEY[]  = "orp_mv";

// ✅ Thêm key mới:
constexpr char CO2_KEY[]      = "co2_ppm";          // CO₂ từ SCD41
constexpr char HUMIDITY_KEY[] = "humidity_percent";  // Độ ẩm từ SCD41
constexpr char AIR_TEMP_KEY[] = "air_temp_c";        // Nhiệt độ KK từ SCD41
constexpr char LIGHT_KEY[]    = "light_lux";         // Ánh sáng từ BH1750
```

> **Quy tắc đặt tên key:**
> - Viết **thường**, dùng `_` để phân cách
> - Format: `{tên_đại_lượng}_{đơn_vị}`
> - Phân biệt nguồn nếu cùng đại lượng (vd: `temp_c` = nước, `air_temp_c` = không khí)

**2b. Thêm cặp hàm Telemetry + Attribute cho mỗi giá trị:**

> **Telemetry** = dữ liệu có timestamp, dùng để vẽ đồ thị theo thời gian.  
> **Attribute** = trạng thái hiện tại của thiết bị (không có lịch sử).

```cpp
// ── CO2 ──────────────────────────────────────────────
void cloudThingsBoardTelemetryUpdateCO2(int val)
{
    if (tb.connected())
        tb.sendTelemetryData(CO2_KEY, val); // đơn vị ppm, không cần chia
}
void cloudThingsBoardAttributeUpdateCO2(int val)
{
    if (tb.connected())
        tb.sendAttributeData(CO2_KEY, val);
}

// ── Độ ẩm ────────────────────────────────────────────
void cloudThingsBoardTelemetryUpdateHumidity(int val)
{
    if (tb.connected())
        tb.sendTelemetryData(HUMIDITY_KEY, val / 100.0); // ví dụ nếu lưu x100
}
void cloudThingsBoardAttributeUpdateHumidity(int val)
{
    if (tb.connected())
        tb.sendAttributeData(HUMIDITY_KEY, val / 100.0);
}

// ── Nhiệt độ không khí ───────────────────────────────
void cloudThingsBoardTelemetryUpdateAirTemp(int val)
{
    if (tb.connected())
        tb.sendTelemetryData(AIR_TEMP_KEY, val / 100.0);
}
void cloudThingsBoardAttributeUpdateAirTemp(int val)
{
    if (tb.connected())
        tb.sendAttributeData(AIR_TEMP_KEY, val / 100.0);
}

// ── Ánh sáng ─────────────────────────────────────────
void cloudThingsBoardTelemetryUpdateLight(int val)
{
    if (tb.connected())
        tb.sendTelemetryData(LIGHT_KEY, val); // đơn vị lux
}
void cloudThingsBoardAttributeUpdateLight(int val)
{
    if (tb.connected())
        tb.sendAttributeData(LIGHT_KEY, val);
}
```

> **Lưu ý về scaling:**  
> Project này lưu giá trị dưới dạng `int` đã nhân hệ số (tránh float).  
> Ví dụ: pH = `7.123` được lưu là `7123` → khi gửi chia `/ 1000.0`.  
> Hãy xem module cảm biến cụ thể để biết hệ số scaling của từng giá trị.

**2c. Thêm case vào `cloudThingsBoardHandleEventData()`:**

```cpp
// File: src/Cloud.cpp – hàm cloudThingsBoardHandleEventData()

switch (data_update.type)
{
    // ... các case hiện có ...

    // ✅ Thêm các case mới:
    case EVENT_UPDATE_CO2:
    {
        cloudThingsBoardTelemetryUpdateCO2(data_update.data);
        cloudThingsBoardAttributeUpdateCO2(data_update.data);
        break;
    }

    case EVENT_UPDATE_HUMIDITY:
    {
        cloudThingsBoardTelemetryUpdateHumidity(data_update.data);
        cloudThingsBoardAttributeUpdateHumidity(data_update.data);
        break;
    }

    case EVENT_UPDATE_AIR_TEMP:
    {
        cloudThingsBoardTelemetryUpdateAirTemp(data_update.data);
        cloudThingsBoardAttributeUpdateAirTemp(data_update.data);
        break;
    }

    case EVENT_UPDATE_LIGHT:
    {
        cloudThingsBoardTelemetryUpdateLight(data_update.data);
        cloudThingsBoardAttributeUpdateLight(data_update.data);
        break;
    }
}
```

---

### Bước 3: Khai Báo Prototype Trong `Cloud.h`

```cpp
// File: include/Cloud.h – thêm vào cuối, trước #endif

// CO2 (SCD41)
void cloudThingsBoardTelemetryUpdateCO2(int val);
void cloudThingsBoardAttributeUpdateCO2(int val);

// Humidity (SCD41)
void cloudThingsBoardTelemetryUpdateHumidity(int val);
void cloudThingsBoardAttributeUpdateHumidity(int val);

// Air Temperature (SCD41)
void cloudThingsBoardTelemetryUpdateAirTemp(int val);
void cloudThingsBoardAttributeUpdateAirTemp(int val);

// Light (BH1750)
void cloudThingsBoardTelemetryUpdateLight(int val);
void cloudThingsBoardAttributeUpdateLight(int val);
```

---

### Bước 4: Gọi Từ Module Cảm Biến

Tại nơi đọc được giá trị cảm biến (ví dụ trong `SCD41Sensor.cpp` hoặc `SensorSchedule.cpp`), gọi như sau:

```cpp
#include "Cloud.h"

// Sau khi đọc xong giá trị từ cảm biến:
int co2_value = scd41_read_co2();        // giả sử trả về ppm
int humidity  = scd41_read_humidity();   // giả sử x100 (7523 = 75.23%)
int air_temp  = scd41_read_temp();       // giả sử x100 (2510 = 25.10°C)
int light_lux = bh1750_read_lux();       // lux

// Gửi từng giá trị vào Queue:
data_update_t event;

event.type = EVENT_UPDATE_CO2;
event.data = (uint32_t)co2_value;
cloudThingsBoardSendEventData(event);

event.type = EVENT_UPDATE_HUMIDITY;
event.data = (uint32_t)humidity;
cloudThingsBoardSendEventData(event);

event.type = EVENT_UPDATE_AIR_TEMP;
event.data = (uint32_t)air_temp;
cloudThingsBoardSendEventData(event);

event.type = EVENT_UPDATE_LIGHT;
event.data = (uint32_t)light_lux;
cloudThingsBoardSendEventData(event);
```

> ⚠️ **Hàm `cloudThingsBoardSendEventData()` an toàn để gọi từ bất kỳ task FreeRTOS nào** vì nó chỉ dùng `xQueueSend()` (thread-safe). KHÔNG gọi `sendTelemetryData()` / `sendAttributeData()` trực tiếp từ ngoài Cloud module.

---

## 4. Tham Khảo – Danh Sách Key Hiện Có

| Key | Đơn vị | Scaling | Cảm biến |
|---|---|---|---|
| `ph` | pH | `val / 1000.0` | pH Sensor |
| `do_mgperl` | mg/L | `val / 1000.0` | DO Sensor |
| `tds` | mg/L | `val / 1000.0` | EC Sensor |
| `salt_ppthousand` | ppt | `val / 1000.0` | EC Sensor |
| `temp_c` | °C | `val / 1000.0` | PT100 (nhiệt độ **nước**) |
| `orp_mv` | mV | `val / 1000.0` | ORP Sensor |
| `ec_ms_cm` | mS/cm | `val / 1000.0` | EC Sensor |
| `error_pump` | string | — | Pump |
| `error_valve` | string | — | Valve |
| `error_phsen` | string | — | pH Sensor |
| `error_ecsen` | string | — | EC Sensor |
| `error_dosen` | string | — | DO Sensor |
| `fw_version` | string | — | System |
| `hw_version` | string | — | System |
| `model` | string | — | System |
| `run_state` | uint | — | System |
| `schedules` | JSON string | — | System |

---

## 5. Thông Tin Kết Nối ThingsBoard

| Thông số | Giá trị |
|---|---|
| Server | `tb1.fuvitech.vn` |
| Port | `1883` (MQTT, không mã hóa) |
| Protocol | MQTT qua TinyGSM (4G BG95) |
| APN | `internet` |
| Access Token | Lấy từ `managerGetAccessToken()` (lưu trong Flash) |
| Queue size | 20 phần tử (`data_update_t`) |
| Cloud task stack | 8 KB |

---

## 6. Danh Tính Thiết Bị, QR Code và Luồng Claim (Quan trọng)

Trước khi thiết bị có thể gửi dữ liệu lên ThingsBoard, nó cần được **xác thực và đăng ký** vào tài khoản người dùng. Đây là luồng hoạt động đầy đủ:

### 6.1 Các Thông Tin Định Danh (trong `Manager.cpp`)

```cpp
// File: src/Manager.cpp – hàm managerInit()

char *secretKey = "UWLS54";               // Khóa bí mật của thiết bị
char *hwId      = "EBXXT5RD34";           // Hardware ID – định danh phần cứng
char *qrCode    = "i=EBXXT5RD34&k=UWLS54"; // Nội dung QR Code hiển thị lên màn hình
```

Các giá trị này được lưu vào struct `device_authen_t` trong EEPROM (địa chỉ `0x800`):

```
device_authen_t
├── device_id[32]      → Hardware ID
├── device_sk[32]      → Secret Key
├── access_token[32]   → Token ThingsBoard (lấy sau khi Claim)
└── qrcode[32]         → Chuỗi QR Code
```

API để đọc/ghi từ module khác:
```cpp
managerGetDeviceID()       // lấy hw_id
managerGetDeviceSK()       // lấy secret_key
managerGetAccessToken()    // lấy access_token (dùng trong Cloud.cpp)
managergetQRCode()         // lấy chuỗi QR hiển thị lên màn hình
```

---

### 6.2 Luồng Claim Toàn Bộ

```
[Màn hình thiết bị]
  Hiển thị QR: "i=EBXXT5RD34&k=UWLS54"
        │
        │  User quét bằng Zalo Mini App
        ▼
[Zalo Mini App / Fuvitech Backend]
  Nhận hw_id + secret_key
  Đăng ký thiết bị vào tài khoản user
  Tạo access_token trên ThingsBoard
        │
        │  (song song) Thiết bị tự gửi heartbeat
        ▼
[Thiết bị – Cloud.cpp: cloudThingBoardClaim()]
  HTTP GET: gw.fuvitech.vn/api/v1/device-heartbeat/?k=UWLS54&i=EBXXT5RD34
        │
        │  Server trả về { "data": { "access_token": "xxx..." } }
        ▼
[Manager.cpp]
  managerSetAccessToken(access_token)
  → Lưu vào EEPROM
  → Bíp 8 tiếng
  → NVIC_SystemReset() – reset MCU
        │
        ▼
[Sau khi reset – Cloud.cpp: cloudTask()]
  tb.connect("tb1.fuvitech.vn", managerGetAccessToken(), 1883)
  → Kết nối ThingsBoard thành công
  → Bắt đầu gửi data cảm biến
```

---

### 6.3 Event Trigger Claim

Claim được kích hoạt khi nhận event `EVENT_UPDATE_CLAIM` trong queue:

```cpp
// Gửi event từ bất kỳ module nào (ví dụ: khi user nhấn nút trên UI)
data_update_t event;
event.type = EVENT_UPDATE_CLAIM;
event.data = 0;
cloudThingsBoardSendEventData(event);

// Cloud.cpp sẽ tự xử lý trong cloudThingsBoardHandleEventData():
case EVENT_UPDATE_CLAIM:
{
    // Thử claim tối đa 8 lần, mỗi lần cách nhau 2 giây
    for (int i = 0; i < 8; i++) {
        if (cloudThingBoardClaim(managerGetDeviceSK(), managerGetDeviceID()) == true)
            break;
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    break;
}
```

---

### 6.4 Lưu Ý Khi Deploy Nhiều Thiết Bị

> ⚠️ **Vấn đề hiện tại:** `hwId` và `secretKey` đang bị hardcode trong `managerInit()`.  
> Mỗi lần boot, 2 giá trị này bị **ghi đè lại** dù EEPROM đã có dữ liệu đúng.  
> Điều này có nghĩa là **mọi thiết bị flash cùng firmware sẽ có cùng 1 hwId** → xung đột Claim.

**Giải pháp đúng:** Chỉ ghi hardcode khi chưa có dữ liệu hợp lệ trong EEPROM:

```cpp
void managerInit(void)
{
    memset(&app_data, 0x00, sizeof(app_data_t));
    memset(&device_authen, 0x00, sizeof(device_authen_t));
    managerLoadAppData(app_data);
    managerLoadAuthen(device_authen);
    queueManager = xQueueCreate(4, sizeof(uint8_t));
    xTaskCreate(managerTask, "Manager", 1024, NULL, 2, NULL);

    // ✅ Chỉ ghi hardcode nếu EEPROM chưa có dữ liệu hợp lệ
    // (signature sẽ không hợp lệ khi EEPROM lần đầu trắng)
    // Việc nạp thông tin thiết bị nên thực hiện qua Factory Setting mode
}
```

Để nạp thông tin riêng cho từng thiết bị, dùng **Factory Setting mode** (giữ 2 nút `PA5 + PB6` lúc boot), thiết bị sẽ chờ nhận JSON qua UART:

```json
{
  "secret_key": "UWLS54",
  "hw_id": "EBXXT5RD34",
  "qr_code": "i=EBXXT5RD34&k=UWLS54"
}
```

---

## 7. Checklist Khi Thêm Giá Trị Mới

- [ ] Thêm `EVENT_UPDATE_XYZ` vào enum `event_update_t` trong `Cloud.h`
- [ ] Thêm `constexpr char XYZ_KEY[] = "..."` vào `Cloud.cpp`
- [ ] Viết 2 hàm: `cloudThingsBoardTelemetryUpdateXYZ()` và `cloudThingsBoardAttributeUpdateXYZ()`
- [ ] Thêm `case EVENT_UPDATE_XYZ:` vào `cloudThingsBoardHandleEventData()`
- [ ] Khai báo prototype 2 hàm mới trong `Cloud.h`
- [ ] Gọi `cloudThingsBoardSendEventData(event)` từ module cảm biến
- [ ] Xác nhận đơn vị và hệ số scaling trước khi chia
