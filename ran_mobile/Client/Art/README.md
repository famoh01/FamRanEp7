# ชุดปุ่ม HUD ของ RAN MOBILE

- **ที่มา:** ออกแบบใหม่ทั้งหมดด้วยโค้ด (`tools/generate_buttons.py`) ไม่ได้คัดลอกหรือดัดแปลงภาพจากเกมอื่นหรือ APK ใด ๆ
- **ลิขสิทธิ์:** เป็นของโปรเจกต์นี้ ตามไลเซนส์ของรีโป
- **ขนาด:** ปุ่มกลม 256x256 px, แถบ 512x40 px, ช่องของ 256x256 px (ใช้ 9-slice ได้)
- **สร้างใหม่:** `pip install pillow` แล้วรัน `python tools/generate_buttons.py`

## รายการไฟล์ (โฟลเดอร์ `Buttons/`)
| ไฟล์ | ใช้กับ |
|---|---|
| btn_attack(.png / _pressed) | ปุ่มโจมตี |
| btn_dash(.png / _pressed) | ปุ่มวิ่ง/Dash |
| btn_skill(.png / _pressed) | ปุ่มสกิล |
| btn_target, btn_camera, btn_inventory, btn_character, btn_quest, btn_map, btn_settings, btn_menu | ปุ่มด้านขวาและเมนู |
| fkey_f1..f4 | ปุ่มลัด F1-F4 |
| joystick_base, joystick_handle | จอยสติ๊กเสมือน |
| slot_frame(.png / _pressed) | ช่องไอเทมลัด 1-6 |
| bar_bg, bar_fill_hp/mp/sp/exp | แถบ HP MP SP EXP |
| portrait_frame | กรอบรูปตัวละคร |

หมายเหตุ: ปุ่มเป็นรูปทรงทั่วไป ไม่มีตัวอักษรภาษาไทย (ยกเว้น F1-F4) ข้อความไทยให้ทำด้วย Text ใน Unity

## การตัดสินใจ
- ปุ่ม HUD ใช้ภาพที่สร้างจากโค้ดเท่านั้น (ผู้ใช้เลือก 2026-11-21) ไม่นำภาพจาก APK ตัวอย่างหรือแหล่งของบุคคลที่สามมาใช้
