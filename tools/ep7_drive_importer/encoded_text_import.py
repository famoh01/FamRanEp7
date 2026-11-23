# tools/ep7_drive_importer/encoded_text_import.py
# ดึง "ข้อความ" จากไฟล์ข้อมูล EP7 ที่เข้ารหัส (compbyte) ได้แก่ .ntk (บทสนทนา NPC) และ .qst (เควสต์)
#
# วิธีถอด: compbyte::decode (parity substitution) แล้ว読みข้อความ ASCII ที่ฝังอยู่
# ไฟล์กลุ่มนี้เก็บค่าตัวเลขเป็นไบนารีผสมข้อความ จึงไม่ parse เป็นฟิลด์ — ดึงเฉพาะข้อความ
# ซึ่งเป็นข้อมูลจริงจากไฟล์ต้นฉบับ (ไม่แต่งขึ้นเอง) และใช้เป็นบทสนทนา/คำอธิบายในเกมมือถือ
import json
import re
from pathlib import Path

import ep7_v5

PRINTABLE_RUN = re.compile(rb"[\x20-\x7E]{8,}")
# กรองข้อความที่ไม่ใช่บทสนทนา (พาธไฟล์/ชื่อไฟล์โมเดล/คำสั่งเครื่อง)
NOISE = re.compile(r"(\.(dds|cps|chf|x|egp|abl|aps|bmp|tga|wav|mtf))|(^[A-Za-z]:\\)|(^\\)", re.I)


# ข้อความจริงในไฟล์ EP7 เป็นภาษาอังกฤษ: ต้องมีคำที่ประกอบด้วยตัวอักษร >= 3 ตัวติดกัน
# และต้องมีช่องว่างอย่างน้อย 1 ที่ (กันสตริงไบนารีที่บังเอิญพิมพ์ได้ เช่น "-Q=NRARC:.")
WORD_RE = re.compile(r"[A-Za-z]{3,}")


def is_sentence(text: str) -> bool:
    if text.count(" ") < 1:
        return False
    words = WORD_RE.findall(text)
    if len(words) < 2:
        return False
    letters = sum(len(c) for c in text if c.isalpha())
    return letters >= 0.65 * len(text) and text.count("=") + text.count('"') <= 2


def extract_strings(path: Path, min_len: int = 10, limit: int = 60):
    """คืนรายการข้อความที่อ่านได้จากไฟล์เข้ารหัส (เรียงตามตำแหน่งในไฟล์)"""
    buf = ep7_v5.decode(path.read_bytes())
    out = []
    for m in PRINTABLE_RUN.finditer(buf):
        text = m.group().decode("ascii", errors="ignore").strip()
        if len(text) < min_len or NOISE.search(text):
            continue
        if not is_sentence(text):
            continue
        out.append(text)
        if len(out) >= limit:
            break
    return out


def import_npc_talk(thaipack_dir: Path, out_path: Path, meta: dict) -> dict:
    src = thaipack_dir / "data" / "glogic" / "npctalk"
    files = sorted(src.glob("*.ntk")) if src.is_dir() else []
    if not files:
        raise FileNotFoundError(f"ไม่พบไฟล์บทสนทนา NPC (.ntk) ใน {src}")
    npcs = []
    for f in files:
        lines = extract_strings(f, limit=24)
        if not lines:
            continue
        npcs.append({
            "source_file": f.name,
            "npc_key": f.stem,
            "lines": lines,
        })
    payload = {
        "_source": {
            "drive_file": meta.get("drive_file"),
            "drive_file_id": meta.get("drive_file_id"),
            "commit": meta.get("commit"),
            "path_in_zip": "data/glogic/npctalk/*.ntk",
            "encoding": "compbyte (256-byte substitution จาก CompByte.cpp)",
            "files": len(files),
            "note": "ดึงเฉพาะข้อความ ASCII ที่อ่านได้จากไฟล์ต้นฉบับ ไม่ได้แต่งบทสนทนาเพิ่ม",
        },
        "count": len(npcs),
        "npcs": npcs,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    return {"files": len(files), "with_text": len(npcs),
            "lines": sum(len(n["lines"]) for n in npcs)}


def import_quests(thaipack_dir: Path, out_path: Path, meta: dict) -> dict:
    src = thaipack_dir / "data" / "glogic" / "quest"
    files = sorted(src.glob("*.qst")) if src.is_dir() else []
    if not files:
        raise FileNotFoundError(f"ไม่พบไฟล์เควสต์ (.qst) ใน {src}")
    quests = []
    for f in files:
        lines = extract_strings(f, limit=12)
        if not lines:
            continue
        quests.append({"source_file": f.name, "quest_key": f.stem, "text": lines})
    payload = {
        "_source": {
            "drive_file": meta.get("drive_file"),
            "drive_file_id": meta.get("drive_file_id"),
            "commit": meta.get("commit"),
            "path_in_zip": "data/glogic/quest/*.qst",
            "encoding": "compbyte (256-byte substitution)",
            "files": len(files),
            "note": "ดึงเฉพาะข้อความที่อ่านได้จากไฟล์เควสต์ต้นฉบับ",
        },
        "count": len(quests),
        "quests": quests,
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")
    return {"files": len(files), "with_text": len(quests),
            "lines": sum(len(q["text"]) for q in quests)}
