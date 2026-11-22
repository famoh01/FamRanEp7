# CLEAN-ROOM POLICY / นโยบายคลีนรูม (โปรเจกต์ RAN EP7 Mobile)

อัปเดต: 2026-11-22

โปรเจกต์นี้ **ไม่นำเข้า ไม่แปลง ไม่แพ็ก และไม่แจกจ่าย** ไฟล์หรือโค้ดของเกม RAN Online EP7 ต้นฉบับ
รวมถึงไม่ใส่ระบบป้องกันการโกง (GameGuard/Hackshield และอื่น ๆ) ลงในโปรเจกต์ ทุกไฟล์ที่แอปโหลด
ถูกสร้างโดยสคริปต์ในรีโปนี้เอง

## สิ่งที่ห้ามนำเข้า (และสถานะปัจจุบัน)

| รายการ | สถานะ | หลักฐาน |
|---|---|---|
| โฟลเดอร์/ไฟล์ข้อมูลเกมต้นฉบับ (map, model, texture, item, mob, npc, skill, quest) | **ไม่นำเข้า** | `docs/EP7_DATA_MANIFEST.md`, `docs/DATA_CONVERTER.md` · ยัง BLOCKED เพราะไม่มีใบอนุญาต |
| ไฟล์ `.bak`/`.sql` ของฐานข้อมูลเกม | **ไม่นำเข้า** | เหตุผลเดียวกับข้างบน |
| APK ตัวอย่าง (Rantest ฯลฯ) | **ไม่แตก ไม่ใช้** | `docs/EP7_DATA_MANIFEST.md` |
| วิดีโอ MP4 / ไฟล์ที่แตกจากวิดีโอ | **ไม่ดาวน์โหลด ไม่แตกไฟล์** | ใช้เป็นวิดีโอประกอบการดูเท่านั้น |
| GameGuard / Hackshield / แพ็กเกจ anti-cheat | **ไม่ใส่ ไม่เชื่อม ไม่ bypass** | การ trace D3D9 ของไคลเอนต์ต้นฉบับถูกบันทึกว่า "ถูกบล็อก" และไม่พยายามข้าม · `docs/research/phase3-bgfx.md` |
| ฟอนต์หรือภาพจากบุคคลที่สาม | ใช้เฉพาะที่มีสัญญาอนุญาตชัดเจน | Noto Sans Thai (SIL OFL 1.1) ใน `mobile/android/tools/fonts/` |

## สิ่งที่โปรเจกต์สร้างเอง

| ไฟล์ | ตัวสร้าง | ลักษณะ |
|---|---|---|
| `mobile/android/app/src/main/assets/labels.bin` | `mobile/android/tools/make_labels.py` | ข้อความ UI ภาษาไทยเรนเดอร์จากฟอนต์ OFL |
| `mobile/android/app/src/main/assets/assets_manifest.bin` และ `ph_*.bin` | `mobile/assets/make_placeholder_assets.py` | ภาพ placeholder สังเคราะห์ขนาดจิ๋ว (PHP1 + RGBA) ทำจากสูตรเลขคณิต |
| ปุ่ม HUD (`ran_mobile/Client/Art/Buttons/*.png`) | `ran_mobile/Client/Art/tools/generate_buttons.py` | วาดด้วยโค้ด |
| แผนที่ทดสอบ/ตัวละคร/มอนสเตอร์/ไอเทม/สกิล/เควสต์ | `ran_mobile/Database/seed.sql` + `mobile/net_client` | ข้อมูลของโปรเจกต์นี้เอง ไม่ใช่ข้อมูล EP7 |

## การตรวจสอบอัตโนมัติ

- `make -C mobile/assets test` ตรวจว่าไฟล์ placeholder บนดิสก์ตรงกับตัวสร้าง และตรงกับขนาด/checksum
  ที่ระบุใน manifest (ฟ้องทันทีถ้ามีคนวางไฟล์อื่นทับ)
- ตัวโหลด (`mobile/assets/asset_manifest.h`) โหลดได้เฉพาะชื่อที่อยู่ใน manifest เท่านั้น
  ชื่อไฟล์ที่มี `/`, `\`, `..` ถูกปฏิเสธตั้งแต่ตอน parse จึงวางไฟล์นอกโฟลเดอร์ asset ไม่ได้
- CI `ran-mobile-ci` รันทั้งชุดข้างบนทุกครั้งที่แก้ `mobile/assets/**` หรือ `mobile/net_client/**`

## ถ้าจะเพิ่มข้อมูลจริงในอนาคต

1. ต้องมีหลักฐานสิทธิ์ใช้งาน (ใบอนุญาต/หนังสืออนุญาต) ก่อน
2. ไฟล์ต้นฉบับต้องอยู่ **นอก** รีโป (อ่านอย่างเดียว) output ที่แปลงแล้วแยกโฟลเดอร์ ไม่ทับต้นฉบับ
3. ต้องผ่าน Validator ที่ตรวจ header/ขนาด/ช่วงค่า/การอ้างอิง ID และมี test ที่ใช้ไฟล์ที่ได้รับอนุญาต
4. ค่าใดที่แปลงไม่ได้ให้รายงานเป็น MISSING/UNKNOWN ห้ามเดาค่า (ดู `docs/DATA_CONVERTER.md`)
