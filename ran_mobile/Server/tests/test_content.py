# ran_mobile/Server/tests/test_content.py
# เทสต์ชุดข้อมูลเกม (content.json) และ AI มอนสเตอร์
# รัน: cd ran_mobile/Server && python -m unittest discover -s tests
import json
import os
import random
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from ranserver import content, game, monster_ai  # noqa: E402

CONTENT_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "..", "Database", "content", "content.json")


def load_raw():
    with open(CONTENT_PATH, encoding="utf-8") as f:
        return json.load(f)


class ContentLoadTests(unittest.TestCase):
    def test_default_file_loads_and_is_valid(self):
        data = content.load()
        self.assertGreaterEqual(data.version, 1)
        self.assertGreaterEqual(len(data.monsters), 3)
        self.assertGreaterEqual(len(data.skills), 3)
        self.assertGreaterEqual(len(data.npcs), 1)
        # ทุกมอนสเตอร์/สกิล/NPC มีข้อมูลครบและอยู่ในขอบเขต
        for m in data.monsters:
            self.assertGreaterEqual(m["max_hp"], 1)
            self.assertTrue(0.0 <= m["pos_x"] <= 1.0 and 0.0 <= m["pos_y"] <= 1.0)
        for s in data.skills:
            self.assertGreaterEqual(s["range"], 0.0)
            self.assertIn(s["cost_stat"], ("mp", "sp"))

    def test_file_matches_the_database_tables(self):
        """ค่าในไฟล์ต้องถูกเขียนลงฐานข้อมูลจริง (ไฟล์คือแหล่งความจริงของสมดุลเกม)"""
        import sqlite3
        from ranserver.db import connect, init_db
        conn = connect(":memory:")
        conn.row_factory = sqlite3.Row
        init_db(conn)
        data = content.load()
        for m in data.monsters:
            row = conn.execute("SELECT * FROM monsters WHERE monster_id = ?", (m["monster_id"],)).fetchone()
            self.assertEqual(row["name"], m["name"])
            self.assertEqual(row["max_hp"], m["max_hp"])
            self.assertEqual(row["exp_reward"], m["exp_reward"])
            self.assertEqual(row["home_x"], m["pos_x"])
        for s in data.skills:
            row = conn.execute("SELECT * FROM skills WHERE skill_id = ?", (s["skill_id"],)).fetchone()
            self.assertEqual(row["name"], s["name"])
            self.assertEqual(row["cooldown_ms"], s["cooldown_ms"])
        for n in data.npcs:
            row = conn.execute("SELECT * FROM npcs WHERE npc_id = ?", (n["npc_id"],)).fetchone()
            self.assertEqual(json.loads(row["dialog_json"]), n["dialog"])
        conn.close()

    def _write(self, mutate):
        raw = load_raw()
        mutate(raw)
        f = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
        json.dump(raw, f, ensure_ascii=False)
        f.close()
        self.addCleanup(os.unlink, f.name)
        return f.name

    def test_broken_files_are_rejected_with_a_readable_error(self):
        def drop_key(raw):
            del raw["combat"]["attack_range"]
        with self.assertRaises(content.ContentError) as ctx:
            content.load(self._write(drop_key))
        self.assertIn("attack_range", str(ctx.exception))

        def bad_position(raw):
            raw["monsters"][0]["pos"] = [1.4, 0.2]
        with self.assertRaises(content.ContentError):
            content.load(self._write(bad_position))

        def bad_drop_item(raw):
            raw["monsters"][0]["drops"][0]["item_id"] = 999999
        with self.assertRaises(content.ContentError) as ctx:
            content.load(self._write(bad_drop_item), item_ids={1001, 2001})
        self.assertIn("items", str(ctx.exception))

        def duplicate(raw):
            raw["monsters"].append(dict(raw["monsters"][0]))
        with self.assertRaises(content.ContentError):
            content.load(self._write(duplicate))

    def test_bad_json_reports_the_line(self):
        f = tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8")
        f.write('{"version": 1,\n "combat": }\n')
        f.close()
        self.addCleanup(os.unlink, f.name)
        with self.assertRaises(content.ContentError) as ctx:
            content.load(f.name)
        self.assertIn("บรรทัด", str(ctx.exception))

    def test_game_curve_follows_the_content_file(self):
        data = content.load()
        game.configure(data.level_up)
        try:
            self.assertEqual(game.exp_required(1), data.level_up["exp_base"])
            self.assertEqual(game.exp_required(5),
                             data.level_up["exp_base"] + data.level_up["exp_growth"] * 4)
        finally:
            game.configure(None)


class MonsterAiTests(unittest.TestCase):
    AI = {"aggro_range": 0.16, "attack_range": 0.10, "chase_speed": 0.10, "patrol_radius": 0.06}

    def mon(self, x=0.5, y=0.5, patrol=0.05):
        return {"pos_x": x, "pos_y": y, "home_x": 0.5, "home_y": 0.5, "patrol": patrol}

    def test_patrols_but_stays_near_home(self):
        rng = random.Random(7)
        m = self.mon()
        for _ in range(200):
            step = monster_ai.step(m, [], 100, [], rng, ai=self.AI)
            self.assertIn(step["state"], ("patrol", "idle", "return"))
            m["pos_x"], m["pos_y"] = step["x"], step["y"]
            self.assertLessEqual(((m["pos_x"] - 0.5) ** 2 + (m["pos_y"] - 0.5) ** 2) ** 0.5, 0.07)
            self.assertIsNone(step["target_id"])

    def test_chases_a_player_inside_aggro_range(self):
        m = self.mon()
        # 0.64 = อยู่ในระยะตรวจจับ (0.16) แต่ยังไกลกว่าระยะโจมตี (0.10)
        step = monster_ai.step(m, [(9, 0.64, 0.5)], 100, [], random.Random(1), ai=self.AI)
        self.assertEqual(step["state"], "chase")
        self.assertEqual(step["target_id"], 9)
        self.assertGreater(step["x"], m["pos_x"])       # ขยับเข้าหาผู้เล่น
        self.assertFalse(step["attack"])                # ยังไม่ถึงระยะโจมตี

    def test_attacks_when_close_and_ignores_far_players(self):
        m = self.mon()
        near = monster_ai.step(m, [(3, 0.54, 0.5)], 100, [], random.Random(1), ai=self.AI)
        self.assertEqual(near["state"], "attack")
        self.assertTrue(near["attack"])
        far = monster_ai.step(m, [(4, 0.95, 0.95)], 100, [], random.Random(1), ai=self.AI)
        self.assertNotEqual(far["target_id"], 4)

    def test_returns_home_after_losing_the_target(self):
        m = self.mon(x=0.62, y=0.5)
        m["pos_x"] = 0.62
        step = monster_ai.step(m, [], 100, [], random.Random(1), ai=self.AI)
        self.assertEqual(step["state"], "return")
        self.assertLess(step["x"], 0.62)

    def test_blocked_chase_does_not_walk_into_a_block(self):
        blocks = [[0.55, 0.40, 0.10, 0.20]]
        m = self.mon(x=0.52, y=0.5, patrol=0)
        step = monster_ai.step(m, [(1, 0.70, 0.5)], 100, blocks, random.Random(1), ai=self.AI)
        self.assertFalse(monster_ai.in_block(step["x"], step["y"], blocks))
        self.assertLessEqual(step["x"], 0.55)

    def test_step_is_limited_so_a_long_pause_cannot_teleport(self):
        m = self.mon()
        far_away = [(1, 0.55, 0.5)]
        step = monster_ai.step(m, far_away, 60_000, [], random.Random(1), ai=self.AI)
        moved = ((step["x"] - m["pos_x"]) ** 2 + (step["y"] - m["pos_y"]) ** 2) ** 0.5
        # สูงสุด 0.25 วินาที * ความเร็วไล่ 0.10 = 0.025 ต่อครั้ง
        self.assertLessEqual(moved, 0.026)

    def test_hit_player_applies_defense(self):
        rng = random.Random(3)
        damage, _, missed = monster_ai.hit_player(20, 5, rng)
        self.assertFalse(missed)
        self.assertEqual(damage, 15)
        self.assertEqual(monster_ai.hit_player(3, 50, rng)[0], 1)   # อย่างน้อย 1

    def test_drops_respect_chance_and_range(self):
        rng = random.Random(11)
        drops = [{"item_id": 1001, "chance": 1.0, "min": 2, "max": 3},
                 {"item_id": 1002, "chance": 0.0, "min": 1, "max": 1}]
        out = monster_ai.roll_drops(drops, rng)
        self.assertEqual(len(out), 1)
        self.assertEqual(out[0][0], 1001)
        self.assertIn(out[0][1], (2, 3))


if __name__ == "__main__":
    unittest.main()
