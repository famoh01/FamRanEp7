# tools/ep7_drive_importer/round4_source_import.py
# นำเข้าข้อมูลจริงจากไฟล์ Google Drive ที่เปิดอ่านได้จริง (ThaiPack) สำหรับรอบ 4
#
# ไฟล์ที่ตรวจแล้วว่า "อ่านได้จริง" และสิ่งที่ดึงออกมาได้:
#   1) data/gui/gameintext_th.txt, gameextext_th.txt, gameword_th.txt  — ข้อความ UI ไทย (cp874, plain)
#      โครงสร้าง: SENTENSE,<KEY>,<n>  ตามด้วยบรรทัดข้อความ  (WORD,... สำหรับคำสั้น)
#      -> ข้อความแชท/ปาร์ตี้/ร้านค้า/วาร์ป ที่ใช้จริงใน Client EP7
#      ตัวเลขที่ตรวจกับไฟล์จริง: 738 คู่คีย์ · 708 บรรทัดมีอักษรไทย · 707 คีย์ไม่ซ้ำ
#   2) data/glogic/quest/*.qst (730 ไฟล์) — ไฟล์ตาราง มีหัว "default" + NUL
#      เนื้อหาไม่ใช่ plain text (ตรวจแล้ว: XOR/เลื่อนแบบคงที่ไม่ได้, entropy สูง) จึง "ไม่" ใช้เป็นตัวเลข
#   3) data/glogic/npctalk/*.ntk (309 ไฟล์) — โครงสร้างไบนารี ไม่มีบทสนทนาแบบข้อความ
#   4) data/glogic/ItemStrtable.txt / SearchItemList.txt — เข้ารหัส ตรวจแล้วถอดไม่ได้ด้วยคีย์ v5
#   5) data/glogic/npcshop.ntk / itemexchange.ntk — โครงสร้างร้านค้าแบบไบนารี ไม่มีชื่อ/ราคาเป็นข้อความ
#
# สคริปต์นี้จึงนำเข้าเฉพาะสิ่งที่ตรวจสอบได้ว่าเป็นข้อความจริง และรายงานสิ่งที่ยังไม่ได้
# ไม่สร้างตัวเลข/ชื่อขึ้นเอง
import json
import re
from pathlib import Path

THAI_MIN, THAI_MAX = "\u0e00", "\u0e7f"
LINE_RE = re.compile(r"^(SENTENSE|WORD),([A-Z0-9_]+),(\d+)\s*$")

# คีย์ที่ระบบมือถือรอบ 4 ใช้ (ตัดมาจากไฟล์จริงเท่านั้น)
WANTED = {
    # ร้านค้า
    "BUYITEM_NOMONEY", "BUYITEM_NOSPACE", "BUY_ITEM_CONFIRM", "SELL_ITEM_CONFIRM",
    "BUY_PILE_ITEM", "BUYITEM_MAXIMUM", "EMNPC_ITEM_TRADE_SUCCEED",
    # วาร์ป / แมพ
    "MAPMOVE_MESSAGE", "MAPMOVE_OKBUTTON", "MAP_REQUIRE_FAIL", "EMREQUIRE_LEVEL",
    "LARGEMAP_WINDOW_STATIC", "EMCONFRONT_END_MOVEMAP",
    # ปาร์ตี้ / แชท
    "CHAT_PARTY_MODE", "CHAT_PRIVATE_MODE", "CHAT_ERROR_NOPARTY", "PARTY_QUESTION",
    "PARTY_TAR", "PARTY_MODAL_TITLE", "PARTY_MODAL_OPTION", "QUIT_PARTY", "PARTY_NAME_STATIC",
    "PARTY_LURE_REFUSE", "PARTY_LURE_FULL", "PARTY_LURE_OTHER",
    # เลเวล
    "LEVELUP",
}


def thai_ratio(text: str) -> float:
    if not text:
        return 0.0
    thai = sum(1 for c in text if THAI_MIN <= c <= THAI_MAX)
    return thai / len(text)


def parse_ui_file(path: Path) -> dict:
    """อ่าน SENTENSE/WORD -> ข้อความ ภาษาไทย (cp874) จากไฟล์ GUI จริง"""
    text = path.read_bytes().decode("cp874", "replace")
    lines = text.splitlines()
    out = {}
    for i, line in enumerate(lines[:-1]):
        m = LINE_RE.match(line.strip())
        if not m:
            continue
        value = lines[i + 1].strip()
        # เอาเฉพาะบรรทัดที่เป็นข้อความจริง (มีอักษรไทย) ไม่ใช่ค่าตัวเลข/รหัส
        if thai_ratio(value) < 0.25:
            continue
        out[f"{m.group(1)},{m.group(2)}"] = value
    return out


def build(extract_dir: Path, out_path: Path) -> dict:
    gui = extract_dir / "data" / "gui"
    merged = {}
    per_file = {}
    for name in ("gameintext_th.txt", "gameextext_th.txt", "gameword_th.txt"):
        p = gui / name
        if not p.is_file():
            per_file[name] = 0
            continue
        parsed = parse_ui_file(p)
        per_file[name] = len(parsed)
        for k, v in parsed.items():
            merged.setdefault(k, v)

    wanted = {k: merged[k] for k in sorted(merged) if k.split(",")[-1] in WANTED}

    payload = {
        "_source": {
            "drive": "Google Drive / RAN-EP7-ThaiPack-FamHawaii.zip",
            "paths": ["data/gui/gameintext_th.txt", "data/gui/gameextext_th.txt",
                      "data/gui/gameword_th.txt"],
            "encoding": "cp874 (Windows-874)",
            "format": "KEY,VALUE table: 'SENTENSE,<KEY>,<n>' or 'WORD,<KEY>,<n>' then the text line",
            "note": "ข้อความจริงจากไฟล์เท่านั้น; ไม่มีตัวเลข/ชื่อที่สร้างขึ้นเอง",
        },
        "counts": {"total": len(merged), "wanted": len(wanted), "per_file": per_file},
        "wanted": wanted,
        "all": merged,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    return {"total": len(merged), "wanted": len(wanted), "per_file": per_file}


if __name__ == "__main__":
    import sys
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("google_drive/thaipack_extract")
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(
        "ran_mobile/Database/content/ep7/ep7_round4_ui_th.json")
    print(json.dumps(build(src, dst), ensure_ascii=False, indent=1))
