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

1. สร้าง keystore นอกโปรเจกต์ เช่น
   `keytool -genkeypair -v -keystore ~/ran-release.jks -keyalg RSA -keysize 2048 -validity 10000 -alias ranmobile`
2. เพิ่มใน `~/.gradle/gradle.properties` (ไม่ใช่ใน repo):
   ```
   ranKeystoreFile=/home/<user>/ran-release.jks
   ranKeystorePassword=<รหัส>
   ranKeyAlias=ranmobile
   ranKeyPassword=<รหัส>
   ```
3. สำรอง keystore ไว้นอกเครื่อง ถ้าหายจะอัปเดตแอปเดิมไม่ได้

ห้าม commit ไฟล์ `.jks`, `.keystore`, `gradle.properties` ที่มีรหัส หรือ `.env`

## 4. ตรวจก่อนส่ง (ทำในเครื่องหรือ CI)

- เทสต์ Server: `cd ran_mobile/Server && python3 -m unittest discover -s tests`
- เทสต์ net_client: `make -C mobile/net_client test`
- CI: `.github/workflows/build-android.yml` (build debug, emulator smoke test, UI flow test)

## 5. ข้อจำกัดที่ยังมี

- ยังไม่มีไอคอนเฉพาะของแอป (ไอคอนเริ่มต้น)
- ยังไม่มีไฟล์ keystore จริง จึงยังไม่ได้ทดสอบ release APK ที่ลงนาม
- การทดสอบ CI ใช้ emulator ไม่ใช่เครื่องจริง
