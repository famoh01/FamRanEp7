# RAN EP7 Mobile: สถานะความคืบหน้า

อัปเดตล่าสุด: 2026-11-22 (รอบ Phase 4: เทสต์เชื่อมต่อใหม่/วงจรชีวิตอัตโนมัติ, จำกัดความถี่การเดิน,
ตัวล็อกฐานข้อมูล, ตัวโหลด asset ที่ตรวจสอบได้, นโยบายคลีนรูม). อ้างอิง: `docs/EP7_DATA_MANIFEST.md`,
`docs/PORT_STATUS.md`, `docs/CLEAN_ROOM.md`, `docs/ASSETS.md`

สถานะที่ใช้: **DONE** (ทำแล้วและมีหลักฐานจากการรันจริง), **IN PROGRESS**, **BLOCKED** (ทำต่อไม่ได้เพราะขาดไฟล์หรือสิทธิ์), **MISSING EP7 DATA** (ไม่มีข้อมูลเกมจริงใน source ที่ใช้ได้), **NEXT ACTION**.

**สถานะรวม: ยังไม่ใช่ Full RAN EP7 Mobile.** APK ผ่านการทดสอบ login/ตัวละคร/เข้าโลกแบบ placeholder กับ Server จริง
รวมถึงเส้นทาง **เชื่อมต่อใหม่/วงจรชีวิตแอป** ที่ทดสอบบนอีมูเลเตอร์แล้ว แต่แผนที่ ตัวละคร มอนสเตอร์ NPC ไอเทม สกิล
เควส ยังไม่มีข้อมูล EP7 จริง (ยึดคลีนรูมตาม `docs/CLEAN_ROOM.md`)

### ผล CI ของรอบนี้ (2026-11-22)

| workflow | run | commit | ผล |
|---|---|---|---|
| `ran-mobile-ci` | [45065330912](https://github.com/famoh01/FamRanEp7/actions/runs/45065330912) | `5e92187` | ✅ server-tests (105 เทสต์), client-integration (`make test` + `make test-reconnect`), assets |
| `build-android` | [45065330934](https://github.com/famoh01/FamRanEp7/actions/runs/45065330934) | `5e92187` | ✅ smoke (เดินได้), UI flow 12/12, reconnect/lifecycle + ตำแหน่งเดิมตรงกัน |

## DONE (มีหลักฐานจาก CI หรือการรันในเครื่อง)

| งาน | หลักฐาน |
|---|---|
| Server REST API: register, login, logout, token, character list/create/get/delete | `ran_mobile/Server/tests` **105 เทสต์ผ่าน** (รวมเทสต์โลกเกมและความปลอดภัย) |
| Server โลกเกม: เข้า/ออก/เดินตามกติกา (ระยะ, สิ่งกีดขวาง, ขอบเขต), เชื่อมต่อใหม่, เก็บตำแหน่งข้ามการออกจากระบบ, migration ย้อนกลับได้ | เทสต์ใน `ran_mobile/Server/tests/test_world.py` |
| Android เข้าโลกจริง: ใช้ตำแหน่งเริ่มต้นและสิ่งกีดขวางจาก Server, ส่งตำแหน่งเดิน, ดึงกลับเมื่อถูกปฏิเสธ, ออกจากโลกตอนกลับหน้าตัวละคร | CI `build-android` `c8bca3f`: UI 12/12 PASS, smoke: จอยสติ๊กทำให้ตัวละครเดิน 159 px ในโลก |
| `mobile/net_client` world (ตรรกะซิงก์ทดสอบบนเครื่อง) | `make -C mobile/net_client test`: เทสต์กับ Server จริง + unit test ของ `world_state`, `world_map`, `world_flow` (state machine) — ผ่านทั้งหมด |
| **การเชื่อมต่อใหม่ + วงจรชีวิต บนอุปกรณ์จริง (CI)** | `build-android` `45065330934`: `session_lost=True resumed=True same_position=True app_paused=True app_resumed=True` · `before=0.4206,0.5275 after=0.4206,0.5275` — logcat: `device test: position before expiry 0.4206,0.5275` → `device test: server session ended by /world/leave (position kept)` → `sync event session_lost` → `sync event resumed (resumes=1, failures=0) pos=0.4206,0.5275` → `world sync paused` → `world sync resumed` → `sync event resumed` (ยืนยัน 409 `not_in_world` จริง → `ResumeWorld` → **ตำแหน่งเดิมเป๊ะ**) |
| **การเชื่อมต่อใหม่ + วงจรชีวิต (ทดสอบอัตโนมัติแล้ว)** | `world_flow.{h,cpp}` แยก state machine ออกจาก JNI เพื่อทดสอบได้: `test_world_flow.cpp` (scripted server: session หมด→resume ที่ตำแหน่งเดิม, backoff, 401, pause/resume, 429) และ `make test-reconnect` ที่รันกับ **Server จริง** (`--world-idle-s 2.5`): session หมดอายุจริง → 409 `not_in_world` → `ResumeWorld` → ตำแหน่งเดิมตรงกัน, pause ไม่ส่งคำขอ, 401 กลับหน้าล็อกอิน |
| **จำกัดความถี่คำขอเดิน (Server + Client)** | Server: `world.MIN_MOVE_INTERVAL_MS` = 50 ms, `429 move_too_fast` + `retry_after_ms` (migration v3 `last_move_req_ms`) · Client: `WorldSession::PendingMove(now)` ไม่ส่งถี่กว่า 50 ms และถือ 429 เป็น "ขอลองใหม่" (ไม่ snap, ไม่ถือว่า session หลุด) — มีเทสต์ทั้งฝั่ง Server และ Client |
| **ตัวล็อกฐานข้อมูลแคบลง** | `Api.handle()` ไม่ล็อกทั้งคำขอ: parse/validate/log อยู่นอกล็อก, ล็อกครอบเฉพาะ connection + ธุรกรรม, ตรวจรหัสผ่าน (scrypt) นอกล็อก — เทสต์ `LockScopingTests` (คำขออื่นตอบได้ระหว่างตรวจรหัสผ่าน, JSON ผิดไม่เปิดธุรกรรม, route ไม่รู้จักไม่เปิดธุรกรรม) + เทสต์ยิงพร้อมกัน 8 เธรด |
| **ตัวโหลด asset ที่ตรวจสอบได้ (ขั้น d)** | `mobile/assets/`: manifest parser เข้มงวด (ปฏิเสธ path traversal/ชื่อซ้ำ/checksum ผิดรูป ฯลฯ), ตรวจขนาด+checksum (FNV-1a 32), ไฟล์หาย = `Missing` ไม่ใช่ข้อมูลว่าง; ไฟล์ placeholder สังเคราะห์ 4 ไฟล์สร้างด้วย `make_placeholder_assets.py`; แอปโหลดจริงและ `LOGI("placeholder assets: N loaded...")` (logcat CI: `placeholder assets: 4 loaded, 0 missing, 0 broken (4 listed)`) และวาด texture ที่ผ่านการตรวจ — `make -C mobile/assets test` ผ่าน |
| Server ความปลอดภัยของโลกเกม: เซสชันหมดอายุเมื่อไม่มีการติดต่อ 5 นาที (ปรับได้ด้วย `--world-idle-s`), เซสชันจบเมื่อโทเคนหมดอายุ, ถังความเร็วการเดิน (เดินข้ามแผนที่ด้วยคำขอถี่ ๆ ไม่ได้), ตรวจข้อมูลแผนที่ตอนโหลด, migration v2 ย้อนกลับได้ | `tests/test_hardening.py` (28 เทสต์) และ `collision_vectors.txt` ที่ Server กับ Client ใช้ร่วมกัน; ทดสอบ HTTP จริงแล้ว ไม่มี token/รหัสผ่านใน log |
| Server การบันทึก: log แยกหมวด, ปิดบังชื่อและข้อมูลลับ, `python -m ranserver.health` ตรวจฐานข้อมูล | `log.py`, `health.py`, เทสต์ใน `test_hardening.py` |
| Server combat (ดาเมจคำนวณฝั่ง Server), skill, revive, inventory use, shop, equipment, quest accept | เทสต์ในชุดเดียวกัน |
| Android: login, register, เลือกตัวละคร, สร้าง, ลบ, ออกจากระบบ, เข้าเล่น (ดึงตัวละครจาก Server) | CI `build-android` run 45047895738 (`20f1d73`): UI flow test 12/12 PASS |
| JNI bridge (`net_bridge.cpp`) ↔ MainActivity (8 native) | ตรวจชื่อ symbol ตรงกันทั้งหมด, `g++ -fsyntax-only` ผ่าน |
| Debug APK (arm64-v8a, x86_64) ติดตั้งและ smoke test บน emulator | CI run 45047895738 |
| HTTPS guard: release build ต้องมี `https://` | `mobile/android/app/build.gradle` |
| BuildConfig.SERVER_URL จากแหล่งเดียว (`-PranServerUrl`, `.env`, ตัวแปรสภาพแวดล้อม) | แก้ใน commit ของรอบนี้ (ดูหัวข้อ CI ด้านล่าง) |

## IN PROGRESS

- **Windows/renderer (เฟส 2–3 ของพอร์ต PC):** ไม่อยู่ในขอบเขตงานมือถือรอบนี้ (CI `platform-win32` แยก workflow)
- **Unity Client:** สคริปต์ 19 ไฟล์ยังไม่คอมไพล์ (ไม่มี Unity Editor ในสภาพแวดล้อมนี้)
- **Release signing:** ตั้งค่าโค้ดให้อ่าน keystore จาก Gradle property แล้ว แต่ยังไม่มี keystore จริง
- **App icon:** ยังไม่มีไอคอนเฉพาะของแอป (ใช้ไอคอนเริ่มต้นของระบบ)
- **Release signing:** ตั้งค่าโค้ดให้อ่าน keystore จาก Gradle property แล้ว แต่ยังไม่มี keystore จริง จึงยังไม่ได้ทดสอบ release APK ที่ลงนาม
- **App icon:** ยังไม่มีไอคอนเฉพาะของแอป (ใช้ไอคอนเริ่มต้นของระบบ)

## FIXED ในรอบนี้ (บั๊กที่ CI เปิดโปง)

- **`RejectedExecutionException` ทำแอป crash หลังปิด activity:** `MainActivity.syncTick()` โพสต์งานเข้า
  executor ที่ถูก `shutdownNow()` ไปแล้ว (เห็นใน crash buffer ของ CI) → เพิ่ม `netExecute()` ที่กลืน
  `RejectedExecutionException` และล็อกสถานะ `netClosed`, `scheduleTick()` เช็ค `isFinishing()/isDestroyed()`,
  `onDestroy()` หยุดลูปก่อนปิด executor
- **ลำดับ log ของ reconnect:** การ resume เกิดใน tick เดียวกับที่พบ 409 ทำให้ log เห็นแค่ `resumed` →
  เพิ่ม `WorldSync::Ticks()/ResumesLost()` ให้ชั้น JNI log `session_lost` ก่อน `resumed`
- **เทสต์บนอีมูเลเตอร์ไม่เกิด 409 เพราะผู้เล่นยืนนิ่ง:** เพิ่ม `WorldSession::ForceNextMove()` ให้ยิงคำขอเดิน
  ที่ตำแหน่งเดิม 1 ครั้งหลังจบ session ฝั่ง Server (Server ตัดสินเป็นระยะ 0 จึงตอบ 409 `not_in_world`)
- **เทสต์ texture:** การ์ด `kLoadPlaceholderTextures` เปิดแล้ว (crash เดิมไม่เกี่ยวกับ texture) และ CI เขียว

## DECISION: CLEAN-ROOM (2026-11-22)

โปรเจกต์นี้ยึดคลีนรูมตาม `docs/CLEAN_ROOM.md`: **ไม่นำเข้าไฟล์/คลังข้อมูลของเกมต้นฉบับ ไม่แตกไฟล์ APK
ไม่แตกไฟล์ MP4 และไม่ใส่ GameGuard/Hackshield หรือระบบป้องกันโกงใด ๆ** ภาพ/ข้อความที่แอปใช้สร้างจากสคริปต์
ในรีโป (`labels.bin`, `ph_*.bin`, ปุ่ม HUD) ทั้งหมด ข้อมูลเกมจริงยัง BLOCKED จนกว่าจะมีหลักฐานสิทธิ์

## BLOCKED

- **Map / Model / Texture / Item / Monster / NPC / Skill / Quest (ข้อมูล EP7 จริง):** source ที่มีข้อมูลเหล่านี้ (`matheomedrana/Ran-Online-EP7-Complete-Files`) ไม่มี license (`license: null`) จึงยังไม่นำไฟล์เข้าแอป ต้องการการยืนยันสิทธิ์จากผู้ใช้ ดูหัวข้อ "ไฟล์ที่ต้องการ" ใน `EP7_DATA_MANIFEST.md`
- **ไฟล์ Google Drive 9 รายการ:** ยังไม่ได้ดาวน์โหลดและตรวจเนื้อหา (ต้องยืนยันสิทธิ์และรายการไฟล์ก่อน) ดู `EP7_DATA_MANIFEST.md`
- **Release APK ที่ลงนาม:** ต้องมี keystore จากเจ้าของโปรเจกต์ (ไม่ใส่รหัสลง Git)
- **Android real device test:** ยังไม่มีการทดสอบบนเครื่องจริงโดยตรงจากผู้ใช้ มีเฉพาะ emulator ใน CI

## MISSING EP7 GAME DATA

- **Map:** ไม่มีแผนที่ EP7 ที่ใช้ได้ (แอปใช้พื้นทดสอบที่สร้างเอง + texture placeholder ที่สร้างเอง ไม่ใช่ EP7)
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
| Movement | มี (`/world/move`, ตรวจความถี่+ระยะและสิ่งกีดขวาง) | มี (ส่งราว 10 ครั้ง/วินาที, ดึงกลับเมื่อถูกปฏิเสธ, ไม่ส่งถี่กว่า 50 ms) | Server ไม่ยอมรับการวาร์ป; คำขอถี่เกินได้ 429 และ Client รอแล้วส่งใหม่ |
| Reconnect / Lifecycle | มี (`/world/enter` ซ้ำ = resume, session หมดอายุ 5 นาที ปรับได้) | มี (`WorldSync` + `onPause/onResume`) | ทดสอบอัตโนมัติทั้งบน host และกับ Server จริง; การทดสอบบนอีมูเลเตอร์อยู่ใน CI |
| Map | มีแผนทดสอบ (สร้างเอง ไม่ใช่ EP7) | มี (ใช้บล็อกจาก Server) | รอข้อมูล EP7 |
| Monster / Combat | มี (ตี/ดาเมจ) | ไม่ต่อ (ดาเมจจำลองในเครื่อง) | ต้องเรียก `combat/attack` |
| Skill | มี (4 สกิลที่โปรเจกต์สร้าง) | ไม่ต่อ (ปุ่มจำลอง) | ต้องเรียก `combat/skill` |
| Inventory / Potion | มี | ไม่ต่อ (ถุงจำลอง) | ต้องเรียก `inventory/use` |
| Quest | มี (accept) | ไม่ต่อ | ต้องเรียก `quests/{id}/accept` และแสดงสถานะ |
| NPC | ไม่มี | ไม่มี | MISSING EP7 DATA |
| Save/Load ตำแหน่ง | มี (ตอนออกจากโลกและออกจากระบบ) | มี (เข้าเล่นครั้งถัดไปเริ่มที่จุดเดิม) | HP/MP/EXP/level บันทึกใน DB อยู่แล้ว |

## NEXT ACTION

1. ~~ทดสอบการเชื่อมต่อใหม่จริง~~ เสร็จแล้ว: host unit test, `make test-reconnect` กับ Server จริง และ CI บนอีมูเลเตอร์ (run 45064761735)
2. ต่อปุ่มตี สกิล ยาใน Android กับ endpoint ที่มีอยู่ (`combat/attack`, `combat/skill`, `inventory/use`) แทนการจำลองในเครื่อง
3. ผู้ใช้ยืนยันสิทธิ์ของ source ที่มีข้อมูล EP7 หรือส่งไฟล์ที่เป็นของตนเอง แล้วจึงเริ่ม Parser/Validator/Converter ตาม `docs/DATA_CONVERTER.md`
4. เตรียม keystore สำหรับ release (ดู `docs/ANDROID_BUILD.md`)
5. (เล็ก) ใช้ `ph_ui_frame.bin` วาดกรอบ UI จริง และเพิ่ม asset ประเภท `label` เข้า manifest แทนการอ่าน `labels.bin` แยก
