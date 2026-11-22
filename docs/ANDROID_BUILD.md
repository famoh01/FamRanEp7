# การ Build และติดตั้ง APK (RAN EP7 Mobile Android)

ที่อยู่โค้ด: `mobile/android/` (Gradle, CMake, NDK 26.1.10909125, compileSdk/targetSdk 34, minSdk 26, ABI arm64-v8a และ x86_64)

## 1. ตั้งค่าที่อยู่เซิร์ฟเวอร์ (ไม่ใส่ในซอร์ส)

ที่อยู่ของ RAN MOBILE Server ถูกส่งเข้า `BuildConfig.SERVER_URL` ตอน build ตามลำดับนี้ (ตัวแรกที่มีค่าชนะ):

1. `-PranServerUrl=...` บนคำสั่ง gradle
2. ไฟล์ `mobile/android/.env` บรรทัด `RAN_SERVER_URL=...` (ไฟล์นี้ถูก `.gitignore` ไว้ ไม่ขึ้น Git)
3. ตัวแปรสภาพแวดล้อม `RAN_SERVER_URL`
4. `-PranApiBaseUrl=...` (ชื่อเก่า ยังใช้ได้เพื่อความเข้ากันได้)

คัดลอกเทมเพลต: `cp mobile/android/.env.example mobile/android/.env` แล้วแก้ค่า

## 2. Debug APK

```bash
gradle -p mobile/android assembleDebug -PranServerUrl=http://10.0.2.2:8080
```

- `10.0.2.2` คือ host ของเครื่องที่รัน Server เมื่อใช้ Android emulator
- Debug อนุญาต HTTP เฉพาะ `10.0.2.2`, `127.0.0.1`, `localhost` (ตาม `app/src/debug/res/xml/network_security_config.xml`)
- ผลลัพธ์: `mobile/android/app/build/outputs/apk/debug/app-debug.apk`

## 3. Release APK (ต้องเป็น HTTPS)

```bash
gradle -p mobile/android assembleRelease -PranServerUrl=https://ran-mobile.example.com
```

- ถ้าไม่ใช่ `https://` งาน build จะล้มเหลวโดยตั้งใจ
- Release ไม่อนุญาต cleartext (`app/src/main/res/xml/network_security_config.xml`) ใช้ system trust store
- ถ้าไม่ได้ตั้ง keystore ผลลัพธ์จะเป็น APK ที่ยังไม่ได้ลงนาม (ติดตั้งจริงไม่ได้)

### การตั้งค่า keystore (ทำครั้งเดียว ไม่ใส่รหัสในโปรเจกต์)

ค่าถูกอ่านตามลำดับ (ตัวแรกที่มีค่าชนะ):

1. `-PranKeystoreFile=... -PranKeystorePassword=... -PranKeyAlias=... -PranKeyPassword=...` บนคำสั่ง gradle
2. ไฟล์ `mobile/android/keystore.properties` (ถูก `.gitignore` ไว้) — คัดลอกจาก `keystore.properties.example`
3. `~/.gradle/gradle.properties` ของเครื่องผู้พัฒนา

สำหรับ build ทดสอบ มีสคริปต์สร้างคีย์ชั่วคราวให้ (ไฟล์คีย์และรหัสไม่ขึ้น Git):

```bash
mobile/android/scripts/make_test_keystore.sh        # เขียน keystore/ + keystore.properties ให้
gradle -p mobile/android assembleRelease -PranServerUrl=https://ran-mobile.example.com
# ผลลัพธ์: mobile/android/app/build/outputs/apk/release/app-release.apk (ลงนามแล้ว)
```

สำหรับ release จริง ให้สร้างคีย์ของตัวเองแล้วสำรองไว้ (ถ้าคีย์หายจะอัปเดตแอปเดิมไม่ได้):

```bash
keytool -genkeypair -v -keystore ~/ran-release.jks -keyalg RSA -keysize 2048 -validity 10000 -alias ranmobile
```

ห้าม commit ไฟล์ `.jks`, `.keystore`, `keystore.properties` ที่มีรหัส หรือ `.env` (มี `.gitignore` กันไว้แล้ว)

## 4. ตรวจก่อนส่ง (ทำในเครื่องหรือ CI)

- เทสต์ Server: `cd ran_mobile/Server && python3 -m unittest discover -s tests`
- เทสต์ net_client: `make -C mobile/net_client test`
- CI: `.github/workflows/build-android.yml` (build debug + release ที่ลงนาม, emulator smoke test, UI flow test, reconnect test, combat test)

## 5. ไอคอนแอป

ไอคอนถูกสร้างด้วยสคริปต์ (ทุกเส้นวาดด้วยโค้ด ไม่มีไฟล์จากเกมต้นฉบับ):

```bash
python3 mobile/android/tools/make_icon.py     # ต้องมี Pillow
```

ได้ไฟล์:
- `app/src/main/res/drawable/ic_launcher_background.xml`, `ic_launcher_foreground.xml` (vector)
- `app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`, `ic_launcher_round.xml` (adaptive icon, API 26+)
- `app/src/main/res/mipmap-{m,h,xh,xxh,xxxh}dpi/ic_launcher.png` (48–192 px สำหรับ API ต่ำกว่า 26)
- ภาพตัวอย่างสำหรับเอกสาร: `docs/assets/app_icon_preview.png`

`AndroidManifest.xml` ตั้ง `android:icon` และ `android:roundIcon` ไว้แล้ว

## 6. ข้อจำกัดที่ยังมี

- การทดสอบ CI ใช้ emulator ไม่ใช่เครื่องจริง
- คีย์ที่ CI ใช้เป็นคีย์ชั่วคราว (สร้างใหม่ทุกครั้ง) จึงติดตั้งทับเวอร์ชันที่ลงนามด้วยคีย์อื่นไม่ได้
- Release APK ใน CI ตั้ง `-PranServerUrl=https://example.invalid` เพื่อทดสอบการ build/ลงนามเท่านั้น
