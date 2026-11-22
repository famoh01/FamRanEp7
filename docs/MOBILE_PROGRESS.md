# RAN EP7 Mobile: สถานะความคืบหน้า

อัปเดตล่าสุด: 2026-11-22 (รอบ Phase 3: ความปลอดภัยของโลกเกม, การเชื่อมต่อใหม่, วงจรชีวิตแอป). อ้างอิง: `docs/EP7_DATA_MANIFEST.md`, `docs/PORT_STATUS.md`

สถานะที่ใช้: **DONE** (ทำแล้วและมีหลักฐานจากการรันจริง), **IN PROGRESS**, **BLOCKED** (ทำต่อไม่ได้เพราะขาดไฟล์หรือสิทธิ์), **MISSING EP7 DATA** (ไม่มีข้อมูลเกมจริงใน source ที่ใช้ได้), **NEXT ACTION**.

**สถานะรวม: ยังไม่ใช่ Full RAN EP7 Mobile.** APK ผ่านการทดสอบ login/ตัวละคร/เข้าโลกแบบ placeholder กับ Server จริง แต่แผนที่ ตัวละคร มอนสเตอร์ NPC ไอเทม สกิล เควส ยังไม่มีข้อมูล EP7 จริง

## DONE (มีหลักฐานจาก CI หรือการรันในเครื่อง)

| งาน | หลักฐาน |
|---|---|
| Server REST API: register, login, logout, token, character list/create/get/delete | `ran_mobile/Server/tests` 94 เทสต์ผ่าน (รวมเทสต์โลกเกมและความปลอดภัย) |
| Server โลกเกม: เข้า/ออก/เดินตามกติกา (ระยะ, สิ่งกีดขวาง, ขอบเขต), เชื่อมต่อใหม่, เก็บตำแหน่งข้ามการออกจากระบบ, migration ย้อนกลับได้ | เทสต์ใน `ran_mobile/Server/tests/test_world.py` |
| Android เข้าโลกจริง: ใช้ตำแหน่งเริ่มต้นและสิ่งกีดขวางจาก Server, ส่งตำแหน่งเดิน, ดึงกลับเมื่อถูกปฏิเสธ, ออกจากโลกตอนกลับหน้าตัวละคร | CI `build-android` `c8bca3f`: UI 12/12 PASS, smoke: จอยสติ๊กทำให้ตัวละครเดิน 159 px ในโลก |
| `mobile/net_client` world (ตรรกะซิงก์ทดสอบบนเครื่อง) | `make -C mobile/net_client test`: 106 เช็ค (เทสต์กับ Server จริง + `world_state`, `world_map`, การแปลงคำตอบ, การดึงกลับ, การเชื่อมต่อใหม่ด้วย backoff, การแมป 409/401) |
| Server ความปลอดภัยของโลกเกม: เซสชันหมดอายุเมื่อไม่มีการติดต่อ 5 นาที (ปรับได้ด้วย `--world-idle-s`), เซสชันจบเมื่อโทเคนหมดอายุ, ถังความเร็วการเดิน (เดินข้ามแผนที่ด้วยคำขอถี่ ๆ ไม่ได้), ตรวจข้อมูลแผนที่ตอนโหลด, migration v2 ย้อนกลับได้ | `tests/test_hardening.py` (28 เทสต์) และ `collision_vectors.txt` ที่ Server กับ Client ใช้ร่วมกัน; ทดสอบ HTTP จริงแล้ว ไม่มี token/รหัสผ่านใน log |
| Server การบันทึก: log แยกหมวด, ปิดบังชื่อและข้อมูลลับ, `python -m ranserver.health` ตรวจฐานข้อมูล | `log.py`, `health.py`, เทสต์ใน `test_hardening.py` |
| Server combat (ดาเมจคำนวณฝั่ง Server), skill, revive, inventory use, shop, equipment, quest accept | เทสต์ในชุดเดียวกัน |
| Android: login, register, เลือกตัวละคร, สร้าง, ลบ, ออกจากระบบ, เข้าเล่น (ดึงตัวละครจาก Server) | CI `build-android` run 45047895738 (`20f1d73`): UI flow test 12/12 PASS |
| JNI bridge (`net_bridge.cpp`) ↔ MainActivity (8 native) | ตรวจชื่อ symbol ตรงกันทั้งหมด, `g++ -fsyntax-only` ผ่าน |
| Debug APK (arm64-v8a, x86_64) ติดตั้งและ smoke test บน emulator | CI run 45047895738 |
| HTTPS guard: release build ต้องมี `https://` | `mobile/android/app/build.gradle` |
| BuildConfig.SERVER_URL จากแหล่งเดียว (`-PranServerUrl`, `.env`, ตัวแปรสภาพแวดล้อม) | แก้ใน commit ของรอบนี้ (ดูหัวข้อ CI ด้านล่าง) |

## IN PROGRESS

- **การเชื่อมต่อโลกขาดและกลับเข้าโลก (Android):** โค้ดและเทสต์บนเครื่องผ่านแล้ว (`ResumeWorld` เมื่อ Server ปิดเซสชันหรือ 409 `not_in_world`, รอระหว่างลองใหม่ 0.5 วินาทีถึง 10 วินาที, 401 กลับหน้าล็อกอิน, แจ้งตอนขาดและตอนกลับมา) ยังไม่ผ่าน CI บน APK นี้
- **วงจรชีวิตแอป:** `onPause` หยุดส่งตำแหน่ง, `onResume` เริ่มส่งใหม่ถ้าอยู่ในโลก (ยังไม่ทดสอบบนเครื่องจริง)
- **การเดินใน Android:** ใช้ `world_map` ตัวเดียวกับเทสต์บนเครื่อง (แทนโค้ดซ้ำใน `main.cpp`) ยังไม่ผ่าน CI บน APK นี้
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
| Enter World | มี (`/world/enter`) | มี (ใช้ตำแหน่งและสิ่งกีดขวางจาก Server) | CI `c8bca3f` |
| Movement | มี (`/world/move`, ตรวจระยะและสิ่งกีดขวาง) | มี (ส่งราว 10 ครั้ง/วินาที, ดึงกลับเมื่อถูกปฏิเสธ) | Server ไม่ยอมรับการวาร์ป |
| Map | มีแผนทดสอบ (สร้างเอง ไม่ใช่ EP7) | มี (ใช้บล็อกจาก Server) | รอข้อมูล EP7 |
| Monster / Combat | มี (ตี/ดาเมจ) | ไม่ต่อ (ดาเมจจำลองในเครื่อง) | ต้องเรียก `combat/attack` |
| Skill | มี (4 สกิลที่โปรเจกต์สร้าง) | ไม่ต่อ (ปุ่มจำลอง) | ต้องเรียก `combat/skill` |
| Inventory / Potion | มี | ไม่ต่อ (ถุงจำลอง) | ต้องเรียก `inventory/use` |
| Quest | มี (accept) | ไม่ต่อ | ต้องเรียก `quests/{id}/accept` และแสดงสถานะ |
| NPC | ไม่มี | ไม่มี | MISSING EP7 DATA |
| Save/Load ตำแหน่ง | มี (ตอนออกจากโลกและออกจากระบบ) | มี (เข้าเล่นครั้งถัดไปเริ่มที่จุดเดิม) | HP/MP/EXP/level บันทึกใน DB อยู่แล้ว |

## NEXT ACTION

1. รัน CI ของ `build-android` กับการเปลี่ยนแปลงรอบนี้ (การเชื่อมต่อใหม่, `world_map`, วงจรชีวิตแอป) และตรวจว่า UI test ยังผ่าน 12/12 และ smoke test ยังเดินได้
2. ต่อปุ่มตี สกิล ยาใน Android กับ endpoint ที่มีอยู่ (`combat/attack`, `combat/skill`, `inventory/use`) แทนการจำลองในเครื่อง
3. ผู้ใช้ยืนยันสิทธิ์ของ source ที่มีข้อมูล EP7 หรือส่งไฟล์ที่เป็นของตนเอง แล้วจึงเริ่ม Parser/Validator/Converter ตาม `docs/DATA_CONVERTER.md`
4. เตรียม keystore สำหรับ release (ดู `docs/ANDROID_BUILD.md`)
