# RAN MOBILE (ฉบับของโปรเจกต์นี้)

โปรเจกต์ MMORPG มือถือที่เขียนขึ้นใหม่ทั้งหมดตามสเปกของผู้ใช้ **ไม่ใช่ซอร์สโค้ดของเกมต้นฉบับ** และไม่ได้ใช้สินทรัพย์จากเกมหรือ APK ของบุคคลที่สาม

## สถานะ (ซื่อตรง)

| ส่วน | สถานะ | ทดสอบได้ที่ไหน |
|---|---|---|
| Database (`Database/schema.sql`, `seed.sql`) | เสร็จ | ทดสอบผ่าน SQLite ใน `Server/tests` |
| เซิร์ฟเวอร์ API (`Server/ranserver/`) | เสร็จ (พัฒนา) 35 เทสต์ผ่าน | `python -m unittest` |
| ปุ่ม HUD (`Client/Art/Buttons/`) | เสร็จ | สร้างใหม่ด้วย `tools/generate_buttons.py` |
| สคริปต์ Unity (`Client/Scripts/`) | **ยังไม่เขียน** | ต้องใช้ Unity Editor (sandbox นี้ไม่มี) |
| Prefab / Scene / UI | **ยังไม่สร้าง** | ทำใน Unity ตามขั้นตอนด้านล่าง |

## เวอร์ชันที่ใช้
- Python 3.10 ขึ้นไป (ทดสอบบน 3.11) ใช้เฉพาะไลบรารีมาตรฐาน (`sqlite3`, `hashlib.scrypt`, `http.server`)
- SQLite 3.40+ (มากับ Python)
- Pillow 10+ สำหรับสร้างปุ่ม (ไม่จำเป็นต่อการรันเซิร์ฟเวอร์)
- Unity 2022.3 LTS และ `com.unity.ugui` (ยังไม่ได้ทดสอบ เพราะไม่มี Unity ในเครื่องที่ใช้พัฒนา)

## ติดตั้งและรันเซิร์ฟเวอร์

```bash
cd ran_mobile/Server
python -m unittest discover -s tests -v          # รันเทสต์ทั้งหมด (ต้องผ่านก่อนเสมอ)
python -m ranserver --db ../ran_mobile.db --host 127.0.0.1 --port 8080
```

ถ้าต้องการให้มือถือในวง Wi-Fi เดียวกันเชื่อมต่อ ใช้ `--host 0.0.0.0` และตั้ง firewall ให้เหมาะสม

ตรวจสอบว่าเซิร์ฟเวอร์ทำงาน:
```bash
curl http://127.0.0.1:8080/api/health
```

## เชื่อมต่อ Server กับ Database
- เซิร์ฟเวอร์สร้างไฟล์ SQLite อัตโนมัติจาก `Database/schema.sql` และใส่ข้อมูลตั้งต้นจาก `Database/seed.sql` (รันซ้ำได้)
- เปิด WAL และ foreign key ทุกการเชื่อมต่อ
- ทุกคำสั่งที่แก้ข้อมูลทำในธุรกรรม (`BEGIN IMMEDIATE`) ถ้าเกิดข้อผิดพลาดจะ rollback ทั้งหมด
- ย้ายไป MySQL หรือ PostgreSQL ได้ โดยเปลี่ยนชั้น `db.py` และ SQL ของ `schema.sql` (ตอนนี้ยังไม่ได้ทำ)

## เชื่อมต่อ Client กับ Server
- Client เรียก HTTP JSON ไปที่ `http://<host>:8080/api/...`
- ล็อกอินที่ `POST /api/auth/login` แล้วส่ง `Authorization: Bearer <token>` ทุกครั้ง
- คำขอที่แก้ข้อมูลส่ง `Idempotency-Key` (8-64 ตัว: A-Z a-z 0-9 _ -) เพื่อกันการทำซ้ำเมื่อเน็ตหลุด ผลเดิมจะถูกตอบกลับ
- บนมือถือจริง Unity ต้องชี้ไปที่ IP ของเครื่องที่รันเซิร์ฟเวอร์ (ไม่ใช่ 127.0.0.1 เพราะนั่นคือเครื่องมือถือเอง)

### รายการ API
| Method | Path | หมายเหตุ |
|---|---|---|
| GET | /api/health | ไม่ต้องล็อกอิน |
| POST | /api/auth/register | `username`, `password` (8-128), `email` (ไม่บังคับ) |
| POST | /api/auth/login | คืน `token` อายุ 24 ชม. |
| POST | /api/auth/logout | |
| GET / POST | /api/characters | สูงสุด 3 ตัวต่อบัญชี |
| GET / DELETE | /api/characters/{id} | เฉพาะเจ้าของเท่านั้น |
| POST | /api/characters/{id}/combat/attack | `monster_id` ดาเมจคำนวณที่เซิร์ฟเวอร์ |
| POST | /api/characters/{id}/combat/skill | `skill_id`, `monster_id` ตรวจ MP และคูลดาวน์ |
| POST | /api/characters/{id}/revive | ฟื้นคืนชีพเมื่อ HP = 0 |
| POST | /api/characters/{id}/inventory/use | `slot` ใช้ได้เฉพาะ Consumable |
| POST | /api/characters/{id}/shop/buy | `item_id`, `quantity` |
| POST | /api/characters/{id}/shop/sell | `slot`, `quantity` ขายได้ครึ่งราคา |
| POST | /api/characters/{id}/quests/{quest_id}/accept | |
| GET | /api/world/monsters | สถานะมอนสเตอร์และเวลาเกิดใหม่ |
| GET | /api/shop/items | รายการสินค้า |

## กติกาความปลอดภัยที่บังคับใช้ฝั่งเซิร์ฟเวอร์
- รหัสผ่านเก็บเป็น scrypt hash เท่านั้น และตอบ "ชื่อผู้ใช้หรือรหัสผ่านผิด" เหมือนกันทุกกรณี
- Token เก็บเป็น SHA-256 ในฐานข้อมูล และมีวันหมดอายุ
- ตัวละครของบัญชีอื่นถูกตอบว่า 404 (ไม่เปิดเผยว่ามีอยู่)
- Client ไม่สามารถส่งดาเมจ ราคา เงิน หรือไอเทมเองได้ ค่าเหล่านี้ถูกคำนวณจากฐานข้อมูล
- จำกัดขนาด body 64 KB และไม่รับ `Transfer-Encoding: chunked`
- ไม่แสดงรายละเอียดข้อผิดพลาดของฐานข้อมูลให้ Client และไม่บันทึก token ลง log

## ข้อจำกัดที่ควรรู้
- `http.server` ของ Python ใช้ได้เฉพาะการพัฒนาและการทดสอบ ยังไม่เหมาะกับการใช้งานจริงกับผู้เล่นจำนวนมาก
- ยังไม่มีการตรวจระยะ (range) ระหว่างตัวละครกับมอนสเตอร์ เพราะยังไม่มีระบบตำแหน่งบนแผนที่
- มอนสเตอร์และเควสต์ตัวอย่างมีเพียงอย่างละ 1-2 รายการ ใช้สำหรับทดสอบเท่านั้น
- ระบบ anti-cheat ในระดับ client (เช่น GameGuard) ไม่เกี่ยวข้องกับโปรเจกต์นี้ และยังไม่ได้ตัดสินใจเรื่องนี้

## สร้าง Client ใน Unity (ขั้นตอนที่ยังทำไม่ได้จากข้อความ)
1. สร้างโปรเจกต์ Unity 2022.3 LTS แบบ 2D หรือ 3D (Mobile)
2. คัดลอก `Client/Art/Buttons/` เข้า `Assets/UI/Buttons/` และตั้ง Texture Type = Sprite (2D and UI)
3. สร้าง Canvas แบบ Scale With Screen Size (Reference 1920x1080, Match 0.5) เพื่อรองรับ 16:9 ถึง 20:9
4. วางปุ่มตามตำแหน่งที่สเปกระบุ (ซ้าย: จอยสติ๊ก, ขวา: ปุ่มโจมตีและเมนู, ล่าง: ช่องไอเทม 1-6)
5. ผูกปุ่มกับสคริปต์ที่จะเขียนในขั้นถัดไป

## ลิขสิทธิ์
- โค้ดและสินทรัพย์ในโฟลเดอร์นี้สร้างขึ้นใหม่สำหรับโปรเจกต์นี้ ดู `Client/Art/README.md`
- ซอร์สของเกมต้นฉบับ (`ran-online-ep7`) มีหมายเหตุว่าลิขสิทธิ์เป็นของ Min Communications, Inc. โปรเจกต์นี้ไม่ได้ใช้โค้ดจากส่วนนั้น
