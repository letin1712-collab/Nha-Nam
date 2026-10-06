# Hướng dẫn đăng nhập CoreIoT và gửi dữ liệu NHA-NAM-V2

Địa chỉ hệ thống: <https://app.coreiot.io>

Máy đo gửi 4 chỉ số lên CoreIoT khoảng 10 giây một lần:

| Tên (key) | Ý nghĩa | Đơn vị |
|---|---|---|
| `co2_ppm` | Nồng độ CO₂ | ppm |
| `temp_c` | Nhiệt độ | °C |
| `humidity_percent` | Độ ẩm | % |
| `light_lux` | Ánh sáng | lux |

---

## Phần 1. Đăng nhập CoreIoT

Mỗi người đăng nhập bằng tài khoản cá nhân của mình.

**Lần đầu**
1. Mở <https://app.coreiot.io>. Nếu màn hình đăng nhập có nút tạo tài khoản / đăng ký (Sign up), bạn có thể tự đăng ký bằng email của mình.
2. Nếu không có nút đó, đưa email của bạn cho admin để được tạo tài khoản: `[Tên admin / số Zalo]`. Admin thêm ở menu **Users**.
3. Mở hộp thư, tìm thư từ CoreIoT (xem cả mục Spam), bấm liên kết kích hoạt và đặt mật khẩu.

**Các lần sau**
1. Mở <https://app.coreiot.io> bằng Chrome hoặc Edge.
2. Nhập email và mật khẩu, bấm đăng nhập.

Quên mật khẩu: chọn quên mật khẩu ở màn hình đăng nhập, hoặc nhờ admin đặt lại.

---

## Phần 2. Tạo device và lấy Access Token

> Bước này làm một lần cho mỗi máy đo. Mỗi máy dùng **một token riêng**.

1. Trong menu trái, vào **Entities > Devices**.
2. Bấm dấu **+** để thêm thiết bị, đặt tên rõ ràng, ví dụ `NHA-NAM-V2-001`, rồi lưu.
3. Bấm vào thiết bị vừa tạo, khung **Device details** hiện ra bên phải.
4. Bấm **Copy access token**. Token đã nằm trong clipboard.

**Lưu ý bảo mật**
- Token giống mật khẩu của máy. Không gửi vào nhóm chat chung, không chụp màn hình có token.
- Không commit token thật lên git hay nơi công khai.
- Nghi ngờ lộ token: vào **Manage credentials** của thiết bị để tạo token mới, rồi nạp lại firmware.

---

## Phần 3. Test gửi dữ liệu thử (chưa cần firmware)

Làm bước này trước để chắc chắn token và mạng đều đúng. Thay `TOKEN` bằng token vừa copy.

**Cách A: PowerShell**
```powershell
Invoke-RestMethod -Method Post `
  -Uri "https://app.coreiot.io/api/v1/TOKEN/telemetry" `
  -ContentType "application/json" `
  -Body '{"temp_c":28.5,"humidity_percent":65,"co2_ppm":800,"light_lux":120}'
```
Gửi thành công thường không in gì ra.

**Cách B: Python qua MQTT** (giống cách firmware gửi)
```bash
pip install paho-mqtt
```
```python
import json
import paho.mqtt.publish as publish

TOKEN = "DAN_TOKEN_O_DAY"
publish.single(
    "v1/devices/me/telemetry",
    json.dumps({"temp_c": 28.5, "humidity_percent": 65, "co2_ppm": 800, "light_lux": 120}),
    hostname="app.coreiot.io", port=1883,
    auth={"username": TOKEN}, qos=1,
)
print("Da gui")
```

**Kiểm tra trên web:** vào **Entities > Devices**, bấm vào `NHA-NAM-V2`, mở tab **Latest telemetry**. Thấy 4 key với đúng giá trị vừa gửi là thành công. Nếu bảng trống, bấm làm mới.

---

## Phần 4. Gửi dữ liệu thật từ firmware

Project đã được sửa sẵn để nói chuyện với CoreIoT. Bạn chỉ cần dán token và nạp lại.

### 4.1. Dán token vào code

Mở `include/thingsboard_client.h`, tìm dòng:

```cpp
#define COREIOT_TOKEN   "DAN_ACCESS_TOKEN_O_DAY"
```

Thay bằng token của máy. Các dòng bên trên phải đúng như sau:

```cpp
#define TB_SERVER       "app.coreiot.io"
#define TB_PORT         1883
```

### 4.2. Những thay đổi đã có trong code

- `TB_SERVER` đổi từ `tb1.fuvitech.vn` sang `app.coreiot.io`.
- Token lấy trực tiếp từ `COREIOT_TOKEN` khi khởi động.
- `tb_claim()` trả về `false` ngay, để việc giữ BOOT 3 giây không gọi `gw.fuvitech.vn` và ghi đè token.
- Khi CoreIoT từ chối token, code chỉ in cảnh báo, không xóa token nữa.
- 4 key gửi lên giữ nguyên: `temp_c`, `humidity_percent`, `co2_ppm`, `light_lux`.
- Máy vẫn gửi song song lên `mqtt.fuvitech.vn`, nên bảo hành không bị ảnh hưởng.

### 4.3. Build và nạp

```bash
pio run -t upload
pio device monitor
```

### 4.4. Đọc log Serial

Log bình thường:

```
[TB] CoreIoT token: xxxxxx...
[TB] Connecting to app.coreiot.io:1883 ...
[TB] MQTT connected!
[TB] Telemetry OK: {"temp_c":...,"humidity_percent":...,"co2_ppm":...,"light_lux":...}
```

Nếu thấy `rc=5` là token sai: kiểm tra có thừa dấu cách hoặc dán thiếu ký tự không.

### 4.5. Xem dữ liệu trên web

1. Đăng nhập CoreIoT.
2. **Entities > Devices**, bấm vào thiết bị.
3. Mở tab **Latest telemetry**. Dữ liệu cập nhật khoảng 10 giây một lần.

---

## Xử lý lỗi thường gặp

| Hiện tượng | Cách xử lý |
|---|---|
| Không đăng nhập được | Kiểm tra email, Caps Lock. Dùng quên mật khẩu hoặc nhờ admin. |
| Gửi thử báo 401 hoặc not authorized | Token dán sai hoặc thừa ký tự. Copy lại token. |
| Không kết nối được broker | Mạng chặn cổng 1883. Thử mạng khác hoặc hotspot điện thoại. |
| Máy báo `rc=5` | Sai token, sửa `COREIOT_TOKEN` rồi nạp lại. |
| Máy báo `Connecting...` rồi thất bại liên tục | Máy chưa có WiFi hoặc không ra được internet. Kiểm tra WiFi trên màn hình máy. |
| Tab Latest telemetry trống | Bấm làm mới. Kiểm tra máy đang bật và có WiFi. |
| Số liệu không đổi sau vài phút | Xem thời gian cập nhật ở tab Latest telemetry. Máy có thể đã mất WiFi hoặc nguồn. |
| Ra cảnh báo `_loadTokenFromNVS defined but not used` khi build | Chỉ là cảnh báo, không phải lỗi, có thể bỏ qua. |


