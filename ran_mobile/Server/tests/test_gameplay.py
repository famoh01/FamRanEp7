# ran_mobile/Server/tests/test_gameplay.py
# เทสต์ระบบเล่นจริง: AI มอนสเตอร์ (ไล่/สวนกลับ/ตาย/เกิดใหม่), ของดรอป, เลเวลอัปที่เพิ่มค่าจริง,
# และการเห็นผู้เล่นคนอื่นบนแผนที่เดียวกัน (/world/sync)
# รัน: cd ran_mobile/Server && python -m unittest discover -s tests
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from ranserver import game  # noqa: E402
from test_api import ApiTestBase  # noqa: E402

DUMMY = 3        # ตุ๊กตาซ้อม: อยู่ใกล้จุดเกิด (0.44, 0.57), ไม่ตอบโต้
CROW = 5         # อีก้าดาดฟ้า: อยู่ไกล


class GameplayBase(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login()
        self.cid = self.new_character(self.token)["id"]
        self.enter()

    def enter(self, token=None, cid=None):
        token = token or self.token
        cid = cid or self.cid
        s, b = self.call("POST", f"/api/characters/{cid}/world/enter", token=token)
        self.assertEqual(s, 200, b)
        return b

    def move(self, x, y, token=None, cid=None):
        """เดินเป็นช่วงสั้น ๆ ต่อเนื่องจนถึงจุดหมาย (Server จำกัดระยะต่อคำขอ)"""
        token = token or self.token
        cid = cid or self.cid
        for _ in range(400):
            row = self.conn.execute("SELECT pos_x, pos_y FROM characters WHERE id = ?",
                                    (cid,)).fetchone()
            dx, dy = x - row[0], y - row[1]
            dist = (dx * dx + dy * dy) ** 0.5
            if dist < 0.008:
                return {"position": {"x": row[0], "y": row[1]}}
            step = min(0.08, dist)
            nx, ny = row[0] + dx / dist * step, row[1] + dy / dist * step
            self.clock.advance(400)   # 400 ms * 0.45 = ระยะที่อนุญาต 0.18 ต่อคำขอ
            s, b = self.call("POST", f"/api/characters/{cid}/world/move", {"x": nx, "y": ny}, token=token)
            if s == 429:
                continue
            if s == 409 and b["error"]["code"] == "blocked":
                # สิ่งกีดขวาง: ลองขยับข้างละเล็กน้อยแทนการเดินตรง
                row = self.conn.execute("SELECT pos_x, pos_y FROM characters WHERE id = ?",
                                        (cid,)).fetchone()
                alt = (row[0] + 0.06, row[1]) if abs(dy) >= abs(dx) else (row[0], row[1] + 0.06)
                self.call("POST", f"/api/characters/{cid}/world/move",
                          {"x": alt[0], "y": alt[1]}, token=token)
                continue
            self.assertEqual(s, 200, b)
        self.fail(f"player {cid} could not walk to {x},{y}")

    def attack(self, monster_id=DUMMY, token=None, cid=None):
        token = token or self.token
        cid = cid or self.cid
        return self.call("POST", f"/api/characters/{cid}/combat/attack",
                         {"monster_id": monster_id}, token=token)

    def sync(self, token=None, cid=None):
        token = token or self.token
        cid = cid or self.cid
        s, b = self.call("GET", f"/api/characters/{cid}/world/sync", token=token)
        self.assertEqual(s, 200, b)
        return b

    def kill(self, monster_id=DUMMY, token=None, cid=None):
        """ตีจนมอนสเตอร์ตาย (จัดการคูลดาวน์/เกิดใหม่ให้) คืนผลลัพธ์ครั้งที่สังหาร"""
        for _ in range(400):
            s, b = self.attack(monster_id, token=token, cid=cid)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            if s == 409 and b["error"]["code"] == "monster_dead":
                self.clock.advance(30001)
                continue
            if s == 409 and b["error"]["code"] == "character_dead":
                self.call("POST", f"/api/characters/{cid or self.cid}/revive", token=token or self.token)
                continue
            self.assertEqual(s, 200, b)
            if b["monster"]["dead"]:
                return b
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        self.fail("monster did not die")


class MonsterAiApiTests(GameplayBase):
    def test_monster_chases_then_hits_the_player(self):
        # ผู้เล่นเดินไปใกล้อีก้า (0.90, 0.85) ในระยะตรวจจับแต่ยังไม่ถึงระยะโจมตี
        self.move(0.78, 0.85)
        seen_chase = False
        hp_before = self.conn.execute("SELECT hp FROM characters WHERE id = ?", (self.cid,)).fetchone()[0]
        for _ in range(10):
            self.clock.advance(200)
            b = self.sync()
            crow = next(m for m in b["monsters"] if m["monster_id"] == CROW)
            if crow["state"] in ("chase", "attack") and crow["target_id"] == self.cid:
                seen_chase = True
            if crow["state"] == "attack":
                break
        self.assertTrue(seen_chase, "อีก้าต้องไล่ผู้เล่นที่เดินเข้ามาใกล้")
        hp_after = self.conn.execute("SELECT hp FROM characters WHERE id = ?", (self.cid,)).fetchone()[0]
        self.assertLess(hp_after, hp_before, "มอนสเตอร์ต้องโจมตีสวนกลับ")

    def test_monster_returns_home_when_the_player_leaves(self):
        # อยู่ห่างจากอีก้าเกินระยะโจมตี (0.10) แต่ยังอยู่ในระยะตรวจจับ (0.16) → อีก้าเดินเข้ามา
        self.move(0.78, 0.85)
        for _ in range(12):
            self.clock.advance(250)
            self.sync()
        home = self.conn.execute(
            "SELECT home_x, home_y FROM monsters WHERE monster_id = ?", (CROW,)).fetchone()
        away = self.conn.execute(
            "SELECT pos_x, pos_y FROM monsters WHERE monster_id = ?", (CROW,)).fetchone()
        self.assertGreater(abs(away[0] - home[0]) + abs(away[1] - home[1]), 0.0)
        # ผู้เล่นเดินกลับไปกลางแมพ: มอนสเตอร์ค่อย ๆ กลับจุดเกิด
        self.move(0.55, 0.50)
        for _ in range(40):
            self.clock.advance(250)
            self.sync()
        back = self.conn.execute(
            "SELECT pos_x, pos_y FROM monsters WHERE monster_id = ?", (CROW,)).fetchone()
        self.assertLess(abs(back[0] - home[0]) + abs(back[1] - home[1]), 0.02)
        self.assertEqual(self.conn.execute(
            "SELECT state FROM monsters WHERE monster_id = ?", (CROW,)).fetchone()[0] in
            ("idle", "patrol", "return"), True)

    def test_dead_monster_does_not_chase_and_respawns_at_home(self):
        self.move(0.44, 0.57)
        self.kill(DUMMY)
        state = self.conn.execute("SELECT hp, respawn_at_ms FROM monsters WHERE monster_id = ?",
                                  (DUMMY,)).fetchone()
        self.assertEqual(state[0], 0)
        self.assertGreater(state[1], 0)
        self.clock.advance(self.api.monster_respawn_ms + 1)
        b = self.sync()
        dummy = next(m for m in b["monsters"] if m["monster_id"] == DUMMY)
        self.assertFalse(dummy["dead"])
        self.assertEqual(dummy["hp"], dummy["max_hp"])
        self.assertAlmostEqual(dummy["position"]["x"], dummy["home"]["x"], places=4)
        self.assertAlmostEqual(dummy["position"]["y"], dummy["home"]["y"], places=4)

    def test_player_dies_and_leaves_world(self):
        self.move(0.80, 0.85)   # ถึงระยะโจมตีพอดี เพื่อให้อีก้าตีทันที
        self.conn.execute("UPDATE characters SET hp = 1 WHERE id = ?", (self.cid,))
        self.conn.execute("UPDATE monsters SET attack = 500 WHERE monster_id = ?", (CROW,))
        for _ in range(40):
            self.clock.advance(300)
            self.sync()
            hp = self.conn.execute("SELECT hp FROM characters WHERE id = ?", (self.cid,)).fetchone()[0]
            if hp == 0:
                break
        self.assertEqual(hp, 0)
        # ออกจากโลกแล้ว: ต้องฟื้นก่อนจึงจะเล่นต่อได้
        b = self.sync()
        self.assertFalse(b["connected"])
        s, b = self.attack(DUMMY)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "character_dead")
        s, b = self.call("POST", f"/api/characters/{self.cid}/revive", token=self.token)
        self.assertEqual(s, 200)


class RewardTests(GameplayBase):
    def test_kill_gives_exp_money_and_drops_the_item_into_the_bag(self):
        self.move(0.44, 0.57)
        # การันตีของดรอป: ตั้งโอกาสเป็น 100%
        self.conn.execute("INSERT OR REPLACE INTO monster_drops (monster_id, item_id, chance, min_qty, max_qty)"
                          " VALUES (?, 2001, 1.0, 1, 1)", (DUMMY,))
        bag_before = self.conn.execute(
            "SELECT COALESCE(SUM(quantity), 0) FROM inventory WHERE character_id = ? AND item_id = 2001",
            (self.cid,)).fetchone()[0]
        b = self.kill(DUMMY)
        self.assertEqual(b["rewards"]["exp"], 20)
        self.assertEqual(b["rewards"]["money"], 10)
        self.assertEqual(len(b["drops"]), 1)
        self.assertEqual(b["drops"][0]["item_id"], 2001)
        self.assertTrue(b["drops"][0]["stored"])
        bag_after = self.conn.execute(
            "SELECT COALESCE(SUM(quantity), 0) FROM inventory WHERE character_id = ? AND item_id = 2001",
            (self.cid,)).fetchone()[0]
        self.assertEqual(bag_after, bag_before + 1)

    def test_level_up_raises_stats_in_the_database(self):
        self.move(0.44, 0.57)
        self.conn.execute("UPDATE characters SET exp = 95, base_atk = 0, base_def = 0 WHERE id = ?",
                          (self.cid,))
        before = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        b = self.kill(DUMMY)          # +20 EXP → เลเวลอัป (ต้องใช้ 100)
        self.assertGreaterEqual(b["rewards"]["levels_gained"], 1)
        growth = b["rewards"]["growth"]
        self.assertGreater(growth["atk_added"], 0)
        self.assertGreater(growth["def_added"], 0)
        row = self.conn.execute("SELECT level, base_atk, base_def, hp, mp FROM characters WHERE id = ?",
                                (self.cid,)).fetchone()
        self.assertEqual(row["level"], before["level"] + 1)
        self.assertEqual(row["base_atk"], growth["atk_added"])
        self.assertEqual(row["base_def"], growth["def_added"])
        # เลเวลอัปฟื้น HP เต็ม และค่าสูงสุดเพิ่มขึ้นจริง
        self.assertEqual(row["hp"], game.max_hp(row["level"], 12))
        sync = self.sync()
        self.assertEqual(sync["player"]["level"], row["level"])
        self.assertEqual(sync["player"]["atk"], game.attack_power(row["base_atk"], 14, 0))

    def test_rewards_carry_the_new_hp_and_exp_for_the_hud(self):
        self.move(0.44, 0.57)
        s, b = self.attack(DUMMY)
        self.assertEqual(s, 200)
        self.assertIn("player", b)
        self.assertIn("exp_required", b["player"])
        self.assertGreater(b["player"]["max_hp"], 0)
        self.assertIn("position", b["monster"])


class MultiplayerSyncTests(GameplayBase):
    def test_other_player_appears_within_view_range(self):
        token2 = self.register_login("walker02")
        cid2 = self.new_character(token2, name="Walker2")["id"]
        self.enter(token=token2, cid=cid2)
        # ผู้เล่นที่สองเดินมาอยู่ข้าง ๆ (ตำแหน่งของแต่ละคนบันทึกแยกกัน)
        self.move(0.60, 0.50, token=token2, cid=cid2)
        b = self.sync()
        ids = [p["character_id"] for p in b["players"]]
        self.assertIn(cid2, ids)
        other = next(p for p in b["players"] if p["character_id"] == cid2)
        self.assertEqual(other["name"], "Walker2")
        self.assertAlmostEqual(other["position"]["x"], 0.60, places=3)
        self.assertLessEqual(other["distance"], self.api.view_range)
        # ระยะที่ส่งมาเป็นระยะจริงบนแผนที่
        self.assertAlmostEqual(other["distance"], 0.10, places=2)

    def test_player_outside_view_range_is_not_sent(self):
        token2 = self.register_login("walker02")
        cid2 = self.new_character(token2, name="Walker2")["id"]
        self.enter(token=token2, cid=cid2)
        # 0.05,0.70 อยู่คนละมุมกับจุดเกิด (0.5,0.5) ห่าง 0.49 เกินระยะสายตา และไม่มีสิ่งกีดขวาง
        self.move(0.05, 0.70, token=token2, cid=cid2)
        b = self.sync()
        self.assertNotIn(cid2, [p["character_id"] for p in b["players"]])

    def test_player_who_left_the_world_disappears(self):
        token2 = self.register_login("walker02")
        cid2 = self.new_character(token2, name="Walker2")["id"]
        self.enter(token=token2, cid=cid2)
        self.move(0.60, 0.50, token=token2, cid=cid2)
        self.assertIn(cid2, [p["character_id"] for p in self.sync()["players"]])
        self.call("POST", f"/api/characters/{cid2}/world/leave", token=token2)
        self.assertNotIn(cid2, [p["character_id"] for p in self.sync()["players"]])

    def test_sync_carries_monster_movement_and_positions(self):
        first = self.sync()
        self.assertIn("monsters", first)
        self.assertIn("npcs", self.call("GET", "/api/world/npcs", token=self.token)[1] or {"npcs": 1})
        self.clock.advance(250)
        second = self.sync()
        self.assertEqual(len(first["monsters"]), len(second["monsters"]))
        # สถานะ/ตำแหน่งของมอนสเตอร์ถูกส่งมาทุกครั้งที่เรียก sync
        for m in second["monsters"]:
            self.assertIn(m["state"], ("idle", "patrol", "chase", "attack", "return", "dead"))
            self.assertIn("position", m)
            self.assertIn("distance", m)

    def test_sync_requires_world(self):
        self.call("POST", f"/api/characters/{self.cid}/world/leave", token=self.token)
        b = self.sync()
        self.assertFalse(b["connected"])
        self.assertEqual(b["players"], [])
        self.assertEqual(b["monsters"], [])


if __name__ == "__main__":
    unittest.main()
