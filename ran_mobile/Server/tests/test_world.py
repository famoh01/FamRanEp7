# ran_mobile/Server/tests/test_world.py
# เทสต์โลกเกม: เข้า/ออก/เดิน/เชื่อมใหม่ และการบันทึกตำแหน่ง ทั้งหมดรันบน Api จริงกับ SQLite
import os
import sqlite3
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from ranserver import migrations, world  # noqa: E402
from test_api import ApiTestBase  # noqa: E402


class WorldRulesTests(unittest.TestCase):
    """กติกาการเดินแบบไม่มีฐานข้อมูล"""
    BLOCKS = [[0.42, 0.12, 0.16, 0.06]]

    def test_normal_walk_accepted(self):
        ok, code = world.validate_move(0.5, 0.5, 0.52, 0.5, 200, self.BLOCKS)
        self.assertTrue(ok, code)

    def test_teleport_rejected(self):
        ok, code = world.validate_move(0.5, 0.5, 0.95, 0.95, 200, self.BLOCKS)
        self.assertEqual((ok, code), (False, "position_rejected"))

    def test_gap_in_time_is_capped(self):
        # หยุดส่ง 1 ชั่วโมงแล้วกระโดดไกล ยังถูกจำกัดด้วย MAX_DT_S
        ok, code = world.validate_move(0.1, 0.1, 0.9, 0.1, 3_600_000, self.BLOCKS)
        self.assertEqual((ok, code), (False, "position_rejected"))

    def test_block_rejected(self):
        ok, code = world.validate_move(0.5, 0.2, 0.5, 0.15, 1000, self.BLOCKS)
        self.assertEqual((ok, code), (False, "blocked"))

    def test_coordinate_bounds(self):
        self.assertTrue(world.is_coord(0.0) and world.is_coord(1.0) and world.is_coord(0))
        for bad in (-0.1, 1.01, True, None, "0.5", float("nan"), float("inf")):
            self.assertFalse(world.is_coord(bad), bad)


class WorldTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login("walker01")
        self.cid = self.new_character(self.token, name="Walker1")["id"]

    def enter(self, token=None, cid=None):
        s, b = self.call("POST", f"/api/characters/{cid or self.cid}/world/enter", token=token or self.token)
        self.assertEqual(s, 200, b)
        return b

    def move(self, x, y, token=None, cid=None):
        return self.call("POST", f"/api/characters/{cid or self.cid}/world/move",
                         {"x": x, "y": y}, token=token or self.token)

    def test_enter_returns_real_game_state(self):
        b = self.enter()
        self.assertEqual(b["status"], "entered")
        ch = b["character"]
        self.assertEqual(ch["id"], self.cid)
        self.assertEqual(ch["name"], "Walker1")
        self.assertEqual(ch["level"], 1)
        self.assertGreater(ch["hp"], 0)
        self.assertEqual(ch["map_id"], 1)
        self.assertTrue(ch["in_world"])
        w = b["world"]
        self.assertEqual(w["map_id"], 1)
        self.assertEqual(w["position"], {"x": 0.5, "y": 0.5})  # จุดเกิดของแผนที่
        self.assertEqual(len(w["blocks"]), 3)
        # แผนที่ 1 (วิทยาเขต) มีมอนสเตอร์ 3 ตัวตาม content.json; ที่เหลืออยู่แผนที่ 2/3
        self.assertEqual(len(w["monsters"]), 3)
        self.assertTrue(all("position" in m for m in w["monsters"]))
        self.assertTrue(w["connected"])

    def test_enter_requires_own_character(self):
        other = self.register_login("other01")
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/enter", token=other)
        self.assertEqual(s, 404)
        self.assertEqual(b["error"]["code"], "character_not_found")

    def test_move_before_enter_is_rejected_with_position(self):
        s, b = self.move(0.52, 0.5)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_in_world")
        self.assertEqual(b["position"], {"x": 0.5, "y": 0.5})

    def test_valid_move_is_accepted_and_saved(self):
        self.enter()
        self.clock.advance(200)
        s, b = self.move(0.53, 0.5)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["position"], {"x": 0.53, "y": 0.5})
        s, state = self.call("GET", f"/api/characters/{self.cid}/world", token=self.token)
        self.assertEqual(state["world"]["position"], {"x": 0.53, "y": 0.5})

    def test_teleport_rejected_and_position_unchanged(self):
        self.enter()
        self.clock.advance(200)
        s, b = self.move(0.95, 0.95)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "position_rejected")
        self.assertEqual(b["position"], {"x": 0.5, "y": 0.5})  # ตำแหน่งที่ Server ยอมรับจริง

    def test_move_into_block_rejected(self):
        self.enter()
        # เดินจากจุดเกิดไปทางบล็อกด้านล่าง (y 0.12..0.18) ผ่านการเดินหลายครั้ง
        for y in (0.45, 0.35, 0.25):
            self.clock.advance(1000)
            s, _ = self.move(0.5, y)
            self.assertEqual(s, 200)
        self.clock.advance(1000)
        s, b = self.move(0.5, 0.15)
        self.assertEqual((s, b["error"]["code"]), (409, "blocked"))
        self.assertEqual(b["position"], {"x": 0.5, "y": 0.25})

    def test_out_of_range_coordinates_are_400(self):
        self.enter()
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 1.5, "y": 0.5}, token=self.token)
        self.assertEqual((s, b["error"]["code"]), (400, "invalid_position"))
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": "a", "y": 0.5}, token=self.token)
        self.assertEqual(s, 400)

    def test_leave_saves_position_and_clears_session(self):
        self.enter()
        self.clock.advance(200)
        self.move(0.55, 0.5)
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/leave", token=self.token)
        self.assertEqual(s, 200)
        self.assertFalse(b["connected"])
        self.assertEqual(b["position"], {"x": 0.55, "y": 0.5})
        s, state = self.call("GET", f"/api/characters/{self.cid}/world", token=self.token)
        self.assertEqual(state["connected"], False)
        s, b = self.move(0.56, 0.5)
        self.assertEqual(b["error"]["code"], "not_in_world")

    def test_reconnect_resumes_same_position(self):
        self.enter()
        self.clock.advance(200)
        self.move(0.54, 0.5)
        b = self.enter()           # เชื่อมต่อใหม่โดยไม่ได้ออกก่อน
        self.assertEqual(b["status"], "resumed")
        self.assertEqual(b["world"]["position"], {"x": 0.54, "y": 0.5})

    def test_save_load_across_logout_and_login(self):
        self.enter()
        self.clock.advance(200)
        self.move(0.6, 0.45)
        s, _ = self.call("POST", "/api/auth/logout", token=self.token)
        self.assertEqual(s, 200)
        # ออกจากระบบแล้วต้องไม่ยังอยู่ในโลก แต่ตำแหน่งต้องถูกบันทึก
        s, b = self.call("POST", "/api/auth/login", {"username": "walker01", "password": "correct-horse-1"})
        self.assertEqual(s, 200)
        token2 = b["token"]
        s, detail = self.call("GET", f"/api/characters/{self.cid}", token=token2)
        self.assertEqual(s, 200)
        self.assertEqual(detail["position"], {"x": 0.6, "y": 0.45})
        self.assertFalse(detail["in_world"])
        b = self.enter(token=token2)
        self.assertEqual(b["world"]["position"], {"x": 0.6, "y": 0.45})
        self.assertEqual(b["status"], "entered")

    def test_logout_ends_world_session(self):
        self.enter()
        self.call("POST", "/api/auth/logout", token=self.token)
        row = self.conn.execute("SELECT 1 FROM world_sessions WHERE character_id = ?", (self.cid,)).fetchone()
        self.assertIsNone(row)

    def test_delete_character_removes_world_session(self):
        self.enter()
        s, _ = self.call("DELETE", f"/api/characters/{self.cid}", token=self.token)
        self.assertEqual(s, 200)
        row = self.conn.execute("SELECT 1 FROM world_sessions WHERE character_id = ?", (self.cid,)).fetchone()
        self.assertIsNone(row)

    def test_two_clients_move_independently(self):
        other_token = self.register_login("walker02")
        other_cid = self.new_character(other_token, name="Walker2")["id"]
        self.enter()
        self.enter(token=other_token, cid=other_cid)
        self.clock.advance(200)
        self.assertEqual(self.move(0.52, 0.5)[0], 200)
        self.assertEqual(self.move(0.48, 0.5, token=other_token, cid=other_cid)[0], 200)
        _, a = self.call("GET", f"/api/characters/{self.cid}/world", token=self.token)
        _, b = self.call("GET", f"/api/characters/{other_cid}/world", token=other_token)
        self.assertEqual(a["world"]["position"], {"x": 0.52, "y": 0.5})
        self.assertEqual(b["world"]["position"], {"x": 0.48, "y": 0.5})
        # มอนสเตอร์เป็นชุดเดียวกันของแผนที่ ทั้งสองคนเห็นสถานะเดียวกัน
        # (ระยะ/in_range ต่างกันได้เพราะคำนวณจากตำแหน่งของผู้เล่นแต่ละคน)
        # (มอนสเตอร์เดินตาม AI ได้ จึงเทียบเฉพาะข้อมูลประจำตัว ไม่ใช่ตำแหน่ง/สถานะชั่วขณะ)
        keys = ("monster_id", "name", "level", "max_hp", "hp")
        self.assertEqual([{k: m[k] for k in keys} for m in a["world"]["monsters"]],
                         [{k: m[k] for k in keys} for m in b["world"]["monsters"]])
        # ระยะถูกคำนวณจากตำแหน่งของผู้เล่นคนนั้นจริง
        for m, other in zip(a["world"]["monsters"], b["world"]["monsters"]):
            self.assertNotEqual(m["distance"], other["distance"])

    def test_monsters_have_positions_on_map(self):
        b = self.enter()
        pos = {(m["monster_id"]): (m["position"]["x"], m["position"]["y"]) for m in b["world"]["monsters"]}
        self.assertEqual(pos[1], (0.15, 0.75))
        blocks = b["world"]["blocks"]
        for mid, (x, y) in pos.items():
            self.assertFalse(world.in_block(x, y, blocks), f"monster {mid} inside a block")


class MigrationTests(unittest.TestCase):
    def test_down_then_up_round_trip(self):
        conn = sqlite3.connect(":memory:", isolation_level=None)
        conn.row_factory = sqlite3.Row
        from ranserver.db import SCHEMA_PATH, SEED_PATH
        conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
        self.assertEqual(migrations.migrate(conn), migrations.LATEST)
        conn.executescript(SEED_PATH.read_text(encoding="utf-8"))
        cols_up = {r[1] for r in conn.execute("PRAGMA table_info(characters)")}
        self.assertIn("pos_x", cols_up)
        self.assertEqual(migrations.migrate(conn, target=0), 0)
        cols_down = {r[1] for r in conn.execute("PRAGMA table_info(characters)")}
        self.assertNotIn("pos_x", cols_down)
        self.assertNotIn("map_id", cols_down)
        tables = {r[0] for r in conn.execute("SELECT name FROM sqlite_master WHERE type='table'")}
        self.assertNotIn("world_sessions", tables)
        # ข้อมูลเดิมยังอยู่หลังย้อนกลับ
        self.assertGreater(conn.execute("SELECT COUNT(*) FROM items").fetchone()[0], 0)
        self.assertEqual(migrations.migrate(conn), migrations.LATEST)
        self.assertIn("world_sessions", {r[0] for r in conn.execute("SELECT name FROM sqlite_master")})
        conn.close()

    def test_old_database_without_version_is_upgraded(self):
        # ฐานข้อมูลเก่า (ก่อนมี migration) ต้องอัปเกรดได้โดยไม่ลบข้อมูล
        conn = sqlite3.connect(":memory:", isolation_level=None)
        conn.row_factory = sqlite3.Row
        from ranserver.db import SCHEMA_PATH, SEED_PATH
        conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
        # seed เวอร์ชันเก่า = ส่วนก่อนแผนที่ทดสอบ (โปรแกรมเก่ายังไม่รู้จักตาราง maps)
        old_seed = SEED_PATH.read_text(encoding="utf-8").split("INSERT OR IGNORE INTO maps")[0]
        conn.executescript(old_seed)
        conn.execute("PRAGMA user_version = 0")
        self.assertEqual(migrations.current_version(conn), 0)
        migrations.migrate(conn)
        self.assertEqual(migrations.current_version(conn), migrations.LATEST)
        self.assertEqual(conn.execute("SELECT COUNT(*) FROM monsters").fetchone()[0], 5)
        conn.close()


if __name__ == "__main__":
    unittest.main()
