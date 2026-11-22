# สถานะการพอร์ต RAN EP7 → Mobile (2026-11-22)

เอกสารนี้สรุปสถานะจริงของ Workspace (`/home/user/FamRanEp7`, สาขา `arena/d4abd2a7-famranep7`) ตรวจจากไฟล์ที่มีอยู่จริง ไม่ได้สมมติว่ามีไฟล์ใด

## 1. ระบบที่มีอยู่แล้ว

| ส่วน | ที่อยู่ | สถานะ | หมายเหตุ |
|---|---|---|---|
| Server (ของเราเอง) | `ran_mobile/Server/ranserver/` | ใช้งานได้ | Python มาตรฐาน + SQLite, HTTP JSON, Bearer token, 44 เทสต์ผ่าน |
| ฐานข้อมูล | `ran_mobile/Database/schema.sql`, `seed.sql` | ใช้งานได้ | SQLite, 13 ตาราง (accounts, sessions, characters, items, inventory, skills, character_skills, monsters, quests, character_quests, character_equipment, login_failures, request_log) |
| ข้อมูลเริ่มต้น | `ran_mobile/Database/seed.sql` | มีข้อมูลตัวอย่างที่เขียนเอง | มอนสเตอร์ 5, ไอเทม 7, สกิล 4, เควส 4 ไม่ได้คัดลอกจากเกมต้นฉบับ |
| Unity Client | `ran_mobile/Client/Scripts/` | เขียนแล้ว 19 ไฟล์ ยังไม่คอมไพล์ | ไม่มีโปรเจกต์ Unity (ไม่มี `ProjectSettings/`, ไม่มีไฟล์ `.unity`) |
| ปุ่ม HUD ของ Unity | `ran_mobile/Client/Art/` | สร้างด้วยโค้ด 28 ไฟล์ | `tools/generate_buttons.py` |
| Android App | `mobile/android/` | ใช้งานได้บนอีมูเลเตอร์ (CI) | NativeActivity + GLES 3.0, C++17, AGP 8.5.2, compileSdk 34, NDK 26.1.10909125, ABI arm64-v8a และ x86_64, minSdk 26, package `com.aiep7.app`, ชื่อแสดง "Ran Ai Ep7" |
| HUD (C++) | `mobile/touch_hud/` | ทดสอบผ่าน | จอยสติ๊ก ปุ่ม กระเป๋า สกิล ยา เมนู (มีคลับ) |
| ไคลเอนต์ HTTP (C++) | `mobile/net_client/` | ทดสอบผ่านกับเซิร์ฟเวอร์จริง | POSIX socket, ล็อกอิน/สมัคร/ตัวละคร/มอนสเตอร์, CI job `client-integration` ผ่าน |
| Platform และ Renderer | `platform/`, `renderer/` | ทดสอบผ่านบน Linux, Windows ผ่าน CI | เป็นส่วนของพอร์ต PC (เฟส 2–3) |
| ป้ายภาษาไทย | `mobile/android/tools/`, `assets/labels.bin` | ใช้งานได้ | Noto Sans Thai (OFL) |

## 2. ระบบที่ยังขาด

- **Android APK ที่เชื่อมเซิร์ฟเวอร์จริง (Phase 1 เสร็จ, Phase 2 บางส่วน):** แอปเรียก `mobile/net_client` ผ่าน `NetBridge.java` มีหน้าล็อกอิน สมัคร และเลือก/สร้างตัวละคร ทดสอบแล้วใน CI (run 45040009999): ล็อกอิน, สร้างตัวละคร `CiHero` และเลือกบนเซิร์ฟเวอร์จริงผ่าน HTTP debug ยังไม่ได้ทดสอบ HTTPS และยังไม่ได้ทดสอบการพิมพ์ในหน้าล็อกอินด้วยมือ
- **Unity Client:** ไม่มี Unity Editor ในสภาพแวดล้อมนี้ ไม่มีโปรเจกต์ที่ผูกฉาก ปุ่ม และการคอมไพล์
- **ระบบเกมฝั่ง Client:** การเคลื่อนไหวแบบซิงก์กับเซิร์ฟเวอร์ การต่อสู้ สกิล ไอเทม เควส ร้านค้า แชต NPC ยังไม่มี (แอป Android เป็นต้นแบบ HUD ที่มีตัวละครจำลอง)
- **ระบบเกมฝั่ง Server:** Movement, NPC, Chat, Map, Save/Load ตำแหน่งยังไม่มี การต่อสู้และสกิลมีบางส่วน
- **แผนที่:** ไม่มีตารางหรือไฟล์แผนที่บนเซิร์ฟเวอร์ แอป Android มีแผนที่ทดสอบ 1 แผ่นที่สร้างจากโค้ด
- **Migration:** ไม่มี `database/migrations/` ของโปรเจกต์นี้ (ยังไม่มีสคีมาเดิมให้ย้าย)
- **APK Release, การลงนาม, และการทดสอบบนมือถือจริง:** ยังไม่มี APK ที่ผ่านการทดสอบบน Android 12 จริง

## 3. ระบบที่ Build ไม่ผ่าน

- ไม่มีที่ทราบ: APK Debug ผ่าน CI ล่าสุด (run 45032235071, commit `8fb3da7`) เทสต์เซิร์ฟเวอร์ผ่าน (44 เทสต์) และเทสต์ไคลเอนต์ผ่าน
- ไม่ได้ Build: APK Release, Unity (ไม่มี Editor), และ Android ที่คอมไพล์ `mobile/net_client` จาก NDK (ยังไม่ได้เพิ่มในโปรเจกต์ Android)

## 4. ระบบที่ต้องแปลงจาก PC → Mobile

| ระบบ | ต้นทางใน PC (ซอร์ส `famoh01/ran-online-ep7`) | สิ่งที่ต้องทำ |
|---|---|---|
| เรนเดอร์ | `enginelib` (D3D9, ~274k บรรทัด) | เขียนใหม่ด้วย GLES (เฟส 3) |
| เครือข่าย | `netserverLib`, `netclientLib` (IOCP ของ Windows) | แทนด้วย POSIX socket และโปรโตคอลของเราเอง (ตาม HTTP ที่มีอยู่) |
| ฐานข้อมูล | ODBC / SQL Server | แทนด้วย SQLite (ทำแล้วบางส่วน) |
| เสียง | DirectSound (`EngineSoundLib`) | Android audio (ยังไม่มี) |
| ระบบไฟล์ | `RanFileSystem` | แทนด้วย Platform abstraction |
| ตรรกะเกม | `GLogicText`, `RanClientLib` | ต้องคัดแยกและพอร์ต (ดู `docs/research/mobile-port-inventory.md`) |

## 5. ไฟล์ต้นทางที่ใช้เป็นข้อมูล

- **ที่ใช้แล้ว:** ซอร์สของ `famoh01/ran-online-ep7` (อ่านอย่างเดียว ใช้นับและวางแผน) และ `tablangdelio/ranOnline` (อ่านอย่างเดียว ไม่พบไฟล์ข้อมูลเกม)
- **ไม่ได้ใช้และจะไม่ใช้:**
  - `matheomedrana/Ran-Online-EP7-Complete-Files` (ไม่มีไลเซนส์) มีไฟล์ไบนารีคอมไพล์แล้ว ไคลเอนต์เกม และฐานข้อมูลของเกมต้นฉบับ ไม่ได้แตกไฟล์หรือนำไปใช้
  - ไฟล์ zip บน Google Drive `แปลงPC Ran Ep7เป็น Mobile` (ID `1byCw7XTW_lK10_i26yUQe3FgxDBDipPr`, 23.9 MB) ตรวจได้เฉพาะ metadata ยังไม่ได้ดาวน์โหลดหรือแตกไฟล์
  - Release `ran-ep7-full-v1` และ APK ตัวอย่าง (`Rantest`) และไฟล์ mp4 (เคยตัดสินใจไม่แยกไปแล้ว)
- **เหตุผล:** ไฟล์เหล่านี้เป็นข้อมูลเกมต้นฉบับ (แผนที่ ไอเทม มอนสเตอร์ NPC สกิล เควส แอนิเมชัน เสียง และฐานข้อมูล) ซึ่งขัดกับข้อจำกัดของโปรเจกต์ที่ห้ามแยกหรือหลบเกมต้นฉบับ และขัดกับกฎเรื่องภาพที่ใช้ในแอป

## 6. ขั้นต่อไปที่ทำได้ภายในกรอบ

1. (เสร็จแล้ว Phase 1) ต่อ `mobile/net_client` เข้ากับแอป Android: เหลือทดสอบ HTTPS กับเซิร์ฟเวอร์ที่มี TLS และทดสอบการกรอกหน้าล็อกอินด้วยมือ
2. เพิ่มการเคลื่อนไหวแบบซิงก์กับเซิร์ฟเวอร์ (ตำแหน่งผู้เล่นบนเซิร์ฟเวอร์ และแอปส่ง input)
3. เพิ่มตารางแผนที่และจุดเกิดมอนสเตอร์ในเซิร์ฟเวอร์ โดยข้อมูลแผนที่สร้างขึ้นเองทั้งหมด
4. เพิ่ม `database/migrations/` และสคริปต์ seed ใหม่ในโครงสร้างที่โปรเจกต์นี้ใช้จริง
5. ทดสอบบน Android 12 จริงและบันทึกผลในเอกสารนี้

## 7. สิ่งที่ไม่สามารถทำได้ในสภาพแวดล้อมนี้

- Unity Editor และการ Build APK ผ่าน Unity
- การทดสอบบนมือถือจริง (ทดสอบได้บนอีมูเลเตอร์ใน CI เท่านั้น)
- การเซ็นลายเซ็น APK Release (ไม่มี keystore)
