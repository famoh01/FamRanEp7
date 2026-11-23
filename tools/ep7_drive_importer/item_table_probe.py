# tools/ep7_drive_importer/item_table_probe.py
# ทดลองหาชั้นเข้ารหัสของ data/glogic/ItemStrtable.txt จาก Drive (ไฟล์จริงจาก ThaiPack)
# เป้าหมาย: ยืนยันว่าไฟล์ถูกเข้ารหัสด้วยอะไร และถ้าถอดได้ ให้ดึง "รหัสไอเทม + ชื่อ" ออกมา
# ไฟล์นี้เป็นเครื่องมือตรวจสอบ ไม่แต่งข้อมูล: ถ้าถอดไม่ได้จะรายงานว่าไม่ได้ (ไม่เดาค่า)
import base64
import hashlib
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aes_ecb  # noqa: E402
import ep7_v5  # noqa: E402


def key_from_seed(seed: bytes) -> bytes:
    return base64.b64encode(hashlib.sha256(seed).digest())[:32]


KEYS = {
    "v5_short": key_from_seed(b"R!E@d#i^t*_RIJNDAEL"),
    "v5_full": key_from_seed(b"R!E@d#i^t*_005_20171011_RIJNDAEL"),
}


def looks_like_item_table(buf: bytes, encoding="cp874") -> float:
    """คะแนน 0..1 ว่า buf หน้าตาเป็นตารางข้อความ (ตัวอักษร/ตัวเลข/ตัวคั่น)"""
    if not buf:
        return 0.0
    text = buf[:2048].decode(encoding, "replace")
    ok = sum(1 for c in text if c.isalnum() or c in " \r\n\t,=_[]():;.-/")
    return ok / max(1, len(text[:2048]))


def probe(path: Path) -> dict:
    data = path.read_bytes()
    report = {"file": path.name, "size": len(data), "header": data[:8].hex(), "attempts": []}
    variants = {"raw": data, "skip4": data[4:],
                "compbyte": ep7_v5.decode(data)}
    for vname, buf in variants.items():
        score = looks_like_item_table(buf)
        report["attempts"].append({"layer": vname, "text_score": round(score, 3),
                                   "head": buf[:32].hex()})
        for kname, key in KEYS.items():
            body = buf[:len(buf) // 16 * 16]
            aes = aes_ecb.AES(key)
            # one block is enough to see whether the plaintext looks like a table header
            out = aes.decrypt_ecb(body[:64])
            report["attempts"].append({"layer": f"{vname}/aes-{kname}",
                                       "text_score": round(looks_like_item_table(out), 3),
                                       "head": out[:32].hex()})
    return report


def main(argv):
    paths = [Path(p) for p in argv[1:]] or [
        Path("google_drive/thaipack_extract/data/glogic/ItemStrtable.txt"),
        Path("google_drive/thaipack_extract/data/glogic/SearchItemList.txt"),
    ]
    for p in paths:
        if not p.exists():
            print(f"{p}: NOT FOUND")
            continue
        r = probe(p)
        best = max(r["attempts"], key=lambda a: a["text_score"])
        print(f"{r['file']}: size={r['size']} header={r['header']}")
        print(f"  best text_score={best['text_score']} via {best['layer']}")
        for a in r["attempts"]:
            print(f"    {a['layer']:28s} score={a['text_score']:<5} head={a['head'][:32]}")


if __name__ == "__main__":
    main(sys.argv)
