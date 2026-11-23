# docs/drive-source-manifest.md

# แหล่งข้อมูลจริงจาก Google Drive (RAN Online EP7)

เอกสารนี้บันทึกว่าไฟล์ใดใน Google Drive ถูกใช้เป็นแหล่งข้อมูลจริงของโปรเจกต์นี้,
ไฟล์ใด **เข้าถึงได้**, ไฟล์ใด **ติดข้อจำกัด**, และถูกแปลงเป็นอะไรในโปรเจกต์

- Google Drive folder: `สร้างแรงMobile` — ID `1Y3VOeAeBzSbaB_o9oWLN16qD29b8Ro1m`
- เจ้าของโฟลเดอร์: `thanaphon281141@hotmail.com`
- การเข้าถึง: ผ่าน Google Drive integration ของ Arena (metadata + ดาวน์โหลดไฟล์ ≤ ~2 MB/ไฟล์)
- วันที่ตรวจ: 2026-11-23
- **ไม่มีการแก้ไข/ลบไฟล์ต้นฉบับใน Drive** — ทุกไฟล์ถูกอ่านอย่างเดียว

## 1. รายการไฟล์ในโฟลเดอร์ Drive (ตรวจจาก Drive API)

| # | ไฟล์ | ID | ขนาด | สถานะเข้าถึง | ใช้ทำอะไร |
|---|------|----|------|--------------|-----------|
| 1 | `RAN-EP7-Client-TH.zip.part00` | `16JlbK_BAMZcwKfooV5BNn6oWdAVcau8V` | 1,572,864,000 B (1.5 GB) | **ไม่ได้** — เกินขีดจำกัดดาวน์โหลดของ integration | — |
| 2 | `RAN-EP7-Client-TH.zip.part01` | `1xUWAoZQeRNEKtmIKA7dChgC-vgt-rAYM` | 1,010,562,339 B (1.0 GB) | **ไม่ได้** — เกินขีดจำกัด | — |
| 3 | `RAN-EP7-ServerDBTools.zip` | `1YZdc1OqAe8rpEfBZ6j_kb9YarLZSZtrO` | 49,791,515 B (47.5 MB) | **ไม่ได้** — ดาวน์โหลดล้มเหลว (ไฟล์ใหญ่เกิน) | ต้องการ: SQL schema/data, server config, tools |
| 4 | `RAN-EP7-ThaiPack-FamHawaii.zip` | `1IL8jlmvl5PGGHdaf5nlnoZdBMhTu25Ag` | 1,359,185 B | **ได้** (1,051 ไฟล์) | ข้อความไทย, เควสต์, บทสนทนา, NPC/shop config |
| 5 | `RAN-EP7-ThaiPack-FamHawaii.zip` (เก่า) | `1Z3O9kV1UyfhKG1GoqmuF-iaZTRDG0Bl2` | 2,266,660 B | ยังไม่ดาวน์โหลด | ชุดเดียวกันคนละเวอร์ชัน |
| 6 | `RAN-EP7-ItemText-Thai-Update.zip` | `14Cjnc3WPpTAi09gIDa78fldph7xu0T6h` | 912,989 B | **ได้** (3 ไฟล์) | ชื่อ/คำอธิบายไอเทมภาษาไทย (เข้ารหัส v5) |
| 7 | `RAN-EP7-Install-ThaiPack-Tutorial_TH.mp4` | `13cvpZLgHKwck1-s5sVSEOCrWpopMoU5V` | 5,227,389 B | ยังไม่ดาวน์โหลด | วิดีโอสอนติดตั้ง (ไม่ใช่ข้อมูลเกม) |
| 8 | `RAN-EP7-Install-ThaiPack-Tutorial_MiniA_TH.mp4` | `1lZxahDcrLfb1ALR97bl_RuvXAjVF9oJ2` | 5,229,412 B | ยังไม่ดาวน์โหลด | วิดีโอสอนติดตั้ง (ไม่ใช่ข้อมูลเกม) |

**สรุปข้อจำกัด:** ไฟล์ที่ต้องใช้จริงสามัญที่สุดคือ `ServerDBTools.zip` (SQL/schema/server config)
และ `Client-TH.zip` (ไฟล์ data ของ client เช่น `.crow`, `.scr`, `.item`, map, gate) **ดาวน์โหลดไม่ได้**
เพราะเกินขีดจำกัดของ Drive integration ในสภาพแวดล้อมนี้ จึง **ไม่มี** ข้อมูลตัวเลขจริงของ
มอนสเตอร์/ไอเทม/สกิล/แผนที่/EXP จาก EP7 ในงานรอบนี้ — ห้ามแต่งตัวเลขขึ้นเองและห้ามอ้างว่ามี

## 2. ไฟล์ที่ดาวน์โหลดได้จริงและนำมาใช้

### 2.1 `RAN-EP7-ThaiPack-FamHawaii.zip` (1,051 ไฟล์)

โครงสร้างภายใน (นับจากไฟล์จริง):

| กลุ่ม | จำนวน | ชนิด (ตรวจจริงแล้ว) | สถานะ | ใช้ทำอะไร |
|-------|-------|----------------------|--------|-----------|
| `data/gui/gameintext_th.txt` | 1 | **cp874 ข้อความจริงล้วน** | **[CONVERTED]** | ข้อความในเกม/UI — 494 คู่คีย์, มีอักษรไทย 485 |
| `data/gui/gameextext_th.txt` | 1 | cp874 ข้อความจริงล้วน | **[CONVERTED]** | ข้อความหน้า login/สร้างตัวละคร — 68 คู่, ไทย 66 |
| `data/gui/gameword_th.txt` | 1 | cp874 ข้อความจริงล้วน | **[CONVERTED]** | คำศัพท์/หัวตาราง — 176 คู่, ไทย 157 |
| `data/glogic/quest/*.qst` | 730 | หัวไฟล์ `default\0` + ตารางไบนารี | **[UNUSABLE]** | ข้อความที่ถอดได้ 693 ไฟล์ / 3,977 บรรทัด เป็น**ภาษาอังกฤษ**ของระบบ ไม่ใช่บทสนทนาไทย · เนื้อในเป็นตาราง struct ไม่ใช่ข้อความ |
| `data/glogic/npctalk/*.ntk` | 304 | ไบนารีตาราง ไม่เข้ารหัส | **[UNUSABLE]** | ไม่มีบทสนทนาแบบข้อความ (ถอดได้ 1 ไฟล์/3 บรรทัด) |
| `data/glogic/npcshop.ntk` | 1 | INI ข้อความจริง (`default\0…`) | **[FOUND]** แต่ **[UNUSABLE]** | เป็นดัชนีร้านค้า ไม่มีชื่อสินค้า/ราคาเป็นตัวเลขที่อ่านได้ |
| `data/glogic/ItemStrtable.txt` | 1 | เข้ารหัส (entropy 8.0) | **[UNUSABLE]** | ตารางไอเทม — ถอดไม่ได้ (ดู §3) |
| `data/glogic/SearchItemList.txt` | 1 | เข้ารหัส (entropy 8.0) | **[UNUSABLE]** | ดัชนีค้นหาไอเทม — ถอดไม่ได้ |

**สรุปตัวเลขที่ตรวจแล้ว (2026-11):**

| ชั้น | จำนวน | หมายเหตุ |
|------|-------|----------|
| คู่คีย์ในไฟล์ GUI ทั้ง 3 ไฟล์ | **738** | นับทุกบรรทัด `SENTENSE,…`/`WORD,…` |
| ในจำนวนนั้นมีอักษรไทยจริง | **708** | อีก 30 รายการเป็นตัวเลข/รหัส ไม่ใช่ข้อความ |
| คีย์ไม่ซ้ำหลังรวม 3 ไฟล์ | **707** | มี 1 คีย์ซ้ำกัน |
| ที่ใช้ในเกม (ร้านค้า/ปาร์ตี้/วาร์ป/เลเวล) | **26** | `round4_source_import.py` เลือกเฉพาะที่ระบบใช้ |

ตัวเลข 637 ที่รายงานไว้ก่อนหน้ามาจากตัวแปลงรอบแรกที่ตัดรายการซ้ำ/รายการที่ไม่ใช่ประโยคออก
รอบนี้ `round4_source_import.py` เก็บทุกบรรทัดที่มีอักษรไทยจริง และตรวจด้วย
`tools/ep7_drive_importer/test_ep7_v5.py` (เทียบค่ากับไฟล์จริงทุกครั้งที่รัน)

### 2.2 `RAN-EP7-ItemText-Thai-Update.zip` (3 ไฟล์)

| ไฟล์ | ขนาด | สถานะ |
|------|------|-------|
| `README_TH.txt` | 1,693 B | อ่านได้ — ระบุว่าไฟล์ข้อมูลเป็น "v5 ที่เข้ารหัสสำหรับ Client" |
| `data/glogic/ItemStrtable.txt` | 820,036 B | ได้ไฟล์จริง แต่เข้ารหัส v5 (Rijndael/AES) — ดู §3 |
| `data/glogic/SearchItemList.txt` | 156,804 B | ได้ไฟล์จริง แต่เข้ารหัส v5 |

## 3. การถอดรหัสไฟล์ v5 (ทำได้จริงและพิสูจน์แล้ว)

ซอร์สอ้างอิง: `TestkerTH/Ran-Online-Episode-7` (public, ไม่ใช่ LFS) ที่
`[Lib]__Engine/Sources/Common/` — ไฟล์ `CompByte.cpp`, `Rijndael.cpp`, `StringFile.cpp`

1. **`compbyte::decode`** — ตารางแทนที่ 256 ไบต์ (permutation)
   → พอร์ตเป็น `tools/ep7_drive_importer/ep7_v5.py` (ทดสอบใน `test_ep7_v5.py`)
   **แก้ข้อมูลเดิม:** ไฟล์ `.qst` **ไม่ได้** เข้ารหัส compbyte — หัวไฟล์เป็น `default` + NUL
   แบบ INI ธรรมดา เมื่อสั่ง compbyte decode จะได้ตัวอักษร `tttt…` เติมเต็ม (ขยะ)
   ข้อความอังกฤษที่เคยอ้างถึงเป็น**สตริงระบบที่ฝังมาในตาราง** ไม่ใช่ผลของการถอดรหัส
   ส่วน `.ntk` (เช่น `npcshop.ntk`) ก็เป็น INI ธรรมดาเช่นกัน — **ห้าม** compbyte decode ไฟล์กลุ่มนี้
2. **Rijndael/AES (ไฟล์ `ItemStrtable.txt`)** — คีย์ v5 = `R!E@d#i^t*_RIJNDAEL`,
   คีย์จริง = Base64(SHA-256(`"R!E@d#i^t*_005_20171011_RIJNDAEL"`)) 32 ไบต์แรก, โหมด ECB,
   block 16, ZeroPadding (จาก `CRijndael::sm_Version`, `ComputeKeyString`)
   → พอร์ตเป็น `tools/ep7_drive_importer/aes_ecb.py` (ตรวจกับ test vector FIPS-197 §C.3 ผ่าน)
   แต่ไฟล์ `ItemStrtable.txt` มีหัวไฟล์เฉพาะ (`default` + NUL + `\x03\x02`) ซึ่งบ่งชี้ว่า
   ไฟล์นี้ผ่าน **compbyte** ไม่ใช่ Rijndael — และเมื่อถอดด้วย compbyte แล้วยังไม่เป็นข้อความ
   (รูปแบบภายในเป็น v5 ของ ItemText ที่ต้องมีตัวอ่านเฉพาะ) → **สถานะ: [NOT USABLE]** รอบนี้
   ยังไม่นำชื่อไอเทมจากไฟล์นี้มาใช้

## 4. สิ่งที่ **ไม่มี** จาก Drive (และจะไม่แต่งขึ้นเอง)

| โดเมน | สถานะ | เหตุผล |
|-------|-------|--------|
| ตาราง EXP/เลเวลจริง | **[NOT FOUND]** | อยู่ใน ServerDBTools/Client data ที่ดาวน์โหลดไม่ได้ |
| ค่าสถิติมอนสเตอร์ (HP/ATK/DEF/EXP/drop) | **[NOT FOUND]** | อยู่ใน client `*.crow`/`genitem` (Client-TH.zip) |
| ไอเทม/ราคา/สเตตัส | **[NOT FOUND]** | `ItemStrtable.txt` ถอดไม่ได้ (ดู §3) |
| แผนที่/จุดวาร์ป/ประตู | **[NOT FOUND]** | อยู่ใน client map files (Client-TH.zip) |
| สกิล/ค่าดาเมจสกิล | **[NOT FOUND]** | อยู่ใน client `skill*.txt` ที่เข้ารหัส (Client-TH.zip) |
| SQL schema จริง (RanUser/RanGame1) | **[NOT FOUND]** | อยู่ใน `ServerDBTools.zip` (47.5 MB, ดาวน์โหลดไม่ได้) |
| รูปภาพ/โมเดล/เสียง | **[NOT FOUND]** | อยู่ใน Client-TH.zip (2.6 GB) |

## 4.1 สรุปสถานะรอบ 4 (ตรวจกับไฟล์จริงแล้ว ไม่ประมาณการ)

| # | รายการ | สถานะ | หลักฐาน |
|---|--------|--------|---------|
| 1 | โฟลเดอร์ Drive หลัก `1Y3VOeAeBzSbaB_o9oWLN16qD29b8Ro1m` | **[FOUND]** | เห็น 8 ไฟล์ผ่าน connector |
| 2 | `แปลงPC Ran Ep7เป็น Mobile` (23.9 MB) | **[UNUSABLE]** | 60,736 entry เป็น **LFS pointer ทั้งหมด** — ไม่มีไฟล์จริงแม้แต่ไฟล์เดียว |
| 3 | `RAN-EP7-ThaiPack-FamHawaii.zip` (2,266,660 B) | **[FOUND]** | ดาวน์โหลดสำเร็จ, แตกได้ 1,044 ไฟล์จริง |
| 4 | ข้อความ UI ไทย `game*_th.txt` | **[CONVERTED]** | 708 บรรทัดมีอักษรไทย → 707 คีย์ไม่ซ้ำ → `ep7_round4_ui_th.json` |
| 5 | ข้อความ UI เข้าเซิร์ฟเวอร์/แอป | **[CONNECTED]** | ตาราง `ui_strings` (migration v7) + `GET /api/ui/strings` + `nativeUiString()` |
| 6 | ชื่อไอเทม/ราคา/สเตตัส | **[NOT FOUND]** | `ItemStrtable.txt`/`SearchItemList.txt` ถอดไม่ได้ · `npcshop.ntk` ไม่มีชื่อ/ราคา |
| 7 | ค่ามอนสเตอร์/EXP/สกิล/แผนที่จริง | **[NOT FOUND]** | ThaiPack ไม่มีตารางกลุ่มนี้เลย (`monster`/`mob` = 0 hit) |
| 8 | `ServerDBTools.zip` (49.8 MB) · Client parts (1.5 GB ×2) | **[UNUSABLE]** | ดาวน์โหลดผ่าน connector ไม่สำเร็จ (ไม่ใช่สิทธิ์การเข้าถึง — ไฟล์ใหญ่เกิน) |
| 9 | เทสต์ฝั่งเซิร์ฟเวอร์ + ไคลเอนต์ | **[TESTED]** | 172 unittest OK · net_client ผ่าน · test-social 103 checks · ui_strings 43 checks live |

### สิ่งที่ต้องพูดตรง ๆ (แก้ข้อมูลที่เคยอ้างผิด)

**1) ชื่อทั้งหมดในเกมไม่ได้มาจาก EP7 — เป็นชื่อที่ทีมตั้งเอง**

ตรวจด้วยการค้นทั้งคลัง ThaiPack ที่แตกแล้ว (5.9 MB, 1,044 ไฟล์): ชื่อเฉพาะ **ทั้ง 29 รายการ**
ใน `content.json` (แผนที่ 3 · NPC 7 · มอนสเตอร์ 7 · ไอเทม 4 · สกิล 5 · เควสต์ 3) **ไม่ปรากฏในไฟล์ใดเลย**

| ตัวอย่างชื่อ | อยู่ในไฟล์ Drive? |
|-------------|-------------------|
| `วิทยาเขตโรงเรียน` · `ตลาดปลอดภาษี` · `ทุ่งล่าปีศาจ` | ไม่พบ |
| `ครูฝึกนักสู้` · `พยาบาลประจำวิทยาเขต` · `เภสัชกรตลาด` | ไม่พบ |
| `ดาบยาวนักเรียน` · `เกราะนักเรียน` · `ยาฟื้นฟูกำลัง` | ไม่พบ |
| `Big Vulgarian` · `Night Crow` · `Market Rat` | ไม่พบ |
| `ฟันดาบพื้นฐาน` · `ลูกไฟ` · `ศรทะลวง` | ไม่พบ |

คำศัพท์ไทยที่นำมาประกอบ (ดาบ · ยา · ครู · ปาร์ตี้ · วิทยาเขต) **มีจริง** ในไฟล์ข้อความของ Drive
แต่ "ชื่อเฉพาะ" ที่ประกอบขึ้นไม่ได้คัดลอกมาจากที่ใด — `content.json` เคยมี `_note` อ้างว่า
"ชื่อ…อ้างอิงข้อมูลจริงจาก EP7" ซึ่ง **ไม่จริง** และได้แก้แล้ว รวมทั้งลบ `_generated_by` ที่ชี้ไปยัง
สคริปต์ที่ไม่มีอยู่จริง (`build_world_content.py`)

มีเทสต์กันไม่ให้คำอ้างผิดกลับมา: `tools/ep7_drive_importer/test_content_provenance.py` (5 เคส)
จะ FAIL ถ้ามีชื่อในเกมปรากฏอยู่ในไฟล์ Drive (แปลว่าคัดลอกมา) หรือถ้า `_note` กลับไปอ้างว่าเป็นข้อมูล EP7

**2) ตัวเลขสมดุล (HP/ATK/EXP/ราคา/พิกัด) เป็นค่าที่เขียนขึ้นเอง**

ไฟล์ต้นทางที่บรรจุตัวเลขจริง (ServerDBTools / Client data) ดาวน์โหลดไม่สำเร็จ และไฟล์ที่ได้มาก็
เข้ารหัสหรือไม่มีตารางนั้น — ดูตาราง §4 ข้างต้น

**3) สิ่งที่มาจาก Drive จริงมีอย่างเดียว: ข้อความ UI ไทย 707 คีย์** (ตาราง `ui_strings`)

ข้อความ UI ภาษาไทย (ข้อ 2.1) เป็นข้อมูลจริง **ที่ใช้ได้** และถูกนำไปใช้จริงตาม `docs/PC_TO_MOBILE_PORT.md`

## 5. เอกสาร/เครื่องมือที่สร้างจากงานนี้

| ไฟล์ | บทบาท |
|------|-------|
| `tools/ep7_drive_importer/ep7_v5.py` | ถอดรหัส compbyte + ตัวอ่าน INI/CSV ของ EP7 |
| `tools/ep7_drive_importer/aes_ecb.py` | AES-128/192/256 ECB (ไม่มี dependency ภายนอก) |
| `tools/ep7_drive_importer/strings_import.py` | แปลง `game*_th.txt` (cp874) → JSON |
| `tools/ep7_drive_importer/encoded_text_import.py` | ดึงข้อความจาก `.qst`/`.ntk` ที่เข้ารหัส |
| `tools/ep7_drive_importer/build_drive_ep7_data.py` | ตัวสั่งงานทั้งหมด (idempotent) |
| `tools/ep7_drive_importer/round4_source_import.py` | ดึงข้อความ UI ไทยจาก `data/gui/game*_th.txt` → `ep7_round4_ui_th.json` (738 คู่คีย์ / 707 คีย์ไม่ซ้ำ) |
| `tools/check_jni_surface.py` | ตรวจ native method ฝั่ง Java ↔ ฟังก์ชัน JNI ฝั่ง C++ ว่าตรงกัน (27/27) |
| `tools/ep7_drive_importer/test_ep7_v5.py` | unittest ของตัวถอดรหัส/ตัวแปลง |
| `ran_mobile/Database/content/ep7/*.json` | ผลลัพธ์ที่เซิร์ฟเวอร์โหลดจริง |

## 6. วิธีรันซ้ำ (ตรวจสอบแล้ว)

```bash
# 1) ดาวน์โหลดไฟล์จาก Drive ผ่าน integration (ครั้งเดียว) — ไฟล์จะอยู่ที่ ./google_drive/
# 2) แตก ZIP ไปที่โฟลเดอร์ทำงานชั่วคราว (ไม่แตะไฟล์ต้นฉบับ)
unzip -q google_drive/RAN-EP7-ThaiPack-FamHawaii.zip -d /tmp/thaipack
# 3) แปลงเป็น JSON ของเซิร์ฟเวอร์
python3 tools/ep7_drive_importer/build_drive_ep7_data.py --thaipack /tmp/thaipack
# 4) ทดสอบ
python3 -m unittest discover -s tools/ep7_drive_importer -p "test_*.py"
```
