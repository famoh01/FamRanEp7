# tools/ep7_drive_importer/strings_import.py
# แปลงไฟล์ข้อความภาษาไทยจริงจาก EP7 (Google Drive ThaiPack) เป็น JSON ที่เซิร์ฟเวอร์ใช้
#
# ที่มา: RAN-EP7-ThaiPack-FamHawaii.zip (Drive file id 1IL8jlmvl5PGGHdaf5nlnoZdBMhTu25Ag)
#   data/gui/gameintext_th.txt   (ข้อความในเกม/UI 493 รายการ)
#   data/gui/gameextext_th.txt   (ข้อความหน้า login/สร้างตัวละคร 64 รายการ)
#   data/gui/gameword_th.txt     (คำศัพท์/หัวตาราง 81 รายการ)
#
# รูปแบบไฟล์ (ตรวจจากของจริง):
#   KIND,KEY,COUNT        <- 1 บรรทัด
#   ข้อความ COUNT บรรทัด
# เข้ารหัสเป็น cp874 (TIS-620 + Windows punctuation) — ไม่ได้ผ่าน compbyte/Rijndael
#
# ห้ามแก้ไฟล์ต้นฉบับใน Drive: โมดูลนี้อ่านอย่างเดียวแล้วเขียนผลลัพธ์ลง ran_mobile/Database/content/
import json
import re
from pathlib import Path

ENTRY_RE = re.compile(r"^([A-Z]+),([A-Za-z0-9_]+),(\d+)\s*$")


def parse_text_file(path: Path) -> dict:
    """อ่านไฟล์ข้อความ EP7 (cp874) คืน {key: [บรรทัด...]}"""
    raw = path.read_bytes()
    text = raw.decode("cp874", errors="replace")
    lines = text.splitlines()
    out = {}
    i = 0
    while i < len(lines):
        m = ENTRY_RE.match(lines[i])
        if not m:
            i += 1
            continue
        _kind, key, count = m.group(1), m.group(2), int(m.group(3))
        values = [v.strip() for v in lines[i + 1:i + 1 + count]]
        out[key] = values
        i += 1 + count
    return out


def import_strings(thaipack_dir: Path, out_dir: Path, meta: dict) -> dict:
    """แปลง 3 ไฟล์ข้อความเป็น JSON 3 ไฟล์ + index รวม; คืนสรุปจำนวนรายการ"""
    gui = thaipack_dir / "data" / "gui"
    sources = {
        "intext": gui / "gameintext_th.txt",
        "extext": gui / "gameextext_th.txt",
        "word": gui / "gameword_th.txt",
    }
    out_dir.mkdir(parents=True, exist_ok=True)
    merged = {}
    counts = {}
    for name, path in sources.items():
        if not path.is_file():
            raise FileNotFoundError(f"ไม่พบไฟล์ข้อความที่ต้องการ: {path}")
        entries = parse_text_file(path)
        payload = {
            "_source": {
                "drive_file": meta.get("drive_file"),
                "drive_file_id": meta.get("drive_file_id"),
                "path_in_zip": str(path.relative_to(thaipack_dir)).replace("\\", "/"),
                "encoding": "cp874",
                "commit": meta.get("commit"),
            },
            "count": len(entries),
            "entries": entries,
        }
        (out_dir / f"ep7_{name}_th.json").write_text(
            json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
        counts[name] = len(entries)
        for key, values in entries.items():
            # คีย์ซ้ำระหว่างไฟล์ให้คงค่าแรก (intext -> extext -> word)
            merged.setdefault(key, values)

    (out_dir / "ep7_strings_th.json").write_text(
        json.dumps({
            "_source": {"drive_file": meta.get("drive_file"),
                        "drive_file_id": meta.get("drive_file_id"),
                        "commit": meta.get("commit"),
                        "note": "รวมคีย์จาก 3 ไฟล์ข้อความ EP7 (ภาษาไทย) ค่าแรกชนะ"},
            "count": len(merged),
            "entries": merged,
        }, ensure_ascii=False, indent=1), encoding="utf-8")
    counts["merged"] = len(merged)
    return counts


# --- ตัวช่วยเลือกข้อความไปใช้ใน UI ของเรา ---
def first_line(entries: dict, key: str, default: str = "") -> str:
    vals = entries.get(key)
    return vals[0] if vals else default


if __name__ == "__main__":  # pragma: no cover - ใช้ผ่าน build_drive_ep7_data.py
    import argparse
    ap = argparse.ArgumentParser(description="แปลงไฟล์ข้อความ EP7 เป็น JSON")
    ap.add_argument("thaipack_dir", type=Path)
    ap.add_argument("out_dir", type=Path)
    a = ap.parse_args()
    print(import_strings(a.thaipack_dir, a.out_dir, {}))
