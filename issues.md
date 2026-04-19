# Issues

1. `connectSecretKey` hiện chưa được dùng trong `WifiDevice`/`EthernetDevice`; các nhánh xác thực MQTT/OTA đang so với `expectedSecret` hard-code `"123456"`.
2. Cấu hình Wi-Fi lưu password dưới key `"password"` nhưng lúc load lại đọc key `"pass"`, nên password cập nhật qua `setting/set` không được dùng sau restart.
3. Retry MQTT chưa đúng backoff mong muốn; khi connect fail thiết bị có thể thử lại liên tục và bị block thay vì chờ đủ khoảng thời gian retry.
4. `EthernetDevice::updateHostIp()` lưu IP của host trả lời request thay vì giá trị `payload.ip` từ API, nên có thể lưu sai địa chỉ broker.
5. `loadConfig()` có thể ghi đè giá trị mặc định của `zone`, `domain`, `log` thành chuỗi rỗng nếu Preferences chưa có dữ liệu.
