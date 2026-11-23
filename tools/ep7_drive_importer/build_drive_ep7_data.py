# tools/ep7_drive_importer/build_drive_ep7_data.py
# ตัวสั่งงานแปลงข้อมูลจริงจาก Google Drive (EP7 ThaiPack) เป็น JSON ที่เซิร์ฟเวอร์ใช้
#
# หลักการ:
#   * อ่านไฟล์ต้นฉบับอย่างเดียว (ไม่แก้/ไม่ลบ) — ไฟล์อยู่ใน /tmp หรือ ./google_drive เท่านั้น
#   * ทุกไฟล์ผลลัพธ์มี "_source" บอก Drive file id + path ในซิป + วิธีถอดรหัส
#   * ถ้าไฟล์ต้นฉบับไม่ครบ ให้ล้มเหลวพร้อมบอกไฟล์ที่ขาด (ไม่แต่งข้อมูลแทน)
import argparse
import hashlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import encoded_text_import as ET          # noqa: E402
import strings_import as SI               # noqa: E402

# ข้อมูลอ้างอิงของไฟล์ต้นฉบับ (ตรวจจาก Drive API — ดู docs/drive-source-manifest.md)
DRIVE_SOURCES = {
    "thaipack": {
        "drive_file": "RAN-EP7-ThaiPack-FamHawaii.zip",
        "drive_file_id": "1IL8jlmvl5PGGHdaf5nlnoZdBMhTu25Ag",
        "commit": "n/a (Drive file, ไม่ใช่ git)",
    },
    "itemtext": {
        "drive_file": "RAN-EP7-ItemText-Thai-Update.zip",
        "drive_file_id": "14Cjnc3WPpTAi09gIDa78fldph7xu0T6h",
        "commit": "n/a (Drive file, ไม่ใช่ git)",
    },
}


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def build(thaipack: Path, out_dir: Path) -> dict:
    meta = DRIVE_SOURCES["thaipack"]
    report = {"sources": meta, "outputs": []}

    # 1) ข้อความ UI ภาษาไทย (cp874, อ่านได้ตรง)
    counts = SI.import_strings(thaipack, out_dir, meta)
    report["strings"] = counts

    # 2) ข้อความเควสต์จริงจากไฟล์ .qst ที่เข้ารหัส compbyte
    q = ET.import_quests(thaipack, out_dir / "ep7_quests_source.json", meta)
    report["quests"] = q

    # 3) รายชื่อบทสนทนา NPC (ไฟล์ .ntk) — เก็บเท่าที่ต้นฉบับมีข้อความจริง
    n = ET.import_npc_talk(thaipack, out_dir / "ep7_npctalk_source.json", meta)
    report["npctalk"] = n

    for p in sorted(out_dir.glob("ep7_*.json")):
        report["outputs"].append({
            "file": p.name,
            "bytes": p.stat().st_size,
            "sha256": _sha256(p),
        })

    report["summary"] = {
        "ui_strings": counts.get("merged", 0),
        "quest_files_with_text": q["with_text"],
        "quest_text_lines": q["lines"],
        "npc_files_with_text": n["with_text"],
    }
    (out_dir / "ep7_import_report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=1), encoding="utf-8")
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description="แปลงข้อมูล EP7 จาก Drive เป็น JSON ของเซิร์ฟเวอร์")
    ap.add_argument("--thaipack", type=Path, required=True,
                    help="โฟลเดอร์ที่แตก RAN-EP7-ThaiPack-FamHawaii.zip แล้ว")
    ap.add_argument("--out", type=Path,
                    default=Path(__file__).resolve().parents[2] / "ran_mobile" / "Database" / "content" / "ep7",
                    help="โฟลเดอร์ผลลัพธ์ (ค่าเริ่มต้น: ran_mobile/Database/content/ep7)")
    a = ap.parse_args(argv)
    if not a.thaipack.is_dir():
        print(f"ไม่พบโฟลเดอร์ต้นฉบับ: {a.thaipack}", file=sys.stderr)
        return 2
    rep = build(a.thaipack, a.out)
    print(json.dumps(rep["summary"], ensure_ascii=False, indent=1))
    print("ผลลัพธ์:", a.out)
    for o in rep["outputs"]:
        print(f"  {o['file']:<32} {o['bytes']:>9} B")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
