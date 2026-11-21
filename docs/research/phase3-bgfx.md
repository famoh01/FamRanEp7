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

## ผลวิเคราะห์ 3.0b: การเรียก D3D9 จากซอร์ส (2026-11-21)

แหล่ง: `famoh01/ran-online-ep7` ที่ commit `4affa8f` (อ่านอย่างเดียว ไม่แก้ไข) ตัวเลขด้านล่างคือ **จำนวนครั้งที่ปรากฏในซอร์ส (grep) ไม่ใช่จำนวนการเรียกตอนรันจริง** ต้องใช้ trace ยืนยันก่อนประมาณงาน
- ไฟล์ที่อ้างถึง D3D9 ในโค้ดเกม (ไม่รวม `=` SDK/ไลบรารีภายนอก): 718 ไฟล์ · ส่วนใหญ่อยู่ใน `enginelib/` (488) แล้วตามด้วย `RanClientUILib/` (80) และ `RanClientLib/` (58)
- ไม่มีไฟล์ shader ต้นฉบับ (`.fx`, `.vsh`, `.psh`) ใน repo ซอร์ส ไฟล์ effect ถูกโหลดตามชื่อจากข้อมูลเกม (เช่น `Possasi.fx` ใน `DxPostProcess.cpp`) ต้องได้จากชุดข้อมูลของผู้ใช้เอง

### A. แมปตรงไป bgfx ได้ (state และ draw)
| กลุ่ม | ตัวอย่างการเรียก (grep) | หมายเหตุการแมป |
|---|---|---|
| Draw | DrawIndexedPrimitive 76, DrawPrimitive 45, DrawPrimitiveUP 84, DrawIndexedPrimitiveUP 15 | index/vertex buffer ปกติ → `bgfx::setVertexBuffer` / `setIndexBuffer`; แบบ UP → transient buffer |
| Buffer | CreateVertexBuffer 105, CreateIndexBuffer 75, SetStreamSource 156, SetIndices 127, Lock 270 / Unlock 173 | ใช้ dynamic/transient buffer ของ bgfx แทน Lock/Unlock; ต้องออกแบบการอัปเดตใหม่ (Lock 270 จุดคือความเสี่ยงหลัก) |
| Texture | SetTexture 370, CreateTexture 37, CreateCubeTexture 4 | `bgfx::createTexture2D` / cube; sampler ผ่าน uniform |
| Render target | SetRenderTarget 86, SetDepthStencilSurface 84, GetBackBuffer 20, StretchRect 5 | `bgfx::FrameBufferHandle`; StretchRect ต้องเขียน blit เอง |
| Clear/Present | Clear 49, Present 9, BeginScene 7, EndScene 8 | `bgfx::setViewClear` / `bgfx::frame()` |
| Blend/depth/cull | SetRenderState ของ ALPHABLENDENABLE 280, SRC/DESTBLEND 221/231, ZWRITEENABLE 202, ZENABLE 69, CULLMODE 113, BLENDOP 22, ZFUNC 19 | `bgfx::setState` ค่ารวมต่อ draw |

### B. ต้องจำลอง (ไม่มีเทียบตรงใน bgfx)
| กลุ่ม | จำนวน (grep) | วิธีที่เสนอ |
|---|---|---|
| Lighting แบบ fixed-function | LightEnable 114, SetLight 48, SetMaterial 96, D3DRS_LIGHTING 208, AMBIENT 21, NORMALIZENORMALS 32 | shader ชุดเดียวที่คำนวณไฟ Gouraud จาก uniform ของแสงและวัสดุ |
| Texture stage combine | SetTextureStageState 977, COLOROP 387, COLORARG1/2 171/143, ALPHAOP 165, ALPHAARG 79/67 | สร้าง shader ตามการตั้งค่า stage (permutation) หรือจำกัดให้เหลือชุดที่ใช้จริง |
| Fog | FOGENABLE 133, FOGCOLOR 46, FOGSTART/END/DENSITY 6 ต่อค่า, FOGTABLEMODE/VERTEXMODE 6 | คำนวณ fog ใน shader |
| Alpha test | ALPHATESTENABLE 66, ALPHAFUNC 41, ALPHAREF 59 | `discard` ใน fragment shader |
| Texture transform / TCI | TEXTURETRANSFORMFLAGS 26, TCI_CAMERASPACE* 21, SetTransform 318 | uniform เมทริกซ์ texture + คำนวณ texcoord ใน shader |
| Bump env | BUMPENVMAT10/11 16 | ยังไม่รู้ว่าใช้ในเส้นทางที่เล่นจริงหรือไม่ ต้อง trace |
| State blocks | BeginStateBlock 157, EndStateBlock 314, Apply/Capture/Delete 55 | bgfx ไม่มี state block ให้ cache ค่า state ใน adapter แล้วส่งตอน draw |
| Vertex format (FVF) | SetFVF 185; XYZ 151, TEX1 124, DIFFUSE 84, NORMAL 83, XYZRHW 36, TEX2 30, TEX3 7, LASTBETA 9 | จำกัดชุด vertex layout ให้เหลือไม่กี่ชุด; XYZRHW (จุดในหน้าจอ) ต้องมี shader แบบ orthographic |
| Vertex blend (skinning) | VERTEXBLEND 24, SetVertexShaderConstant 54 / F 33 | skinning ใน shader ตามจำนวน bone |

### C. shader ที่เขียนเองและการคอมไพล์ตอนรัน (เสี่ยงสูง)
- `CreateVertexShader` 25, `CreatePixelShader` 7, `SetVertexShader` 33, `SetPixelShader` 16 อยู่ใน `enginelib` (24 ไฟล์) และ `LegacyDX_Include`
- `D3DXAssembleShader` 17 และ `D3DXAssembleShaderFromFile` 9 คอมไพล์ shader จากข้อความ **ตอนรัน** ใน bgfx ต้องคอมไพล์ล่วงหน้าด้วย `shaderc` ออฟไลน์ (รองรับ GLSL/ESSL/SPIR-V) ต้องเขียน shader ใหม่ทั้งชุด ไม่แปลจาก asm ได้ตรง ๆ
- `ID3DXEffect` 41 และ `D3DXCreateEffectFromFile` 5 (รวม `DxPostProcess.cpp`, `DXUTmisc.cpp`) ต้องหาว่าไฟล์ `.fx` ใดถูกใช้จริงและเตรียมแทนด้วย shader ของ bgfx
- `DXUTmisc.cpp` ใน `enginelib/DxCommon9/` มาจาก DirectX SDK sample framework ต้องเช็กไลเซนส์ของ DXUT แยก (ยังไม่ได้ตรวจ)

### D. ยูทิลิตี้ D3DX
| กลุ่ม | จำนวน (grep) | แนวทาง |
|---|---|---|
| Mesh (`ID3DXMesh` 21, `LPD3DXMESH` 238, `D3DXCreateMeshFVF` 17, `D3DXLoadMeshFromX` 7) | สูง | ตัว parser ไฟล์ `.x` เอง หรือแปลงข้อมูลเป็นฟอร์แมตของเราออฟไลน์ |
| Skinned mesh (`ID3DXSkinInfo` 8, `D3DXLoadSkinMeshFromXof` 2, `LPD3DXBONECOMBINATION` 48) | ปานกลาง | ย้ายไป skinning ใน shader |
| Normal/tangent/weld (`D3DXComputeNormals` 12, `D3DXComputeTangent` 4, `D3DXWeldVertices` 3) | ต่ำ | เขียนเอง หรือใช้ไลบรารี mesh ที่มีไลเซนส์ชัดเจน |
| Texture โหลดไฟล์ (`D3DXCreateTextureFromFile*` ~30, `D3DXGetImageInfoFromFile` 4, `D3DXFilterTexture` 4) | ต่ำ-ปานกลาง | นามสกุลที่พบ: `.bmp` 72, `.dds` 64, `.tga` 33, `.x` 10, `.jpg` 5 ใช้ตัวถอดรหัสภาพแบบ portable (ต้องตรวจไลเซนส์ก่อนเลือก) |
| บันทึกภาพ (`D3DXSaveTextureToFile` 11, `D3DXSaveSurfaceToFile` 4) | ต่ำ | ใช้ทำ screenshot ใน Windows อาจไม่จำเป็นบน Android |
| Sprite/line (`D3DXCreateSprite` 4, `D3DXCreateLine` 2) | ต่ำ | batching quad ของเราเอง |
| `D3DXGetFVFVertexSize` 163 | สูง แต่ง่าย | ฟังก์ชันคำนวณขนาดจาก FVF เขียนเองได้ทันที |
| PRT (`LPD3DXPRTBUFFER` 38) | **ไม่ได้ใช้ในโค้ดรันไทม์** พบเฉพาะในไฟล์ header ของ `LegacyDX_Include` | ไม่ต้องพอร์ต (ยืนยันด้วย trace) |

### E. ฟอนต์และข้อความ (GDI ปนกับ D3DX)
- `DrawText` ตรวจพบ 41 จุด ส่วนใหญ่ใน `enginelib` 21 จุด ทั้งแบบ GDI (`pDC->DrawText`, `DrawText(hdc, ...)`) และแบบ D3DX (`pD3dFont->DrawText` ซึ่งเป็น `ID3DXFont` 6 จุด, `D3DXCreateFont` 4 จุด)
- GDI ใช้ไม่ได้บน Android ต้องเขียนระบบฟอนต์ใหม่ (rasterize ด้วยไลบรารีฟอนต์ที่มีไลเซนส์ชัดเจน)

### F. ส่วนที่เป็นอุปกรณ์และระบบ (ไม่ใช่เรนเดอร์)
- `GetDeviceCaps` 61 → `bgfx::getCaps()` ข้อจำกัดของ GLES ต่างจาก D3D9 ต้องตรวจตอนเลือกระดับ
- `Reset` 4, `TestCooperativeLevel` 5 → Android ต้องจัดการ surface lost (onPause/onResume) ในชั้น platform
- `ShowCursor` 3, `SetCursorProperties` 2, `SetCursorPosition` 2 → เป็นงาน input/platform ไม่ใช่เรนเดอร์

### ข้อสรุปของ 3.0b
1. งานหลักไม่ใช่ D3D9 → bgfx ตรง ๆ แต่คือ fixed-function และ state block ที่ต้องจำลอง (กลุ่ม B) กับ shader ที่ต้องเขียนใหม่ (กลุ่ม C)
2. shader ไม่มีในซอร์สและ repo: ต้องได้จากชุดข้อมูลของผู้ใช้ ผู้ใช้ยืนยันว่ามีสิทธิ์แล้ว แต่ยังไม่ได้เก็บหลักฐานการสิทธิ์ไว้ใน repo
3. `D3DX` math (`D3DXVECTOR3` 8,560 ครั้ง, `D3DXMATRIX` 1,553 ครั้ง) ควรทำ shim header ที่มีชื่อเดิม เพื่อลดการแก้โค้ดจำนวนมาก
4. ก่อนประมาณงาน 3.2 ต้อง trace การเรียกจริงตอนเล่นบน Windows และนับแยกตามเมธอด

## ขั้นของเฟส 3 (ทำตามลำดับ)
1. **3.0 วิจัย (ทำได้ทันที):** ยืนยันไลเซนส์และเวอร์ชัน bgfx ที่จะใช้ (ไลเซนส์ bgfx ยืนยันแล้ว), เลือก commit ที่ตรึงไว้, ตรวจว่า build บน Android NDK ได้ (ต้องใช้ CI และยังไม่ทำในรอบนี้)
1a. **3.0b วิจัยการเรียก D3D9 (ทำแล้วระดับ grep):** จัดกลุ่มเป็น A–F ในหัวข้อ "ผลวิเคราะห์ 3.0b" ยังต้องทำ trace การเรียกจริงบน Windows ก่อนประมาณงาน 3.2
2. **3.1 ชั้นอินเทอร์เฟซ (ทำแล้ว):** `renderer/include/ran/render/` มี `IRenderer`, handle แบบมี generation, ขนาด vertex จาก FVF และ `NullRenderer` ทดสอบด้วย `make -C renderer test` (ไม่มี backend จริง)
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
