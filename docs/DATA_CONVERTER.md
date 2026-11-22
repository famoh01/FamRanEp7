# ตัวแปลงข้อมูล EP7 (สถานะ: ยังไม่สร้าง)

**ไม่มี Converter ในรอบนี้ เพราะยังไม่มีข้อมูล EP7 ที่ได้รับสิทธิ์ใช้งาน**

ตามหลัก "ตรวจ format ก่อนแปลง ห้ามเดา format" การเขียน Converter ต้องทำหลังจากมีไฟล์จริงและตรวจ header แล้วเท่านั้น

## ลำดับ pipeline ที่จะสร้างเมื่อมีข้อมูล

```
EP7 Data (ต้นฉบับ, อ่านอย่างเดียว)
  → Parser     อ่านโครงสร้างไฟล์ตาม format ที่ตรวจแล้ว
  → Validator  ตรวจ header, ขนาด, ค่าที่อยู่ในช่วง, การอ้างอิง ID
  → Converter  แยกตามประเภท (Map, Model, Texture, Item, Mob, NPC, Skill, Quest, Animation, Effect)
  → Mobile Format  ไฟล์ output แยกจากต้นฉบับ (ไม่เขียนทับ)
  → Game Runtime   แอป Android โหลดไฟล์ output
  → Server/Game State  ข้อมูลที่ Server ใช้ตัดสิน (ไม่ใช่ Client)
```

กฎ:
- ไม่แก้หรือลบไฟล์ต้นฉบับ output ต้องอยู่คนละโฟลเดอร์
- ทุก Converter ต้องมี test ที่ใช้ไฟล์ตัวอย่างจริงที่ได้รับอนุญาต
- ข้อมูลที่แปลงไม่ได้ต้องรายงานเป็น MISSING หรือ UNKNOWN ไม่ใช่ใส่ค่าเดา

## ประเภท Converter ที่จะแยกทำ

| ประเภท | ต้องใช้ไฟล์ | สถานะ |
|---|---|---|
| Map | ไฟล์แผนที่ (จาก `data/map` หรือเทียบเท่า) | MISSING EP7 DATA / BLOCKED |
| Model | โมเดล (`.x` หรือเทียบเท่า) | MISSING EP7 DATA / BLOCKED |
| Texture | texture (`.dds`, `.cps`, `.tga` หรือเทียบเท่า) | MISSING EP7 DATA / BLOCKED |
| Item | ตารางไอเทม | MISSING EP7 DATA |
| Mob | ตารางมอนสเตอร์ | MISSING EP7 DATA |
| NPC | ตาราง NPC | MISSING EP7 DATA |
| Skill | ตารางสกิล | MISSING EP7 DATA |
| Quest | ไฟล์เควสต์ | MISSING EP7 DATA / BLOCKED |
| Animation | ไฟล์แอนิเมชัน | MISSING EP7 DATA / BLOCKED |
| Effect | ไฟล์เอฟเฟกต์ | MISSING EP7 DATA / BLOCKED |

รายละเอียดว่าไฟล์ใดพบที่ไหนและสถานะเป็นอย่างไร อยู่ใน `docs/EP7_DATA_MANIFEST.md`
