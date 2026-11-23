# tools/ep7_drive_importer/test_content_provenance.py
# ตรวจ "ที่มาของข้อมูล" ใน content.json กับไฟล์จริงใน Google Drive
#
# รอบ 4 ตรวจพบว่า content.json เคยอ้างว่าชื่อแผนที่/NPC/ไอเทม "อ้างอิงข้อมูลจริงจาก EP7"
# แต่เมื่อค้นทั้งคลัง ThaiPack จริงกลับไม่พบชื่อเหล่านี้เลย → เทสต์นี้กันไม่ให้คำอ้างนั้นกลับมาอีก
#
#   python3 -m unittest test_content_provenance
import json
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
CONTENT = REPO / "ran_mobile" / "Database" / "content" / "content.json"
THAIPACK = REPO / "google_drive" / "thaipack_extract"


def entity_names(data):
    """ชื่อเฉพาะทั้งหมดที่ผู้เล่นเห็นในเกม"""
    names = set()
    for key, field in (("maps", "name"), ("npcs", "name"), ("monsters", "name"),
                       ("items", "name"), ("skills", "name"), ("quests", "name")):
        for row in data.get(key, []):
            if field in row:
                names.add(row[field])
    return names


def drive_corpus_text():
    blob = b""
    for p in THAIPACK.rglob("*"):
        if p.is_file():
            blob += p.read_bytes()
    return blob.decode("cp874", "ignore")


class ContentProvenanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = json.loads(CONTENT.read_text(encoding="utf-8"))

    def test_no_generator_claim_for_a_missing_script(self):
        """ถ้าอ้างว่าสร้างโดยสคริปต์ สคริปต์นั้นต้องมีอยู่จริง"""
        gen = self.data.get("_generated_by")
        if gen is None:
            return
        direct = REPO / gen
        under_pkg = HERE / Path(gen).name
        self.assertTrue(direct.is_file() or under_pkg.is_file(),
                        f"content.json อ้างว่าสร้างโดย {gen} แต่ไม่พบไฟล์นั้น")

    def test_note_does_not_claim_ep7_names(self):
        note = self.data.get("_note", "")
        self.assertTrue(note, "_note ต้องอธิบายที่มาของข้อมูล")
        # ต้องบอกตรง ๆ ว่าเป็นข้อมูลของโปรเจกต์เอง
        self.assertIn("clean-room", note)
        self.assertIn("ตั้งขึ้นเอง", note)

    @unittest.skipUnless(THAIPACK.is_dir(), "ไม่พบไฟล์ Drive ที่แตกไว้")
    def test_entity_names_are_not_copied_from_drive(self):
        """ชื่อเฉพาะในเกมต้องไม่ปรากฏในไฟล์ Drive — ยืนยันว่าไม่ได้คัดลอกมา"""
        corpus = drive_corpus_text()
        self.assertGreater(len(corpus), 1_000_000, "คลังข้อความ Drive ต้องโหลดได้จริง")
        # ตรวจว่าการค้นหาใช้ได้ (สตริงที่รู้ว่ามีอยู่ต้องเจอ)
        self.assertIn("เงินไม่เพียงพอ", corpus)

        copied = sorted(n for n in entity_names(self.data) if n and n in corpus)
        self.assertEqual(copied, [],
                         "พบชื่อที่คัดลอกมาจากไฟล์ Drive: " + ", ".join(copied))

    @unittest.skipUnless(THAIPACK.is_dir(), "ไม่พบไฟล์ Drive ที่แตกไว้")
    def test_thai_vocabulary_comes_from_real_files(self):
        """คำศัพท์ไทยที่ใช้ประกอบชื่อ ต้องมีอยู่จริงในไฟล์ข้อความ (ไม่ใช่สะกดเอง)"""
        corpus = drive_corpus_text()
        for word in ("ปาร์ตี้", "วิทยาเขต", "ดาบ", "ยา"):
            self.assertIn(word, corpus, f"คำว่า {word} ควรมีในไฟล์ข้อความจริง")

    def test_balance_numbers_are_not_claimed_as_ep7(self):
        note = self.data.get("_note", "")
        self.assertTrue("เขียนขึ้นเอง" in note or "ประมาณ" in note,
                        "ต้องยอมรับว่าตัวเลขสมดุลไม่ได้มาจาก EP7")


if __name__ == "__main__":
    unittest.main()
