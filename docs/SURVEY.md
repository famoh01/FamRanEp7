# การสำรวจเบื้องต้น (Phase 1 — ระดับ grep, ยังไม่ใช่การวิเคราะห์เต็ม)

วันที่: 2026-11-21 · ซอร์ส: `ran-online-ep7` @ `4affa8f`
วิธีนับ: `grep -rl` บนไฟล์ `.cpp` / `.h` นอกโฟลเดอร์ `=*` (ยกเว้นไลบรารีภายนอก)

| หมวด | จำนวนไฟล์ | หมายเหตุ | ความยากในการพอร์ต | ทางเลือกบนมือถือ |
|---|---|---|---|---|
| ไฟล์ที่ใช้ `IDirect3DDevice9` / `D3DX*` | **713** | ตัวเลขนี้เป็นการใช้งานจริง (ไม่ใช่แค่ include) | **ยากมาก** | OpenGL ES 3.x / Vulkan ผ่าน renderer abstraction, หรือ wrapper D3D9→GL/Vulkan (ต้องประเมินเพิ่ม) |
| `#include <d3d9.h>` / `<d3dx9.h>` โดยตรง | 18 | จุดเข้าระดับโค้ด | ยาก | รวมไว้ใน renderer |
| `#include <windows.h>` โดยตรง | 37 | ต้องดูอีกครั้งว่าใช้ API ใดบ้าง (ไม่รวม transitive include) | กลาง | หลังอินเทอร์เฟซ platform (SDL2/Android) |
| `dinput.h` / `dsound.h` | 11 | อินพุตและเสียง | กลาง | Touch layer ใหม่ + OpenSL ES / SDL audio |
| อ้างถึง GameGuard / NPGame / ggsrv | 33 | ระบบป้องกันโกง | ไม่ควรแตะโดยไม่ตกลง | ดู `LICENSE_RISKS.md` |
| `.cpp` ฝั่งโค้ดเกมทั้งหมด | 1,326 | ขนาดฐานของงานพอร์ต | — | — |

## ข้อจำกัดของการสำรวจนี้

- ยังไม่ได้วิเคราะห์ Win32 API ทั้งหมด (CreateWindow, message loop, registry, thread) อย่างละเอียด
- ยังไม่ได้นับ DirectX วิธีเรียกผ่านฟังก์ชันของเอนจิน (`m_pd3dDevice->…`) แยกตามเมธอด
- ยังไม่ได้วิเคราะห์การใช้ `\` ในเส้นทางไฟล์และชื่อไฟล์ที่มีตัวพิมพ์ใหญ่/เล็กสับสน
- การประเมินเวลาเฟส 2–6 ต้องรอผลวิเคราะห์เต็ม (Phase 1 จริง)

---

## ผลสำรวจละเอียด (Phase 1, ระดับ grep, ซอร์ส commit `4affa8f`)

สคริปต์: `survey.sh` (อ่านอย่างเดียว ไม่แก้ซอร์ส) · ตัวเลขเป็นจำนวนการเรียก ไม่ใช่จำนวนไฟล์

### D3D9 — เมธอดที่เรียกบ่อยที่สุด

| กลุ่ม | เมธอด (จำนวนครั้ง) | ผลต่อการพอร์ต |
|---|---|---|
| Fixed-function state | `SetRenderState` (1,786), `SetTextureStageState` (977), `SetSamplerState` (261), `SetTexture` (370) | ต้องจำลองด้วย shader — งานหลัก |
| Transform และไฟ | `SetTransform` (318), `LightEnable` (114), `SetLight` (48), `SetMaterial` (96) | จำลอง fixed-function lighting ใน shader |
| FVF / vertex format | `SetFVF` (185), `SetStreamSource` (156), `SetIndices` (127) | แปลงเป็น vertex layout ของ GLES/Vulkan |
| State blocks | `BeginStateBlock` (157), `EndStateBlock` (314), `ApplyStateBlock` (22) | ต้องทำ state cache เอง |
| Shader | `SetVertexShader` (33), `SetVertexShaderConstantF` (33), `SetPixelShader` (16), `CreateVertexShader` (25) | shader ของเกมต้องแปลงเป็น GLSL/SPIR-V |
| Draw | `DrawPrimitive` (45), `DrawPrimitiveUP` (84), `DrawIndexedPrimitive` (76), `DrawIndexedPrimitiveUP` (15) | ต้องจัด batch ให้ทันบนมือถือ |
| Buffer/Texture | `CreateVertexBuffer` (105), `CreateIndexBuffer` (75), `CreateTexture` (37) | ต้องรองรับ context loss |
| Render target | `SetRenderTarget` (86), `SetDepthStencilSurface` (84), `GetBackBuffer` (20) | รองรับ FBO/offscreen |
| Clear / ตรวจสอบ | `Clear` (49), `GetDeviceCaps` (61) | ค่า capability ต้องแมปใหม่ |

**ข้อสรุป:** งานกว่า 80% ของการเรียก D3D9 อยู่ใน fixed-function state, transform, lighting และ FVF ซึ่งหมายถึงต้องเขียน shader จำลอง fixed-function ก่อน จึงจะเห็นภาพเกมถูกต้อง

### Win32 และระบบอื่น (จำนวนไฟล์ที่ใช้)

| API | ไฟล์ | หมายเหตุ |
|---|---|---|
| `GetMessage` / `PeekMessage` / `DispatchMessage` | 43 / 6 / 8 | message loop ต้องแทนด้วย event จาก platform layer |
| `WaitForSingleObject` / `CreateThread` / `CreateMutex` | 35 / 21 / 3 | threading แทนด้วย std::thread |
| `CreateFile` / `GetModuleFileName` | 26 / 32 | ระบบไฟล์ต้องแปลง path |
| `timeGetTime` / `QueryPerformanceCounter` | 20 / 3 | timer ต้องแทนด้วย `clock_gettime` |
| `RegOpenKey` / `RegQueryValue` | 12 / 9 | registry ต้องย้ายไปไฟล์ config |
| `SetCursor` / `ShowCursor` / `ClipCursor` | 27 / 8 / 2 | ไม่มีเมาส์บนมือถือ (เฟส 5 แทนด้วยปุ่มสัมผัส) |
| `SetWindowLong` / `CreateWindow` / `GetDC` / `ReleaseDC` | 18 / 7 / 25 / 20 | หน้าต่างแทนด้วย Android surface |
| DirectInput / DirectSound (object) | 18 | ต้องย้ายไป touch layer และ OpenSL ES/SDL audio |
| path ที่มี `\` ในสตริง | 81 | ต้องแปลงเป็น `/` และจัดการตัวพิมพ์ |

### ข้อจำกัดที่ยังค้าง
- ยังไม่ได้วิเคราะห์ว่าฟังก์ชันไหนอยู่ใน code path ที่เกมเรียกจริงตอนเล่น (ต้องใช้ profiler หรือ log)
- การนับ `SetRenderState` ฯลฯ นับทุกจุดที่พิมพ์ ไม่ได้แยกจากโค้ดที่ไม่ถูกเรียก
