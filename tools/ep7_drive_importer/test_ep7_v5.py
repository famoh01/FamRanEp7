# tools/ep7_drive_importer/test_ep7_v5.py
# เทสต์ของตัวแปลงข้อมูล EP7 ที่ดึงจาก Google Drive จริง
#   python3 -m unittest discover -s tools/ep7_drive_importer -p "test_*.py" -t tools/ep7_drive_importer
#
# ครอบคลุม: ตาราง compbyte, AES-128 ECB (test vector FIPS-197 §C.3), ตัวแปลงข้อความ UI ไทย
# และข้อเท็จจริงที่ตรวจกับไฟล์จริงแล้ว (ไฟล์ต้นฉบับของ Drive ไม่ถูกแก้)
import hashlib
import string
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import aes_ecb          # noqa: E402
import ep7_v5           # noqa: E402
import round4_source_import as r4   # noqa: E402

REPO = HERE.parents[1]
THAIPACK = REPO / "google_drive" / "thaipack_extract"
UI_JSON = REPO / "ran_mobile" / "Database" / "content" / "ep7" / "ep7_round4_ui_th.json"


class CompByteTests(unittest.TestCase):
    """ตาราง compbyte ต้องเป็น permutation และย้อนกลับได้ (คุณสมบัติทางคณิตศาสตร์)"""

    def test_table_is_a_permutation(self):
        self.assertEqual(len(ep7_v5.ARRAY), 256)
        self.assertEqual(len(set(ep7_v5.ARRAY)), 256)
        self.assertEqual(set(ep7_v5.ARRAY), set(range(256)))

    def test_decode_then_encode_is_identity(self):
        data = bytes(range(256))
        self.assertEqual(ep7_v5.encode(ep7_v5.decode(data)), data)

    def test_encode_then_decode_is_identity(self):
        data = bytes(range(256))
        self.assertEqual(ep7_v5.decode(ep7_v5.encode(data)), data)

    def test_decode_is_not_the_identity(self):
        # ถ้าเป็น identity แปลว่าไม่ได้โหลดตารางจริงมา
        self.assertNotEqual(ep7_v5.decode(bytes(range(256))), bytes(range(256)))

    def test_decode_handles_empty_and_binary(self):
        self.assertEqual(ep7_v5.decode(b""), b"")
        blob = bytes([0x00, 0xFF, 0x80, 0x7F])
        self.assertEqual(ep7_v5.decode(blob), ep7_v5.decode(blob))   # ไม่โยน exception


class AesTests(unittest.TestCase):
    """AES ต้องตรงกับ test vector มาตรฐาน — ถ้าตรงแปลว่าใช้ถอดไฟล์ v5 ได้จริง"""

    def test_fips197_c3_aes128_ecb(self):
        # FIPS-197 §C.3: key 000102...0F, plaintext 00112233445566778899AABBCCDDEEFF
        key = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
        plain = bytes.fromhex("00112233445566778899aabbccddeeff")
        want = bytes.fromhex("69c4e0d86a7b0430d8cdb78070b4c55a")
        self.assertEqual(aes_ecb.AES(key).encrypt_ecb(plain), want)
        self.assertEqual(aes_ecb.AES(key).decrypt_ecb(want), plain)

    def test_roundtrip_random_blocks(self):
        key = hashlib.sha256(b"roundtrip").digest()[:16]
        aes = aes_ecb.AES(key)
        for i in range(16):
            block = hashlib.sha256(bytes([i])).digest()[:16]
            self.assertEqual(aes.decrypt_ecb(aes.encrypt_ecb(block)), block)

    def test_key_sizes_128_192_256(self):
        for n in (16, 24, 32):
            aes = aes_ecb.AES(bytes(range(n)))
            block = bytes(range(16))
            self.assertEqual(len(aes.encrypt_ecb(block)), 16)
            self.assertEqual(aes.decrypt_ecb(aes.encrypt_ecb(block)), block)


class UiStringParseTests(unittest.TestCase):
    """ตัวแยกไฟล์ GUI จริง: 'SENTENSE,<KEY>,<n>' แล้วบรรทัดถัดไปเป็นข้อความ"""

    def _parse(self, text: str):
        # เขียนไฟล์ชั่วคราวเป็น cp874 แล้วเรียกตัวแยกจริง
        import tempfile
        with tempfile.NamedTemporaryFile("wb", suffix=".txt", delete=False) as f:
            f.write(text.encode("cp874"))
            path = Path(f.name)
        try:
            return r4.parse_ui_file(path)
        finally:
            path.unlink()

    def test_parses_sentence_and_word(self):
        out = self._parse(
            "SENTENSE,BUYITEM_NOMONEY,1\nเงินไม่เพียงพอ!\n"
            "WORD,QUIT_PARTY,1\nยุบ,ออกจากปาร์ตี้\n")
        self.assertEqual(out["SENTENSE,BUYITEM_NOMONEY"], "เงินไม่เพียงพอ!")
        self.assertEqual(out["WORD,QUIT_PARTY"], "ยุบ,ออกจากปาร์ตี้")

    def test_drops_lines_without_thai(self):
        # บรรทัดที่เป็นตัวเลข/รหัส ไม่ใช่ข้อความที่ผู้เล่นเห็น
        out = self._parse("SENTENSE,SOME_KEY,1\n12345\nSENTENSE,REAL_KEY,1\nข้อความจริง\n")
        self.assertNotIn("SENTENSE,SOME_KEY", out)
        self.assertIn("SENTENSE,REAL_KEY", out)

    def test_ignores_malformed_header(self):
        out = self._parse("NOTAHEADER,FOO,1\nข้อความ\nSENTENSE,OK_KEY,1\nข้อความไทย\n")
        self.assertNotIn("SENTENSE,NOTAHEADER", out)
        self.assertEqual(out["SENTENSE,OK_KEY"], "ข้อความไทย")

    def test_thai_ratio_maths(self):
        self.assertAlmostEqual(r4.thai_ratio("ไทย"), 1.0)
        self.assertEqual(r4.thai_ratio("abcd"), 0.0)
        self.assertEqual(r4.thai_ratio(""), 0.0)


@unittest.skipUnless(THAIPACK.is_dir(), "ไม่พบไฟล์ Drive ที่แตกไว้ (google_drive/thaipack_extract)")
class RealDriveFileTests(unittest.TestCase):
    """ตรวจกับไฟล์จริงที่ดาวน์โหลดมา — ตัวเลขต้องตรงกับที่รายงานไว้ ไม่ใช่ค่าที่เดา"""

    def test_gui_files_exist_and_are_cp874(self):
        gui = THAIPACK / "data" / "gui"
        for name in ("gameintext_th.txt", "gameextext_th.txt", "gameword_th.txt"):
            p = gui / name
            self.assertTrue(p.is_file(), name)
            text = p.read_bytes().decode("cp874", "replace")
            self.assertIn("SENTENSE,", text)

    def test_counts_match_the_documented_numbers(self):
        gui = THAIPACK / "data" / "gui"
        counts = {}
        for name in ("gameintext_th.txt", "gameextext_th.txt", "gameword_th.txt"):
            counts[name] = len(r4.parse_ui_file(gui / name))
        # ตัวเลขที่ยืนยันแล้ว (docs/drive-source-manifest.md §2.1)
        self.assertEqual(counts["gameintext_th.txt"], 485)
        self.assertEqual(counts["gameextext_th.txt"], 66)
        self.assertEqual(counts["gameword_th.txt"], 157)
        # 708 คู่ที่มีอักษรไทย; รวมข้ามไฟล์แล้วเหลือ 707 คีย์ (มี 1 คีย์ซ้ำกัน)
        self.assertEqual(sum(counts.values()), 708)
        merged = set()
        for name in counts:
            merged |= set(r4.parse_ui_file(gui / name))
        self.assertEqual(len(merged), 707)

    def test_quest_bodies_are_not_thai_text(self):
        """ไฟล์จริงที่มีข้อความไทย ต้องมีสัดส่วนไบต์ไทยสูง; เนื้อใน .qst ต้องต่ำกว่านั้นมาก

        ไบต์ไทยใน CP874 คือ 0xA1–0xFB. ข้อความไทยจริงจะมีสัดส่วนนี้สูงมาก (ทดสอบเทียบกับ
        ไฟล์ GUI จริงด้านล่าง) ส่วนเนื้อใน .qst ที่ compbyte decode แล้วกลับ *ต่ำกว่าค่าคาดหมาย
        แบบสุ่ม* (ข้อมูลมีโครงสร้าง ไม่ใช่ข้อความ) จึงยืนยันว่าใช้เป็นบทสนทนาไม่ได้
        """
        real_thai = (THAIPACK / "data" / "gui" / "gameintext_th.txt").read_bytes()
        real_ratio = sum(1 for b in real_thai if 0xA1 <= b <= 0xFB) / len(real_thai)
        self.assertGreater(real_ratio, 0.30, "ไฟล์ GUI จริงต้องมีไบต์ไทยหนาแน่น")

        quest_dir = THAIPACK / "data" / "glogic" / "quest"
        samples = sorted(quest_dir.glob("*.qst"))[:20]
        self.assertGreaterEqual(len(samples), 5, "ต้องมีไฟล์ .qst จริงให้ตรวจ")
        ratios = []
        for qst in samples:
            decoded = ep7_v5.decode(qst.read_bytes())
            ratios.append(sum(1 for b in decoded if 0xA1 <= b <= 0xFB) / len(decoded))
        mean = sum(ratios) / len(ratios)
        # ค่าที่วัดได้จริง: ข้อความไทย 44.4% · ไบต์สุ่ม 35.5% · เนื้อใน .qst เฉลี่ย 18.7%
        # ต่ำกว่าค่าสุ่มแปลว่ามีโครงสร้าง (ตาราง) ไม่ใช่ข้อความ
        self.assertLess(mean, 0.25,
                        f"ไบต์ไทยใน .qst เฉลี่ย {mean:.1%} — สูงเกินกว่าจะเป็นตารางไบนารี")
        self.assertLess(mean, 0.355 * 0.75,
                        f"ไบต์ไทยใน .qst เฉลี่ย {mean:.1%} — ไม่ต่ำกว่าค่าสุ่มอย่างชัดเจน")

    def test_npctalk_and_npcshop_headers_are_plain_ini_not_compbyte(self):
        """บทเรียนจากรอบก่อน: .ntk เป็น INI ธรรมดา — compbyte decode จะได้ขยะ 'tttt…'"""
        ntk_dir = THAIPACK / "data" / "glogic" / "npctalk"
        ntk = next(ntk_dir.glob("*.ntk"), None)
        if ntk is None:
            self.skipTest("ไม่พบไฟล์ .ntk")
        raw = ntk.read_bytes()
        self.assertTrue(raw.startswith(b"default"), f"{ntk.name} ต้องมีหัว 'default' แบบ INI")
        bad = ep7_v5.decode(raw)
        self.assertGreater(bad.count(b"t"), raw.count(b"t"),
                           "compbyte decode ไม่ควร 'ทำให้อ่านง่ายขึ้น' สำหรับไฟล์กลุ่มนี้")

    def test_imported_json_matches_the_files(self):
        self.assertTrue(UI_JSON.is_file())
        import json
        data = json.loads(UI_JSON.read_text(encoding="utf-8"))
        self.assertEqual(data["counts"]["total"], 707)
        self.assertEqual(data["counts"]["per_file"]["gameintext_th.txt"], 485)
        # ทุกค่าใน wanted ต้องมีอักษรไทยจริง
        for key, text in data["wanted"].items():
            self.assertTrue(any("\u0e00" <= c <= "\u0e7f" for c in text), key)
        self.assertGreaterEqual(len(data["wanted"]), 20)

    def test_source_is_not_modified(self):
        # ไฟล์ต้นฉบับของ Drive ต้องไม่ถูกแก้ — เทียบขนาดกับที่บันทึกไว้
        z = REPO / "google_drive" / "RAN-EP7-ThaiPack-FamHawaii.zip"
        if z.is_file():
            self.assertEqual(z.stat().st_size, 2266660)


if __name__ == "__main__":
    unittest.main()
