# เฟส 3 — แผนเรนเดอร์ด้วย bgfx (ร่าง)

สถานะ: **ร่างแผน ยังไม่มีโค้ด** ผู้ใช้เลือก bgfx แล้ว (2026-11-21)

## เหตุผลของการเลือก
- ไลเซนส์ BSD-2-Clause ใช้ในเกมเชิงพาณิชย์ได้ และไม่บังคับให้เปิดโค้ดเกม (ต้องเก็บข้อความลิขสิทธิ์ใน NOTICE) — **ตรวจจากไฟล์ LICENSE ของ `bkaradzic/bgfx` แล้ว (ลิขสิทธิ์ 2010–2026, Branimir Karadzic)** ยังไม่ได้ตรวจ bx และ bimg ที่มาพร้อม bgfx
- มี backend สำหรับ GLES และ Vulkan ซึ่งใช้บน Android ได้โดยตรง
- ไม่ผูกกับ Wine/Box64 จึงไม่ต้องพึ่งอุปกรณ์รุ่นเฉพาะ (ต่างจาก Winlator) และไม่ใช่ DXVK ที่รันเฉพาะ Windows/Linux

## ข้อจำกัดที่ต้องรู้
- bgfx ไม่ใช่ตัวแปล D3D9 โดยตรง ต้องเขียน **adapter** แปลงคำสั่งจาก D3D9 ไปเป็น API ของ bgfx เอง
- โค้ด D3D9 ที่พบใน survey: 713 ไฟล์ที่อ้างถึง `IDirect3DDevice9`/`LPDIRECT3DDEVICE9`/`D3DX*` (ระดับ grep) ต้องทำการวิเคราะห์การเรียกที่เข้าถึงได้จริงก่อนประมาณงาน
- ฟังก์ชันที่ใช้มากที่สุด (ระดับ grep): SetRenderState 1,786 · SetTextureStageState 977 · SetTexture 370 · SetTransform 318 · BeginStateBlock/EndStateBlock 157/314 · DrawIndexedPrimitive 76 · DrawPrimitiveUP 84
- state block ของ D3D9 (BeginStateBlock/EndStateBlock) ไม่มีเทียบตรงใน bgfx ต้องจำลองด้วยการ cache state ใน adapter
- shader: โค้ดเดิมใช้ fixed-function ส่วนใหญ่ (SetFVF, SetLight, SetMaterial) ต้องเขียน shader ใหม่หรือใช้ shader ชุดมาตรฐานที่มี lighting

## ขั้นของเฟส 3 (ทำตามลำดับ)
1. **3.0 วิจัย (ทำได้ทันที):** ยืนยันไลเซนส์และเวอร์ชัน bgfx ที่จะใช้ (ไลเซนส์ bgfx ยืนยันแล้ว), เลือก commit ที่ตรึงไว้, ตรวจว่า build บน Android NDK ได้ (ต้องใช้ CI และยังไม่ทำในรอบนี้)
1a. **3.0b วิจัยการเรียก D3D9 (ใหม่):** ไล่การเรียก D3D9 ที่ใช้จริงแยกตามเมธอดและ call site แล้วจัดกลุ่ม: แมปตรงไป bgfx / ต้องจำลอง / ไม่ได้ใช้ในเกม ผลเป็นตารางในไฟล์นี้ ต้องทำก่อนประมาณงาน 3.2
2. **3.1 ชั้นอินเทอร์เฟซ:** `renderer/include/ran/render/` นิยาม API ขั้นต่ำที่เกมต้องใช้ (vertex buffer, index buffer, texture, draw, state) โดยไม่ผูกกับ D3D9 หรือ bgfx โดยตรง
3. **3.2 adapter D3D9 → อินเทอร์เฟซใหม่:** ทำทีละเมธอดตามจำนวนการเรียกจากมากไปน้อย พร้อมเทสต์ที่เทียบผลกับ D3D9 บน Windows (CI)
4. **3.3 backend bgfx:** ต่อ bgfx เข้ากับอินเทอร์เฟซ 3.1 ทดสอบบน GLES ก่อน แล้วค่อย Vulkan
5. **3.4 ฉากทดสอบ:** ฉากเดียวที่ใช้ fixed-function พร้อมภาพอ้างอิงจาก Windows เพื่อวัดความต่างของภาพ

## ผลการตรวจไลเซนส์ (2026-11-21)
- bgfx: BSD-2-Clause (ยืนยันจาก LICENSE upstream)
- ยังไม่ได้ตรวจ: bx, bimg, และไลบรารีอื่นที่ bgfx ดึงมาใน build

## สิ่งที่ยังไม่รู้ (ต้องตรวจก่อนลงมือ)
- จำนวนการเรียก D3D9 ที่ถูกเรียกจริงตอนเล่น (ต้องใช้ trace ไม่ใช่ grep)
- ขนาดงาน shader สำหรับ fixed-function ที่ใช้ในเกม
- ประสิทธิภาพบนมือถือเป้าหมาย (ยังไม่มีอุปกรณ์ทดสอบ)
