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
cd ran_mobile/Server && python3 -m unittest discover -s tests -v   # 44 เทสต์
cd mobile/net_client && make test                                    # เทสต์ HTTP จริงกับ Server
```

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
