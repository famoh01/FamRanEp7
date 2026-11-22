# RAN Online EP7 — แผนที่ซอร์ส (Phase 0)

แหล่งที่มา: `famoh01/ran-online-ep7` tag `ran-ep7-full-v1` (commit `4affa8f`)
ขนาดซอร์สหลังแตก ~190 MB, ไฟล์ทั้งหมด 9,829 ไฟล์ (รวมไลบรารีภายนอก)

## โครงสร้างหลัก

| โมดูล | จำนวนไฟล์ | บทบาท (อนุมานจากชื่อและ README) |
|---|---|---|
| `enginelib` | 731 | เอนจินกราฟิก D3D9 และฟิสิกส์พื้นฐาน (จุดที่เรียก D3D9 มากที่สุด) |
| `RanClientUILib` | 467 | UI ฝั่งไคลเอนต์ (หน้าต่างสกิล, กระเป๋า, ร้านค้า ฯลฯ) |
| `RanClientLib` | 265 | ตรรกะไคลเอนต์ (ตัวละคร, การเดิน, การต่อสู้) |
| `EngineUILib` | 95 | เครื่องมือ UI ของเอนจิน |
| `EngineSoundLib`, `SoundSourceManager`, `Vorbislib` | 29 + 30 + 245 | เสียง (DirectSound) และ Ogg Vorbis |
| `netclientLib`, `netserverLib` | 39 + 146 | เครือข่าย (Winsock) |
| `GLogicText` | 41 | ข้อความเกม/ตารางข้อมูล |
| `GameServer`, `LoginServer`, `AgentServer`, `FieldServer`, `SessionServer`, `BoardServer`, `ServerMessanger`, `ServerController`, `ServerManager` | รวม ~200 | ฝั่งเซิร์ฟเวอร์ |
| `NpcAction`, `Emulator`, `MMPC`, `superpad`, `xskinexp`, `TexEncrypt` | 34–66 | เครื่องมือผู้พัฒนา/แอสเซ็ต |
| `AutoPatchMan`, `PatchPrimeMan`, `VersionManager`, `VerMan`, `MinWebLauncher`, `RanOnlienInstaller` | 16–121 | ตัวอัปเดต, launcher, installer (Windows) |
| `BugTrap`, `BugTrapServer`, `CrashExplorer`, `ErrorReport`, `XCrashReport`, `ReportFiles` | 11–117 | ระบบรายงานข้อผิดพลาด (Windows) |
| `RanFileSystem`, `minLib`, `Basic`, `MfcExLib` | 15–87 | ไฟล์ระบบ, MFC helper |

## ไลบรารีภายนอก (โฟลเดอร์ที่ขึ้นต้นด้วย `=`)

- `=boost` (3,769 ไฟล์), `=ZLIB`, `=TbbLib`, `=DX_Lib`, `=LegacyDX_Include`, `=MAX4.2SDK` … `=MAX8.0SDK` (SDK 3ds Max สำหรับปลั๊กอิน)
- `=Library` มีไลบรารีป้องกันโกง GameGuard (`ggauth.dll`, `ggsrvdll.dll`, `NPGameLib*.lib`) และ `DaumCrypt`
- `=DLL` มี `ntwdblib.DLL`, `CABINET.DLL`, `CABARC.EXE` (ไม่ได้ใช้ในเส้นทางมือถือ)

## ระบบ build

- `Smtm2022.sln` (VS2022, 31 project) และ `Smtm.sln` (รุ่นเก่า)
- `.github/workflows/build.yml`: `windows-latest`, MSBuild, Win32, configuration เลือกได้ (Release/Debug/Release_Thailand ฯลฯ) อัปโหลด artifact
- ไม่มี CMake หรือ Gradle ยังต้องสร้างทั้งหมดในเฟส 2

## ข้อสังเกตจากการอ่านโครงสร้าง

1. ชั้น platform ไม่ได้แยกออก โค้ดเรียก Win32/D3D9 กระจายอยู่หลายโมดูล
2. เซิร์ฟเวอร์ใช้ ODBC (`sql/include` ต้องคัดลอกจาก Windows SDK) ผูกกับ Windows
3. ระบบป้องกันโกง (GameGuard) ฝังอยู่ในไคลเอนต์และเซิร์ฟเวอร์ ดู `LICENSE_RISKS.md`
4. README ของซอร์สระบุว่าเป็นโค้ดที่ `Min Communications` เป็นเจ้าของลิขสิทธิ์ และแจกเพื่อการศึกษา
