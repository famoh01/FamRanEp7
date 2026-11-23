# FamRanEp7 - RAN Online EP7 mobile port (clean-room)

ผลงานของโปรเจกต์นี้คือ **พอร์ต/ไคลเอนต์มือถือที่เขียนขึ้นใหม่** ไม่ได้นำไฟล์เกม ข้อมูล หรือระบบป้องกันโกงของ
เกมต้นฉบับมาใช้ (ดู `docs/CLEAN_ROOM.md`)

| ส่วน | ที่อยู่ | สถานะ |
|---|---|---|
| เซิร์ฟเวอร์ RAN MOBILE (Python + SQLite) | `ran_mobile/Server/` | ผ่านเทสต์ 172 รายการ |
| ไคลเอนต์เน็ตเวิร์ก (C++17, ใช้บน Android และ host) | `mobile/net_client/` | ผ่านเทสต์บน host + เทสต์กับเซิร์ฟเวอร์จริง (`make test`, `make test-social`) |
| แอป Android (NativeActivity + GLES 3.0) | `mobile/android/` | build ใน CI, smoke/UI test บนอีมูเลเตอร์ |
| HUD สัมผัส (C++) | `mobile/touch_hud/` | ผ่านเทสต์ |
| ตัวโหลด asset + ภาพ placeholder ที่สร้างเอง | `mobile/assets/` | ผ่านเทสต์ (`make -C mobile/assets test`) |
| พอร์ต PC (platform/renderer) | `platform/`, `renderer/` | ผ่านเทสต์ Linux, Windows ผ่าน CI |

## เริ่มใช้งานเร็ว

```bash
# เซิร์ฟเวอร์
cd ran_mobile/Server && python3 -m ranserver --db /tmp/ran.db --host 0.0.0.0 --port 8080

# เทสต์ทั้งหมดบนเครื่อง (ไม่ต้องมี Android SDK)
python3 -m unittest discover -s ran_mobile/Server/tests -v
make -C mobile/net_client test && make -C mobile/net_client test-social
make -C mobile/touch_hud test
python3 -m unittest discover -s tools/ep7_drive_importer -p "test_*.py" -t tools/ep7_drive_importer
python3 tools/check_jni_surface.py       # native method ฝั่ง Java ↔ JNI ฝั่ง C++
make -C mobile/assets test
make -C platform test && make -C renderer test
```

APK: ดาวน์โหลด artifact `ai-ep7-debug-apk` จาก workflow `build-android` (วิธีติดตั้ง: `mobile/android/README.md`)

## เอกสาร

- `docs/MOBILE_PROGRESS.md` - สถานะงานและหลักฐาน (DONE / IN PROGRESS / BLOCKED)
- `docs/CLEAN_ROOM.md` - สิ่งที่ห้ามนำเข้า และสิ่งที่โปรเจกต์สร้างเอง
- `docs/ASSETS.md` - manifest และการตรวจสอบ asset
- `docs/SERVER_SETUP.md` - รันเซิร์ฟเวอร์, endpoint, การจำกัดความถี่การเดิน
- `docs/EP7_DATA_MANIFEST.md` - ข้อมูลเกมจริงที่ยัง BLOCKED (ไม่มีใบอนุญาต)
