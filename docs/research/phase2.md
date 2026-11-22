# Phase 2 — เลือก platform layer (ตัวเลือก 3 แบบ)

วันที่ค้น: 2026-11-21

| ตัวเลือก | ไลเซนส์ | Android | ข้อดี | ข้อเสีย |
|---|---|---|---|---|
| **SDL2** | zlib (ตั้งแต่ 2.0) | รองรับทางการ | ใช้ได้ทั้ง Windows และ Android, มี input/audio/window ครบ, แปลงโค้ดเกมน้อย | ต้องผูกกับ SDL ต่อไป, SDL3 มี API ต่างกัน |
| GLFW | zlib | ไม่รองรับ Android | เบา เหมาะ desktop | ไม่ครอบคลุมมือถือ |
| Android NativeActivity / GameActivity | Apache-2.0 (GameActivity) — **ยังไม่ตรวจไลเซนส์ซ้ำ** | เป็นของ Android เอง | ไม่มี dependency นอก | ต้องเขียน Windows backend แยกเองทั้งหมด, lifecycle ซับซ้อน |

**ตัวที่เลือก: SDL2**
- ไลเซนส์ zlib อนุญาตให้ลิงก์แบบ static ในผลิตภัณฑ์ปิดซอร์สได้ [1](https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer)
- SDL อย่างเป็นทางการรองรับ Windows, macOS, Linux, iOS และ Android [2](https://github.com/ennorehling/android-sdl/tree/master/jni/SDL2)
- เหตุผลเพิ่มเติม: ลดงานเขียน backend สองชุดของ input, window และ audio

**ข้อควรระวัง**
- ใช้ SDL2 ไม่ใช่ SDL 1.2 เพราะ SDL 1.2 ใช้ LGPL [1](https://en.wikipedia.org/wiki/Simple_DirectMedia_Layer)
- ระหว่าง `ran::platform` กับ SDL2 ต้องมี adapter ไม่ให้ไฟล์เกมเรียก SDL โดยตรง เพื่อให้เปลี่ยน backend ได้

**สิ่งที่ยังต้องยืนยันก่อนใช้จริง**
- เวอร์ชัน SDL2 ที่จะใช้และไฟล์ LICENSE ของเวอร์ชันนั้น (ยังไม่ได้ดาวน์โหลดเข้ารีโป)
- ตรวจไลเซนส์ GameActivity ถ้าจะใช้ในอนาคต
