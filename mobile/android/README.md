# Ran Ai Ep7 - โปรเจกต์ Android (โค้ดของเราเอง)

โครงแอป Android ที่รันด้วย NativeActivity + OpenGL ES 3.0 (C++17) วาดหน้าจอสัมผัส (จอยสติ๊กซ้ายล่าง ปุ่มโจมตีขวาล่าง แถวสกิล 5 ช่อง ปุ่มวิ่ง/เดิน ปุ่มเมนู ตัวละคร สกิล กระเป๋า และปุ่มยาพร้อมยาอัตโนมัติที่ตั้งเปอร์เซ็นต์ได้) และตัวละครจำลองที่เดินตามจอยสติ๊ก โค้ด HUD มาจาก `mobile/touch_hud/` ของโปรเจกต์นี้ ไม่มีโค้ดหรือไฟล์ของเกมต้นฉบับ

## สถานะ
- build APK ใน CI: `.github/workflows/build-android.yml` (ทุกครั้งที่ push ที่แตะ `mobile/android`,
  `mobile/touch_hud`, `mobile/net_client`, `mobile/assets` หรือ `ran_mobile/Server`) — CI รันเทสต์บน host
  (`make -C mobile/net_client test`, `make -C mobile/assets test`) ก่อน build และมี smoke test
  ของ reconnect/วงจรชีวิตหลัง build
- ยังไม่มีข้อมูลเกม (ตัวละคร ไอเทม แผนที่) ข้อมูลจะมาจากเซิร์ฟเวอร์ RAN MOBILE ของเราในขั้นถัดไป
- ยังไม่ได้ทดสอบบนมือถือจริง (ทดสอบ build ผ่าน CI เท่านั้น)

## build และดาวน์โหลด APK
1. เปิดแท็บ **Actions** ของรีโป เลือก workflow `build-android` แล้วเปิดรันล่าสุดที่สำเร็จ
2. ดาวน์โหลด artifact `ai-ep7-debug-apk` (ไฟล์ `app-debug.apk`)
3. ติดตั้งบนมือถือ: เปิด "ติดตั้งแอปที่ไม่รู้จัก" ในบราวเซอร์ที่ใช้ดาวน์โหลด แล้วแตะไฟล์ APK

build บนเครื่องที่มี JDK 17, Android SDK (platform 34, build-tools 34.0.0, CMake 3.22.1, NDK 26.1.10909125) และ Gradle 8.7:
```
gradle -p mobile/android assembleDebug
```

## ตัวอักษรบนปุ่มและหน้าต่าง
ข้อความไทยถูกเรนเดอร์ล่วงหน้าเป็นไฟล์ `app/src/main/assets/labels.bin` ด้วยสคริปต์ `tools/make_labels.py` (ใช้ HarfBuzz จัดรูปคำ และ FreeType วาดตัวอักษร) ฟอนต์คือ Noto Sans Thai ใช้ตามสัญญาอนุญาต SIL OFL 1.1 (`tools/fonts/`) ถ้าแก้รายการข้อความ ให้รันใหม่:
```
pip install uharfbuzz freetype-py
python3 mobile/android/tools/make_labels.py
```
ลำดับข้อความใน `LABELS` ของสคริปต์ต้องตรงกับ `enum LabelId` ใน `main.cpp`

## ภาพ placeholder ที่ตรวจสอบได้ (mobile/assets/)
แอปโหลด `app/src/main/assets/assets_manifest.bin` แล้วโหลดเฉพาะไฟล์ที่ระบุไว้ และตรวจขนาด + checksum
(FNV-1a 32) ก่อนใช้ ไฟล์ภาพทั้ง 4 ไฟล์ (`ph_ground.bin`, `ph_block.bin`, `ph_player.bin`, `ph_ui_frame.bin`)
สร้างโดยสคริปต์ของโปรเจกต์เอง (คลีนรูม ไม่มีข้อมูลเกมต้นฉบับ) ถ้าไฟล์หาย/ไม่ตรง แอปยังทำงานได้
(วาดพื้นและกล่องด้วยรูปทรงจากโค้ด) และ log `assets: <ชื่อ>: <สถานะ>` ไว้
สร้าง/ตรวจใหม่ + รันเทสต์:
```
python3 mobile/assets/make_placeholder_assets.py
make -C mobile/assets test
```
รายละเอียดรูปแบบ manifest และไฟล์ PHP1: `docs/ASSETS.md`

## ทดสอบการเชื่อมต่อใหม่บนอีมูเลเตอร์ (debug เท่านั้น)
```
adb shell am start -n com.aiep7.app/.MainActivity \
  --es ci_user ci_smoke --es ci_pass CiSmokeTest2026 --ez ci_reconnect true
```
แอปจะเข้าสู่โลก แล้วเรียก `POST /world/leave` (ฝั่งเซิร์ฟเวอร์เท่านั้น) หลัง 6 วินาที คำขอเดินถัดไปจึงได้
`409 not_in_world` จริง จากนั้นเห็นใน logcat:
```
sync event session_lost
sync event resumed (resumes=1 ...)
```
กด HOME แล้วกลับเข้าแอปจะเห็น `world sync paused` / `world sync resumed` CI (`build-android`) ตรวจลำดับนี้ให้

## Termux บนมือถือ
Termux ใช้รันเซิร์ฟเวอร์ RAN MOBILE (`ran_mobile/Server`) ได้ แต่ **ติดตั้งหรือรัน APK ไม่ได้** ให้ติดตั้ง APK ด้วยวิธีด้านบนแทน

## ข้อจำกัดที่รู้แล้ว
- ปุ่มทั้งหมดเป็นตัวจำลอง: โจมตีแค่ขึ้นแฟลช และกดสกิลแค่พิมพ์ log
- หน้าต่างสกิลมีสกิลตัวอย่าง 6 อัน ลากไปวางบนช่องด่วนได้ (ซ่อนหน้าต่างระหว่างลาก) ยังไม่มีสกิลจริงจากเซิร์ฟเวอร์
- เลือดและการโดนตีเป็นค่าจำลอง (โดนตี 15% ทุก 6 วินาที ยาคืน 40%) ยาในกระเป๋าเริ่มที่ 12 ขวด ดื่มจากปุ่มยา ยาอัตโนมัติ หรือแตะช่องยาในกระเป๋า แล้วจำนวนจะลดลง ค่าการตั้งยาอัตโนมัติและจำนวนยาไม่ถูกบันทึกเมื่อปิดแอป
- แผนที่ทดสอบเป็นพื้นตารางพร้อมกล่องสิ่งกีดขวาง 3 กล่อง ตัวละครเดินชนได้ (ไม่ใช่แผนที่ของเกมต้นฉบับ)
- หน้าต่างตัวละครมีแถบเลือดและช่องอาวุธ/เกราะว่าง 2 ช่อง (ยังไม่มีอุปกรณ์จริงจากเซิร์ฟเวอร์)
- ปุ่มคลับอยู่ในหน้าต่างเมนู (กดในเมนูแล้วเปิดหน้าต่างคลับ) หน้าต่างคลับยังว่างเปล่า กระเป๋ามีไอเทมตัวอย่างที่ยังใช้ไม่ได้ (ยกเว้นยา)
- ใช้ landscape เท่านั้น
- รองรับ arm64-v8a และ x86_64

## เชื่อมเซิร์ฟเวอร์จริง (Phase 1–2)

- เปิดแอปจะมีหน้าต่าง **เข้าสู่ระบบ** (สมัครบัญชีได้) แล้วไป **เลือกตัวละคร** / **สร้างตัวละคร** ทุกการเรียกเซิร์ฟเวอร์ผ่าน `mobile/net_client` (C++) และ `NetBridge.java` (HttpURLConnection)
- ที่อยู่เซิร์ฟเวอร์ (`BuildConfig.SERVER_URL`) ตั้งตอน build ไม่ได้ใส่ในซอร์ส ระบุได้ 3 ทาง (ตัวแรกที่เจอชนะ):
  1. `-PranServerUrl=...` บนคำสั่ง gradle
  2. ไฟล์ `mobile/android/.env` บรรทัด `RAN_SERVER_URL=...` (ไม่ขึ้น Git; คัดลอกจาก `.env.example`)
  3. ตัวแปรสภาพแวดล้อม `RAN_SERVER_URL`
  - debug: `gradle -p mobile/android assembleDebug -PranServerUrl=http://10.0.2.2:8080` (อีมูเลเตอร์ → เครื่อง host, HTTP ได้เฉพาะ debug และ loopback)
  - release: `gradle -p mobile/android assembleRelease -PranServerUrl=https://<โฮสต์>` (ต้องเป็น https ไม่งั้น build ล้มเหลว)
  - รายละเอียดทั้งหมด (keystore, ติดตั้ง, ทดสอบบนมือถือ): `docs/ANDROID_BUILD.md`
- `network_security_config.xml`: release ไม่อนุญาต cleartext; debug อนุญาตเฉพาะ `10.0.2.2`, `127.0.0.1`, `localhost`
- ทดสอบอัตโนมัติ (debug เท่านั้น): เปิดแอปด้วย `--es ci_user <ชื่อ> --es ci_pass <รหัส>` จะล็อกอินโดยไม่มีหน้าต่าง CI ใช้วิธีนี้ใน smoke test
- Flow จริง (Phase 2): เปิดแอป → เข้าสู่ระบบ/สมัคร → เลือกตัวละคร (ถ้าไม่มีตัวละครจะเปิดหน้าสร้างเอง) → สร้าง/ลบตัวละคร → **เข้าเล่น** (Enter World ดึงตัวละครจากเซิร์ฟเวอร์) → Back เปิดเมนู: กลับหน้าตัวละคร หรือ **ออกจากระบบ**
- HUD และแผนที่ถูกวาดหลังเข้าเล่น (`InWorld()`) ไม่วาดตอนอยู่ในหน้าล็อกอิน
- โลกเกมซิงก์กับเซิร์ฟเวอร์: เข้าเล่นแล้วแอปใช้ตำแหน่งเริ่มต้นและสิ่งกีดขวางจากเซิร์ฟเวอร์ (ไม่ใช่แผนที่ทดสอบในแอป) ตำแหน่งตัวละครส่งไปเซิร์ฟเวอร์ราว 10 ครั้งต่อวินาที เซิร์ฟเวอร์ตรวจความเร็วและสิ่งกีดขวาง ถ้าปฏิเสธ ตัวละครจะถูกดึงกลับไปตำแหน่งที่เซิร์ฟเวอร์ยอมรับ ตอนออกจากโลก (เมนู → กลับหน้าตัวละคร) เซิร์ฟเวอร์บันทึกตำแหน่งไว้ และครั้งหน้าเข้าเล่นจะเริ่มจากตำแหน่งนั้น
- กติกาการเดินและการซิงก์อยู่ที่ `mobile/net_client/world_state.{h,cpp}` ทดสอบบนเครื่องด้วย `make -C mobile/net_client test`
- UI test (debug เท่านั้น): `MainActivity` เปิดด้วย `--ez ci_ui_test true` จะรัน `CiUiTest.java` กดปุ่มและกรอกช่องจริงใน dialog 12 ขั้น (validation, login failed, login success, character empty, create, list, select, logout, login again, delete open/confirm/done) และ log `CI_TEST <ชื่อ> PASS|FAIL` ผล CI ตรวจใน job `build-android`
- ยังไม่ได้ทำ: หน้าจอเกมที่เข้าแผนที่จริง (ต้องมีข้อมูลแผนที่จาก source ที่มีสิทธิ์ใช้ ดู `docs/PORT_STATUS.md`)
