# EP7 Data Manifest (สถานะข้อมูลเกม EP7 ที่มีอยู่จริง)

วันที่ตรวจ: 2026-11-22. ไฟล์นี้บันทึกเฉพาะสิ่งที่ตรวจแล้วจริง: ขนาดไฟล์ (byte) และ header/เนื้อหา ไม่ได้นับจากชื่อไฟล์.

## สรุป

- **MISSING EP7 GAME DATA**: ไม่พบแผนที่ โมเดล texture ของเกม ไอเทม มอนสเตอร์ NPC สกิล เควส หรือฐานข้อมูลเกมที่ใช้ได้ ทั้งใน Workspace และใน Git tree ปัจจุบันของ `famoh01/ran-online-ep7` (ยังไม่ได้ไล่ประวัติ commit ของ upstream)
- Repository `famoh01/ran-online-ep7` (commit `4affa8f`) ตรวจแล้ว: **0 ไฟล์ Git LFS Pointer**, มีโค้ด C/C++ และไฟล์ UI bitmap เท่านั้น
- ZIP/RAR ของ Google Drive ที่พบ 9 ไฟล์: ตรวจได้เฉพาะ metadata (ชื่อ, ขนาดที่ Drive รายงาน, MIME) ยังไม่ได้ดาวน์โหลด เพราะยังไม่ยืนยันสิทธิ์ใช้งาน ดูหัวข้อ Drive
- Repository `matheomedrana/Ran-Online-EP7-Complete-Files`: **ไม่มี License (`license: null`) ไม่ได้ดึง LFS และไม่ใช้**

## MAP

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีไฟล์แผนที่ เช่น `.wld` `.lev` `.map`) | - | - | **MISSING EP7 GAME DATA** |

## MODEL

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีไฟล์โมเดล เช่น `.x` `.msh` `.pmf`) | - | - | **MISSING EP7 GAME DATA** |
| `=MAX4.2SDK` … `=MAX8.0SDK` (โฟลเดอร์ 3ds Max SDK) | หลายไฟล์ | SDK header/โค้ด | ไม่ใช่โมเดลเกม ไม่ใช้ |

## TEXTURE

ไฟล์ BMP ทั้งหมดใน repo (59 ไฟล์) เป็น UI/toolbar/ไอคอนของเครื่องมือ Windows และ launcher ไม่ใช่ texture ของเกม สถานะทุกไฟล์: *ตรวจ header แล้ว, ไม่ใช่ texture เกม, ไม่ใช้*. ไม่มีไฟล์ texture เกม (`.dds` `.tga` `.cps`) เลย

| filename | size | format | status |
|---|---|---|---|
| `AutoPatchMan/res/Dlg.bmp` | 540,056 B | BMP 600x300 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/ExitDn.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/ExitGray.bmp` | 3,838 B | BMP 119x23 8bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/ExitUp.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/HomepageDn.bmp` | 30,414 B | BMP 440x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/HomepageGray.bmp` | 11,198 B | BMP 440x23 8bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/HomepageUp.bmp` | 30,414 B | BMP 440x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/SettingDn.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/SettingGray.bmp` | 8,336 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/SettingUp.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/StartDn.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/StartGray.bmp` | 8,336 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/StartUp.bmp` | 8,334 B | BMP 119x23 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/dlg_dn.bmp` | 505,704 B | BMP 450x374 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/dlg_up.bmp` | 175,814 B | BMP 450x130 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/exit_new_blue.bmp` | 4,844 B | BMP 75x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/exit_new_gray.bmp` | 4,844 B | BMP 75x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/option_new_blue.bmp` | 4,844 B | BMP 76x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/option_new_gray.bmp` | 4,844 B | BMP 76x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/progressup.bmp` | 11,982 B | BMP 568x7 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/start_new_blue.bmp` | 4,760 B | BMP 74x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `AutoPatchMan/res/start_new_gray.bmp` | 4,760 B | BMP 74x21 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `BugTrap/res/Checkmark.bmp` | 146 B | BMP 8x7 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `BugTrap/res/ImageToolbar.bmp` | 598 B | BMP 64x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `BugTrap/res/SortArrows.bmp` | 174 B | BMP 14x7 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `Emulator/res/Toolbar.bmp` | 358 B | BMP 32x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `GLogicText/res/Toolbar.bmp` | 2,158 B | BMP 272x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `GLogicText/res/mg_icons.bmp` | 1,910 B | BMP 224x16 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `MinWebLauncher/MinWebLauncherCtrl.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `NpcAction/res/toolbar2.bmp` | 478 B | BMP 48x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `RanFileSystem/res/FileListIcon.bmp` | 374 B | BMP 32x16 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `RanOnlienInstaller/RanDownImage.bmp` | 210,594 B | BMP 483x145 24bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/bitmap1.bmp` | 478 B | BMP 48x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/bmp00001.bmp` | 478 B | BMP 48x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/bmp00002.bmp` | 3,190 B | BMP 48x128 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/bmp00003.bmp` | 358 B | BMP 21x20 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/bmp00004.bmp` | 358 B | BMP 21x20 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/leds.bmp` | 3,190 B | BMP 48x128 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixla.bmp` | 31,798 B | BMP 704x90 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixsetblue.bmp` | 31,798 B | BMP 704x90 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixsetsmallblue.bmp` | 17,014 B | BMP 512x66 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixsettinyblue.bmp` | 8,038 B | BMP 352x45 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixsm.bmp` | 17,014 B | BMP 512x66 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/matrixti.bmp` | 8,038 B | BMP 352x45 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pin1.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pin2.bmp` | 358 B | BMP 21x20 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pin3.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pin4.bmp` | 358 B | BMP 21x20 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pp_pinne.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/pp_unpin.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/state.bmp` | 1,846 B | BMP 48x16 8bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `ServerManager/res/statebit.bmp` | 1,846 B | BMP 48x16 8bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `SoundSourceManager/res/Toolbar.bmp` | 238 B | BMP 16x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `SoundSourceManager/res/state.bmp` | 374 B | BMP 32x16 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `VersionManager/res/Toolbar.bmp` | 1,198 B | BMP 144x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `VersionManager/res/toolbar1.bmp` | 2,230 B | BMP 264x16 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `XCrashReport/checkboxes.bmp` | 1,846 B | BMP 48x16 8bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `superpad/res/Itoolbar.bmp` | 718 B | BMP 80x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |
| `superpad/res/Toolbar.bmp` | 1,198 B | BMP 144x15 4bpp | UI bitmap (ไม่ใช่ texture เกม) |

## ITEM

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีข้อมูลไอเทมใน repo) | - | - | **MISSING EP7 GAME DATA** |
| `RAN-EP7-ItemText-Thai-Update.zip` (Drive) | 912,989 B (รายงานโดย Drive) | ZIP | ยังไม่ดาวน์โหลด ไม่ได้ตรวจ header ยังไม่ยืนยันสิทธิ์ |

## MOB

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีข้อมูลมอนสเตอร์ใน repo) | - | - | **MISSING EP7 GAME DATA** |

## NPC

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีข้อมูล NPC ใน repo; `NpcAction/` เป็นโค้ดโปรเจกต์) | - | - | **MISSING EP7 GAME DATA** |

## SKILL

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีข้อมูลสกิลใน repo) | - | - | **MISSING EP7 GAME DATA** |

## QUEST

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีข้อมูลเควสใน repo) | - | - | **MISSING EP7 GAME DATA** |

## DATABASE

| filename | size | format | status |
|---|---|---|---|
| (ไม่มีไฟล์ schema/seed/`.bak`/`.mdf` ของ EP7 ใน repo; โค้ด ODBC อยู่ใน `netserverLib/s_COdbc*.cpp` และ `ServerManager/COdbcManager.*`) | - | - | **MISSING EP7 GAME DATA** |
| `RAN-EP7-ServerDBTools.zip` (อยู่ใน release `ran-ep7-full-v1`) | ไม่ได้ตรวจขนาด | ZIP | ไม่ได้ดาวน์โหลด ไม่ใช้ |
| `ran_mobile/Database/schema.sql` (ของเราเอง ไม่ใช่ EP7) | ดูในไฟล์ | SQLite | ใช้งาน ข้อมูลตัวอย่างเขียนเอง ไม่ใช่ข้อมูลเกม |

## ไฟล์ Binary ใน repo (ไม่ใช่ข้อมูลเกม, ไม่ใช้กับพอร์ต)

| filename | size | format | status |
|---|---|---|---|
| `=DLL/CABARC.EXE` | 114,688 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=DLL/CABINET.DLL` | 65,536 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=DLL/EXTRACT.EXE` | 101,376 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=DLL/MAKECAB.EXE` | 152,576 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=DLL/ntwdblib.DLL` | 274,489 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=Library/ggauth.dll` | 86,016 B | PE x86 DLL | ไม่ใช้ (encryption/anti-cheat/SDK/เครื่องมือ, ดู LICENSE_RISKS) |
| `=Library/ggsrvdll.dll` | 57,344 B | PE x86 DLL | ไม่ใช้ (encryption/anti-cheat/SDK/เครื่องมือ, ดู LICENSE_RISKS) |
| `=Library/zlib1.dll` | 55,808 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=TbbLib/bin/tbb.dll` | 139,264 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=TbbLib/bin/tbb_debug.dll` | 159,744 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=TbbLib/bin/tbbmalloc.dll` | 37,376 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=TbbLib/bin/tbbmalloc_debug.dll` | 37,376 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=boost/lib/boost_regex-vc71-mt-1_34_1.dll` | 475,136 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `=boost/lib/boost_regex-vc71-mt-gd-1_34_1.dll` | 831,488 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `BugTrapServer/BugTrapSetup.exe` | 2,530,180 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `LegacyInclude/DaumCrypt.DLL` | 45,056 B | PE x86 DLL | ไม่ใช้ (encryption/anti-cheat/SDK/เครื่องมือ, ดู LICENSE_RISKS) |
| `MinWebLauncher/CodeSign/CABARC.EXE` | 114,688 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/Makecert.exe` | 34,576 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/cert2spc.exe` | 10,000 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/chktrust.exe` | 12,560 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/setreg.exe` | 56,592 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/signcode.exe` | 27,408 B | PE x86 EXE | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `MinWebLauncher/CodeSign/signer.dll` | 77,584 B | PE x86 DLL | ไม่ใช้ (เครื่องมือ/ไลบรารี Windows) |
| `netserverLib/EGameEncrypt.dll` | 36,864 B | PE x86 DLL | ไม่ใช้ (encryption/anti-cheat/SDK/เครื่องมือ, ดู LICENSE_RISKS) |
| `netserverLib/MyRossoEncrypt.dll` | 94,208 B | PE x86 DLL | ไม่ใช้ (encryption/anti-cheat/SDK/เครื่องมือ, ดู LICENSE_RISKS) |

## Google Drive (metadata อย่างเดียว ไม่ได้ดาวน์โหลด)

ค้นหาด้วย Google Drive connector (`search_files`, ไม่รวม trashed) พบ 9 ไฟล์ ขนาดในตารางคือค่าที่ Drive รายงาน ยังไม่ได้ตรวจ header เพราะต้องดาวน์โหลดก่อน และยังไม่ยืนยันสิทธิ์.

| filename | size (Drive) | format | หมวด | status |
|---|---|---|---|---|
| RanNeON.rar | 2,587,496,984 B | RAR | ไม่ทราบ (ใหญ่ 2.5 GB) | ไม่ดาวน์โหลด ไม่ใช้ สิทธิ์ไม่ทราบ |
| RAN_EP3_Mobile_WIP_2026-11-20.zip | 13,720,937 B | ZIP | ไม่ทราบ (EP3) | ไม่ดาวน์โหลด ยังไม่รู้ที่มา ต้องถามก่อน |
| RAN-EP7-ThaiPack-FamHawaii.zip | 2,266,660 B | ZIP | Thai pack (ข้อความ/UI) | ไม่ดาวน์โหลด (ผู้ใช้ปฏิเสธการนำมาใช้) |
| RAN-EP7-ItemText-Thai-Update.zip | 912,989 B | ZIP | ITEM (ข้อความไอเทม) | ไม่ดาวน์โหลด ยังไม่ยืนยันสิทธิ์ |
| Rantest | 36,788,397 B | APK (Android) | APK ตัวอย่าง | ไม่แตกไฟล์ ไม่ใช้ (ปฏิเสธแล้ว) |
| แปลงPC Ran Ep7เป็น Mobile (ZIP) | 23,891,871 B | ZIP | ไม่ทราบ | ไม่ดาวน์โหลด (PORT_STATUS: ตรวจได้เฉพาะ metadata) |
| คำสั่งแปลงเกมran เป็นมือถือ (PDF) | 200,204 B | PDF | เอกสาร | ไม่ได้อ่านในรอบนี้ |
| RAN-EP7-Install-ThaiPack-Tutorial_TH.mp4 | 5,227,389 B | MP4 | วิดีโอสอนติดตั้ง | ไม่ใช่ข้อมูลเกม ไม่ใช้ |
| RAN-EP7-Install-ThaiPack-Tutorial_MiniA_TH.mp4 | 5,229,412 B | MP4 | วิดีโอสอนติดตั้ง | ไม่ใช่ข้อมูลเกม ไม่ใช้ |

Drive mp4 `ให้ทำตามนี้.mp4` (`14Xr0Onn8vSTLV00hACFqCyCvqUAU0UFN`, 255,411,434 B) ไม่ได้ดาวน์โหลด เป็นคลิปเกมต้นฉบับ สิทธิ์ยังไม่ยืนยัน

## Git LFS

- `famoh01/ran-online-ep7`: 0 ไฟล์ LFS Pointer (ตรวจทั้ง tree ที่ commit `4affa8f`)
- `matheomedrana/Ran-Online-EP7-Complete-Files`: ไม่ได้ดึง ไม่มี License จึงยืนยันสิทธิ์ไม่ได้ (LFS ยังไม่ตรวจ)

## สิ่งที่ต้องได้รับเพิ่มเพื่อปลดบล็อก Phase 4–6

1. ไฟล์แผนที่ (`.wld`/`.lev` หรือเทียบเท่า) และไฟล์โมเดล/texture ของเกม (`.x` `.dds` `.cps` ฯลฯ)
2. ข้อมูลไอเทม มอนสเตอร์ NPC สกิล เควส (`.genitem` `.qst` หรือไฟล์ข้อความ/CSV/SQL ที่ export จากเซิร์ฟเวอร์)
3. ฐานข้อมูลเซิร์ฟเวอร์ EP7 (`.bak` หรือ `.sql`) เพื่อใช้เป็นต้นแบบ schema
4. **หลักฐานสิทธิ์ใช้งาน** ของไฟล์ทั้งหมดข้างต้น (ใบอนุญาต หรือหนังสือยืนยันจากเจ้าของลิขสิทธิ์). ถ้าไม่มี จะไม่นำมาใช้

## สถานะการนำมาใช้

ยังไม่มีไฟล์ EP7 ที่มีสิทธิ์ใช้งานได้จริง จึงยังไม่มี Converter ของ Map/Model/Texture/Item/Mob/NPC/Skill/Quest. งาน EP7 Data หยุดอยู่ที่จุดนี้ ส่วนของ Android (Phase 2) ทำต่อได้
