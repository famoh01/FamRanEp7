# Placeholder assets และตัวโหลดที่มีการตรวจสอบ (ขั้น d)

อัปเดต: 2026-11-22 · ไฟล์โค้ด: `mobile/assets/` · นโยบายคลีนรูม: `docs/CLEAN_ROOM.md`

## ทำไมต้องมี

แอปต้องมี "ไปป์ไลน์ asset" ที่ทดสอบได้จริงก่อนจะมีข้อมูลเกมจริง (ซึ่งยัง BLOCKED เพราะไม่มีใบอนุญาต)
ขั้นนี้จึงสร้างไฟล์ placeholder ขนาดจิ๋วที่ **โปรเจกต์สร้างเองทั้งหมด** เพื่อพิสูจน์เส้นทาง:
อ่าน manifest → ตรวจขนาด/checksum → โหลด/ปฏิเสธ → รายงานไฟล์หาย โดยไม่แตะไฟล์ของเกมต้นฉบับ

## ไฟล์ที่สร้าง (ใน `mobile/android/app/src/main/assets/`)

| ไฟล์ | kind | ขนาด | ลักษณะ |
|---|---|---|---|
| `assets_manifest.bin` | — | 368 B | manifest แบบข้อความ ASCII |
| `ph_ground.bin` | texture | 16,392 B | ภาพ PHP1 64×64 RGBA (พื้นลายตาราง) |
| `ph_block.bin` | texture | 4,104 B | ภาพ PHP1 32×32 RGBA (กล่องสิ่งกีดขวาง) |
| `ph_player.bin` | texture | 1,032 B | ภาพ PHP1 16×16 RGBA (ตัวละคร) |
| `ph_ui_frame.bin` | raw | 4,616 B | ภาพ PHP1 48×24 RGBA (กรอบ UI) |

สร้าง/ตรวจใหม่:
```
python3 mobile/assets/make_placeholder_assets.py          # เขียนไฟล์
python3 mobile/assets/make_placeholder_assets.py --check  # ตรวจว่าไฟล์ตรงกับตัวสร้าง
make -C mobile/assets test                                # unit test + ตรวจไฟล์จริง + checksum
```

## รูปแบบ manifest

```
# คอมเมนต์ได้ (บรรทัดขึ้นต้นด้วย #) และเว้นบรรทัดว่างได้
ran-assets 1
<name> <kind> <bytes> <checksum>
ph_ground.bin texture 16392 7e4e661a
```

- `name`: `[a-z0-9_]+\.bin` เท่านั้น (ไม่มี `/` `\` `..` → ไม่สามารถชี้ไฟล์นอกโฟลเดอร์ asset)
- `kind`: `texture` | `label` | `raw`
- `bytes`: จำนวนไบต์ (ต้อง ≤ 1 MiB)
- `checksum`: FNV-1a 32 บิต เป็นเลขฐานสิบหก **ตัวพิมพ์เล็ก 8 หลัก**

ตัว parse ปฏิเสธ: ไม่มี header/version ผิด, kind ไม่รู้จัก, ขนาดติดลบหรือเกินกำหนด, checksum ผิดรูป,
ชื่อซ้ำ, ชื่อผิดรูปแบบ, ฟิลด์เกิน, manifest ใหญ่เกิน 64 KiB — ทุกกรณีมี unit test ใน
`mobile/assets/test_assets.cpp`

## ผลการโหลดในแอป

`main.cpp` เรียก `LoadPlaceholderAssets()` หลัง `LoadLabels()`:

- สำเร็จ → `LOGI("placeholder assets: 4 loaded, 0 missing, 0 broken (4 listed)")`
- ไฟล์หาย/ขนาดไม่ตรง/checksum ไม่ตรง → `LOGE("assets: <ชื่อ>: <สถานะ>")` ต่อไฟล์
  และ **ไม่ทำให้แอปหยุด** แผนที่ยังวาดด้วยรูปทรงที่สร้างจากโค้ด (procedural)
- ภาพ PHP1 ที่ผ่านการตรวจถูกอัปโหลดเป็น texture RGBA (`DrawTexturedQuad`) ทับพื้นและกล่อง

## รูปแบบไฟล์ PHP1 (placeholder image)

```
"PHP1" | uint16 w (LE) | uint16 h (LE) | w*h*4 bytes RGBA8
```

ทดสอบว่า magic/ขนาดตรง (`ph_ground.bin is a PHP1 image of width 64`) ใน `make -C mobile/assets test`

## สิ่งที่ยังไม่ทำ

- ยังไม่มีข้อมูลเกมจริง (แผนที่ โมเดล texture ตัวละคร/มอนสเตอร์) — BLOCKED ตาม `docs/EP7_DATA_MANIFEST.md`
- ยังไม่มี atlas/compression: ไฟล์ placeholder เล็กพอที่จะโหลดทั้งก้อน
- `ph_ui_frame.bin` ยังไม่ได้วาด (kind=raw) ใช้เป็นตัวอย่างของ kind ที่ไม่ใช่ texture
