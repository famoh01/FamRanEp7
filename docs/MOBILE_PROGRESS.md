# RAN EP7 Mobile: สถานะความคืบหน้า

อัปเดตล่าสุด: 2026-11-22. อ้างอิง: `docs/EP7_DATA_MANIFEST.md`, `docs/PORT_STATUS.md`

สถานะที่ใช้: **DONE** (ทำแล้วและมีหลักฐานจากการรันจริง), **IN PROGRESS**, **BLOCKED** (ทำต่อไม่ได้เพราะขาดไฟล์หรือสิทธิ์), **MISSING EP7 DATA** (ไม่มีข้อมูลเกมจริงใน source ที่ใช้ได้), **NEXT ACTION**.

**สถานะรวม: ยังไม่ใช่ Full RAN EP7 Mobile.** APK ผ่านการทดสอบ login/ตัวละคร/เข้าโลกแบบ placeholder กับ Server จริง แต่แผนที่ ตัวละคร มอนสเตอร์ NPC ไอเทม สกิล เควส ยังไม่มีข้อมูล EP7 จริง

## DONE (มีหลักฐานจาก CI หรือการรันในเครื่อง)

| งาน | หลักฐาน |
|---|---|
| Server REST API: register, login, logout, token, character list/create/get/delete | `ran_mobile/Server/tests` 44 เทสต์ผ่าน |
| Server combat (ดาเมจคำนวณฝั่ง Server), skill, revive, inventory use, shop, equipment, quest accept | เทสต์ในชุดเดียวกัน |
| `mobile/net_client` (C++ HTTP client ต่อ Server จริง) | `make test` 21 เช็คผ่าน |
| Android: login, register, เลือกตัวละคร, สร้าง, ลบ, ออกจากระบบ, เข้าเล่น (ดึงตัวละครจาก Server) | CI `build-android` run 45047895738 (`20f1d73`): UI flow test 12/12 PASS |
| JNI bridge (`net_bridge.cpp`) ↔ MainActivity (8 native) | ตรวจชื่อ symbol ตรงกันทั้งหมด, `g++ -fsyntax-only` ผ่าน |
| Debug APK (arm64-v8a, x86_64) ติดตั้งและ smoke test บน emulator | CI run 45047895738 |
| HTTPS guard: release build ต้องมี `https://` | `mobile/android/app/build.gradle` |
| BuildConfig.SERVER_URL จากแหล่งเดียว (`-PranServerUrl`, `.env`, ตัวแปรสภาพแวดล้อม) | แก้ใน commit ของรอบนี้ (ดูหัวข้อ CI ด้านล่าง) |

## IN PROGRESS

- **Enter World ต่อ Server ฝั่ง World:** ตอนนี้ "เข้าเล่น" เรียก `GET /api/characters/{id}` แล้วเปิดหน้าเกม แต่ Server ยังไม่มี endpoint สำหรับเข้าโลก (ตำแหน่ง, map_id, สถานะการเชื่อมต่อ) — ยังไม่ได้เพิ่ม
- **Release signing:** ตั้งค่าโค้ดให้อ่าน keystore จาก Gradle property แล้ว แต่ยังไม่มี keystore จริง จึงยังไม่ได้ทดสอบ release APK ที่ลงนาม
- **App icon:** ยังไม่มีไอคอนเฉพาะของแอป (ใช้ไอคอนเริ่มต้นของระบบ)

## BLOCKED

- **Map / Model / Texture / Item / Monster / NPC / Skill / Quest (ข้อมูล EP7 จริง):** source ที่มีข้อมูลเหล่านี้ (`matheomedrana/Ran-Online-EP7-Complete-Files`) ไม่มี license (`license: null`) จึงยังไม่นำไฟล์เข้าแอป ต้องการการยืนยันสิทธิ์จากผู้ใช้ ดูหัวข้อ "ไฟล์ที่ต้องการ" ใน `EP7_DATA_MANIFEST.md`
- **ไฟล์ Google Drive 9 รายการ:** ยังไม่ได้ดาวน์โหลดและตรวจเนื้อหา (ต้องยืนยันสิทธิ์และรายการไฟล์ก่อน) ดู `EP7_DATA_MANIFEST.md`
- **Release APK ที่ลงนาม:** ต้องมี keystore จากเจ้าของโปรเจกต์ (ไม่ใส่รหัสลง Git)
- **Android real device test:** ยังไม่มีการทดสอบบนเครื่องจริงโดยตรงจากผู้ใช้ มีเฉพาะ emulator ใน CI

## MISSING EP7 GAME DATA

- **Map:** ไม่มีแผนที่ EP7 ที่ใช้ได้ (แอปใช้พื้นทดสอบที่สร้างเอง ไม่ใช่ EP7)
- **Model:** ไม่มีโมเดลตัวละคร/มอนสเตอร์/ไอเทมที่ใช้ได้
- **Texture:** ไม่มี texture เกมที่ใช้ได้ (ไฟล์ BMP ใน repo เป็น UI ของเครื่องมือ)
- **Item:** ไม่มีตารางไอเทม EP7 (ไอเทม 5 รายการในฐานข้อมูลเป็นของโปรเจกต์นี้เอง)
- **Monster:** ไม่มีข้อมูลมอนสเตอร์ EP7 (มอนสเตอร์ 5 ตัวใน seed เป็นของโปรเจกต์นี้เอง)
- **NPC:** ไม่มีข้อมูล NPC ใด ๆ ทั้งใน schema และ seed
- **Skill:** ไม่มีข้อมูลสกิล EP7 (สกิล 4 ตัวใน seed เป็นของโปรเจกต์นี้เอง)
- **Quest:** ไม่มีข้อมูลเควสต์ EP7 (เควส 4 รายการใน seed เป็นของโปรเจกต์นี้เอง)
- **Database:** ไม่มีฐานข้อมูลเกม EP7 ที่ใช้ได้ (มี `.bak`/`.sql` ใน source ที่ไม่มี license)

## สถานะการเชื่อมต่อกับ Server (สิ่งที่ยังไม่ต่อจริง)

| ระบบ | Server มี | Android ต่อจริง | หมายเหตุ |
|---|---|---|---|
| Login/Character | มี | มี | ผ่าน CI |
| Enter World | ไม่มี endpoint เฉพาะ | ดึงตัวละคร | ยังไม่มีตำแหน่ง/map/สถานะการเชื่อมต่อจาก Server |
| Movement | ไม่มี | ไม่ต่อ (เคลื่อนที่ในเครื่อง) | ต้องทำ endpoint + ตรวจระยะฝั่ง Server |
| Map | ไม่มี | ไม่ต่อ (พื้นทดสอบในโค้ด) | รอข้อมูล EP7 |
| Monster / Combat | มี (ตี/ดาเมจ) | ไม่ต่อ (ดาเมจจำลองในเครื่อง) | ต้องเรียก `combat/attack` |
| Skill | มี (4 สกิลที่โปรเจกต์สร้าง) | ไม่ต่อ (ปุ่มจำลอง) | ต้องเรียก `combat/skill` |
| Inventory / Potion | มี | ไม่ต่อ (ถุงจำลอง) | ต้องเรียก `inventory/use` |
| Quest | มี (accept) | ไม่ต่อ | ต้องเรียก `quests/{id}/accept` และแสดงสถานะ |
| NPC | ไม่มี | ไม่มี | MISSING EP7 DATA |
| Save/Load ตำแหน่ง | ไม่มี | ไม่มี | HP/MP/EXP/level บันทึกใน DB อยู่แล้ว |

## NEXT ACTION

1. เพิ่ม endpoint เข้าโลก/ตำแหน่ง/เคลื่อนที่บน Server (`enter`, `move`, `leave`) พร้อม migration ของคอลัมน์ตำแหน่ง และเทสต์ฝั่ง Server ที่ตรวจระยะ/ขอบเขต
2. ต่อปุ่มตี สกิล ยาใน Android กับ endpoint ที่มีอยู่ (`combat/attack`, `combat/skill`, `inventory/use`) แทนการจำลองในเครื่อง
3. ผู้ใช้ยืนยันสิทธิ์ของ source ที่มีข้อมูล EP7 หรือส่งไฟล์ที่เป็นของตนเอง แล้วจึงเริ่ม Parser/Validator/Converter ตาม `docs/DATA_CONVERTER.md`
4. เตรียม keystore สำหรับ release (ดู `docs/ANDROID_BUILD.md`)
