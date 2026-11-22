# การตั้งค่า RAN MOBILE Server

โค้ด: `ran_mobile/Server/ranserver/` (Python 3, ไม่มี dependency ภายนอก, ใช้ SQLite)
ฐานข้อมูล: `ran_mobile/Database/schema.sql` และ `seed.sql` (ดู `docs/DATA_CONVERTER.md` และหัวข้อ "ฐานข้อมูล" ด้านล่าง)

## รัน Server

```bash
cd ran_mobile/Server
python3 -m ranserver --db ../ran_mobile.db --host 0.0.0.0 --port 8080
```

- `--host 0.0.0.0` ให้เครื่องอื่นในวงเดียวกันเชื่อมได้ (เช่น มือถือจริง)
- ไฟล์ฐานข้อมูล `--db` สร้างให้อัตโนมัติถ้ายังไม่มี (ถ้าเป็นฐานข้อมูลเก่า ไม่ต้องลบ)
- ตรวจว่าทำงาน: `curl http://127.0.0.1:8080/api/health` ควรได้ `{"ok": true, ...}`

## เทสต์

```bash
cd ran_mobile/Server && python3 -m unittest discover -s tests -v   # 105 เทสต์
cd mobile/net_client && make test              # HTTP จริงกับ Server + unit test ของ state machine
cd mobile/net_client && make test-reconnect    # session หมดอายุ -> ResumeWorld, pause/resume, 401
cd mobile/assets && make test                  # ตัวโหลด asset + ไฟล์ placeholder ที่สร้างเอง
```

`make test-reconnect` จะสตาร์ท Server ด้วย `--world-idle-s 2.5` เพื่อให้ session หมดอายุระหว่างเทสต์
(ปรับได้ด้วย `IDLE_MS=...`)

## ตัวล็อกฐานข้อมูล และการจำกัดความถี่การเดิน

- `Api.handle()` ไม่ได้ล็อกทั้งคำขอแล้ว: แปลง header, parse JSON, ตรวจรูปแบบ และเขียน log ทำ**นอก**ล็อก
  ตัวล็อก (`Api.lock`) ครอบเฉพาะช่วงที่ใช้ sqlite3 connection (ตรวจ token, อ่าน/เขียนในธุรกรรม)
  การตรวจรหัสผ่าน (scrypt ซึ่งช้าโดยตั้งใจ) ทำนอกล็อก จึงไม่ทำให้คำขอของบัญชีอื่นต้องรอ
- การเดินมี 2 ชั้น:
  1. **ความถี่**: ห่างกันอย่างน้อย `world.MIN_MOVE_INTERVAL_MS` = 50 ms (Client ที่ถูกต้องส่งทุก 100 ms)
     คำขอที่ถี่กว่านั้นได้ `429 move_too_fast` พร้อม `retry_after_ms` และ**ไม่ถูกตรวจ/ไม่เขียนฐานข้อมูล**
     คำขอที่ถูกปฏิเสธด้วยระยะ/สิ่งกีดขวางก็กินโควตานี้ (กันการยิงจุดผิดซ้ำไม่จำกัด)
  2. **ระยะสะสม (token bucket)**: `MAX_SPEED` = 0.45 ของแผนที่/วินาที, ถังสะสมสูงสุด `BUDGET_CAP`
     เดินข้ามแผนที่ด้วยคำขอถี่ ๆ ไม่ได้
- `last_move_req_ms` (migration v3) แยกจาก `last_move_ms` (การเดินที่สำเร็จ) และ `last_seen_ms`
  (heartbeat/หมดอายุ) การอ่านสถานะ (`GET /world`) จึงไม่ทำให้คำขอเดินครั้งถัดไปถูกจำกัด

## Endpoint หลัก (ทั้งหมดผ่าน `/api/...`)

| Method | Path | หมายเหตุ |
|---|---|---|
| GET | `/health` | ไม่ต้องล็อกอิน |
| POST | `/auth/register`, `/auth/login` | ไม่ต้องมี token; login ล็อกชั่วคราวหลังผิด 5 ครั้งใน 15 นาที |
| POST | `/auth/logout` | ต้องมี `Authorization: Bearer <token>` |
| GET/POST | `/characters` | รายการ / สร้าง (สูงสุด 3 ตัวต่อบัญชี) |
| GET/DELETE | `/characters/{id}` | เฉพาะตัวละครของบัญชีนั้น |
| POST | `/characters/{id}/combat/attack`, `/combat/skill`, `/revive` | ดาเมจคำนวณที่ Server |
| POST | `/characters/{id}/inventory/use`, `/shop/buy`, `/shop/sell`, `/equip`, `/unequip` | ตรวจทรัพยากรที่ Server |
| POST | `/characters/{id}/quests/{qid}/accept` | |
| GET | `/world/monsters`, `/shop/items` | |
| POST | `/characters/{id}/world/enter`, `/world/leave`, `/world/move` | `move` ตอบ `200` (ตำแหน่งที่รับ), `409 position_rejected/blocked` (ตำแหน่งจริงของ Server), `409 not_in_world` (session หมด), `429 move_too_fast` |
| GET | `/characters/{id}/world` | สถานะโลก; นับเป็น heartbeat |

ทุกคำขอที่เปลี่ยนข้อมูลทำในธุรกรรมเดียว และรองรับ header `Idempotency-Key`.

## ฐานข้อมูล

- ตารางที่มี: accounts, sessions, characters, items, inventory, skills, character_skills, monsters, quests, character_quests, character_equipment, login_failures, request_log
- ข้อมูลใน `seed.sql` เป็นของโปรเจกต์นี้เองสำหรับทดสอบ (ไอเทม 5 สกิล 4 มอนสเตอร์ 5 เควสต์ 4 และอุปกรณ์ 2) ไม่ได้คัดลอกจากเกมอื่น
- **ยังไม่มีตาราง** map, npc, player_position, player_stats, logs (ยังไม่ได้เพิ่ม ดู `MOBILE_PROGRESS.md`)
- ยังไม่มีระบบ migration แบบมีเวอร์ชัน (schema ใช้ `CREATE TABLE IF NOT EXISTS`) การเปลี่ยนคอลัมน์ต้องทำเป็นสคริปต์ migration ที่ย้อนกลับได้ ซึ่งยังไม่ได้เขียน

## ความปลอดภัยที่ทำแล้ว

- รหัสผ่านเก็บเป็น scrypt hash ไม่เก็บตรง ๆ
- Token เก็บเป็น hash, มีวันหมดอายุ 24 ชั่วโมง
- ผู้เล่นเข้าถึงได้เฉพาะตัวละครของตัวเอง (ตอบ 404 เหมือนกันทั้งกรณีไม่มีและไม่ใช่ของตน)
- Server เป็นผู้คำนวณดาเมจ EXP เงิน เลเวล HP/MP; Client ส่งได้แค่คำขอ
- ตรวจ input ทุกฟิลด์ด้วย pattern และช่วงค่า
- ยังไม่มี TLS ที่ตัว Server เอง (ต้องวางไว้หลัง reverse proxy ที่มี HTTPS สำหรับการใช้งานจริง)
- ยังไม่มี rate limit ทั่วไปนอกจากการล็อกการล็อกอิน
