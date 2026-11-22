# ran_mobile/Server/tests/test_hardening.py
# เทสต์ความทนทานของ Server: session โลกเกมหมดอายุ, ถังระยะเดิน, คำขอผิดรูปแบบ, การทำงานพร้อมกัน,
# log ที่ไม่มีข้อมูลลับ, การตรวจสุขภาพฐานข้อมูล, migration v2 และข้อมูลแผนที่
import json
import logging
import os
import random
import sqlite3
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from ranserver import health, log, migrations, world  # noqa: E402
from ranserver.api import Api, WORLD_IDLE_MS_DEFAULT  # noqa: E402
from ranserver.db import SEED_PATH, connect, init_db  # noqa: E402
from test_api import ApiTestBase, Clock  # noqa: E402

MIN = 60_000


def insert_character(conn, account_id, name):
    """แทรกตัวละครด้วยค่าเริ่มต้นของคอลัมน์ NOT NULL ทั้งหมด (สำหรับเทสต์ที่ใช้สคีมาระดับ migration)"""
    given = {"id", "account_id", "name", "gender", "class"}
    cols = []
    for _cid, col, ctype, notnull, default, _pk in conn.execute("PRAGMA table_info(characters)"):
        if col in given or not notnull or default is not None:
            continue
        cols.append((col, 0 if ctype.upper() in ("INTEGER", "REAL", "NUMERIC", "") else ""))
    names = ["account_id", "name", "gender", "class"] + [c for c, _ in cols]
    values = [account_id, name, "male", "fighter"] + [v for _, v in cols]
    conn.execute(f"INSERT INTO characters ({', '.join(names)}) VALUES ({', '.join('?' * len(values))})", values)
    return conn.execute("SELECT id FROM characters WHERE name = ?", (name,)).fetchone()[0]


class WorldSessionLifecycleTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login("sessionA")
        self.cid = self.new_character(self.token, name="SessA")["id"]

    def enter(self, token=None):
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/enter", token=token or self.token)
        self.assertEqual(s, 200, b)
        return b

    def state(self, token=None):
        s, b = self.call("GET", f"/api/characters/{self.cid}/world", token=token or self.token)
        self.assertEqual(s, 200, b)
        return b

    def in_db(self):
        return self.conn.execute("SELECT 1 FROM world_sessions WHERE character_id = ?", (self.cid,)).fetchone()

    def test_idle_session_expires_and_reconnect_uses_saved_position(self):
        self.enter()
        self.clock.advance(200)
        self.assertEqual(self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 0.52, "y": 0.5},
                                   token=self.token)[0], 200)
        self.clock.advance(WORLD_IDLE_MS_DEFAULT + 1000)   # ไม่มีการติดต่อเกิน 5 นาที
        self.assertFalse(self.state()["connected"])
        self.assertIsNone(self.in_db())
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 0.53, "y": 0.5}, token=self.token)
        self.assertEqual((s, b["error"]["code"]), (409, "not_in_world"))
        self.assertEqual(b["position"], {"x": 0.52, "y": 0.5})  # ตำแหน่งที่บันทึกไว้
        b = self.enter()
        self.assertEqual(b["status"], "entered")                 # session เก่าหมดแล้ว จึงเริ่มใหม่
        self.assertEqual(b["world"]["position"], {"x": 0.52, "y": 0.5})

    def test_heartbeat_keeps_session_alive(self):
        self.enter()
        for _ in range(5):                 # 5 รอบ × 4 นาที = 20 นาที เกินเวลาหมดอายุ แต่มีการติดต่อทุกรอบ
            self.clock.advance(4 * MIN)
            self.assertTrue(self.state()["connected"])
        self.assertIsNotNone(self.in_db())

    def test_move_counts_as_heartbeat(self):
        self.enter()
        self.clock.advance(4 * MIN)
        self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 0.5, "y": 0.5}, token=self.token)
        self.clock.advance(4 * MIN)
        self.assertTrue(self.state()["connected"])

    def test_rejected_move_still_counts_as_heartbeat(self):
        self.enter()
        self.clock.advance(4 * MIN)
        s, _ = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 0.9, "y": 0.9}, token=self.token)
        self.assertEqual(s, 409)
        self.clock.advance(4 * MIN)
        self.assertTrue(self.state()["connected"])

    def test_token_expiry_ends_world_session_of_that_account(self):
        self.register_login("sessionB")
        self.enter()
        self.clock.advance(24 * 3600 * 1000 + 1000)   # โทเคนของ A หมดอายุ (24 ชั่วโมง)
        s, _ = self.call("GET", f"/api/characters/{self.cid}/world", token=self.token)
        self.assertEqual(s, 401)
        # บัญชีอื่นล็อกอินใหม่ (โทเคนใหม่) คำขอนี้ทำให้ระบบปิด session ของบัญชีที่โทเคนหมดอายุ
        other_new = self.call("POST", "/api/auth/login", {"username": "sessionB", "password": "correct-horse-1"})[1]
        self.assertEqual(self.call("GET", "/api/characters", token=other_new["token"])[0], 200)
        self.assertIsNone(self.in_db())

    def test_duplicate_enter_is_resume_and_single_row(self):
        self.enter()
        b = self.enter()
        self.assertEqual(b["status"], "resumed")
        count = self.conn.execute("SELECT COUNT(*) FROM world_sessions WHERE character_id = ?",
                                  (self.cid,)).fetchone()[0]
        self.assertEqual(count, 1)

    def test_leave_then_move_is_not_in_world(self):
        self.enter()
        self.call("POST", f"/api/characters/{self.cid}/world/leave", token=self.token)
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": 0.5, "y": 0.5}, token=self.token)
        self.assertEqual((s, b["error"]["code"]), (409, "not_in_world"))

    def test_other_account_cannot_touch_session(self):
        self.enter()
        intruder = self.register_login("sessionC")
        self.assertEqual(self.call("GET", f"/api/characters/{self.cid}/world", token=intruder)[0], 404)
        self.assertEqual(self.call("POST", f"/api/characters/{self.cid}/world/leave", token=intruder)[0], 404)
        self.assertIsNotNone(self.in_db())


class SpeedBudgetTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login("speedA")
        self.cid = self.new_character(self.token, name="SpeedA")["id"]
        self.call("POST", f"/api/characters/{self.cid}/world/enter", token=self.token)

    def move(self, x, y):
        return self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": x, "y": y}, token=self.token)

    def test_burst_of_small_moves_cannot_cross_the_map(self):
        # เดิม: ทุกคำขอ (เวลาห่างกัน 1 ms) ได้ระยะ 0.03 จึงรวม 60 คำขอได้ระยะ 1.8 ทั้งที่ควรจำกัดไว้ราว 0.5
        accepted = 0.0
        x = 0.5
        for i in range(60):
            self.clock.advance(1)
            nx = 0.53 if i % 2 == 0 else 0.5
            s, _ = self.move(nx, 0.5)
            if s == 200:
                accepted += abs(nx - x)
                x = nx
        total_s = 60 / 1000.0
        self.assertLessEqual(accepted, world.BUDGET_CAP + world.MAX_SPEED * total_s + 1e-9)
        self.assertLess(accepted, 1.0)

    def test_budget_recovers_over_time(self):
        for i in range(20):                    # ใช้ถังจนหมด
            self.clock.advance(1)
            self.move(0.53 if i % 2 == 0 else 0.5, 0.5)
        self.clock.advance(2000)              # พัก 2 วินาที ถังเติมกลับ
        self.assertEqual(self.move(0.53, 0.5)[0], 200)

    def test_budget_never_exceeds_cap(self):
        self.clock.advance(4 * MIN)           # พักนานในช่วงที่ session ยังไม่หมดอายุ ถังต้องไม่เกินค่าสูงสุด
        s, _ = self.move(0.53, 0.5)
        self.assertEqual(s, 200)
        budget = self.conn.execute("SELECT move_budget FROM world_sessions WHERE character_id = ?",
                                   (self.cid,)).fetchone()[0]
        self.assertLessEqual(budget, world.BUDGET_CAP + 1e-9)
        self.assertGreaterEqual(budget, 0.0)

    def test_nan_infinity_bool_and_strings_are_400(self):
        for raw in (b'{"x": NaN, "y": 0.5}', b'{"x": Infinity, "y": 0.5}', b'{"x": true, "y": 0.5}',
                    b'{"x": "0.5", "y": 0.5}', b'{"x": 0.5}', b'{"x": -0.1, "y": 0.5}', b'[1,2]', b'{bad json'):
            self.clock.advance(100)
            s, b = self.api.handle("POST", f"/api/characters/{self.cid}/world/move",
                                   {"Authorization": f"Bearer {self.token}"}, raw)
            self.assertEqual(s, 400, (raw, b))


class ConcurrencyTests(ApiTestBase):
    def test_parallel_world_calls_keep_one_session_per_character(self):
        tokens = []
        cids = []
        for i in range(4):
            t = self.register_login(f"parallel{i}")
            tokens.append(t)
            cids.append(self.new_character(t, name=f"Par{i:02d}")["id"])
        errors = []
        statuses = []

        def worker(idx):
            rng = random.Random(idx)
            token, cid = tokens[idx % 4], cids[idx % 4]
            for _ in range(40):
                try:
                    op = rng.choice(["enter", "move", "state", "leave"])
                    path = {"enter": "/world/enter", "move": "/world/move", "state": "/world",
                            "leave": "/world/leave"}[op]
                    method = "GET" if op == "state" else "POST"
                    body = {"x": round(rng.uniform(0.45, 0.55), 3), "y": 0.5} if op == "move" else None
                    data = json.dumps(body).encode() if body is not None else b""
                    s, _ = self.api.handle(method, f"/api/characters/{cid}{path}",
                                           {"Authorization": f"Bearer {token}"}, data)
                    statuses.append(s)
                except Exception as e:  # noqa: BLE001 - any exception is a failure here
                    errors.append(repr(e))

        threads = [threading.Thread(target=worker, args=(i,)) for i in range(8)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(errors, [])
        self.assertNotIn(500, statuses)
        dup = self.conn.execute(
            "SELECT character_id, COUNT(*) FROM world_sessions GROUP BY character_id HAVING COUNT(*) > 1").fetchall()
        self.assertEqual(dup, [])
        self.assertEqual(self.conn.execute("PRAGMA integrity_check").fetchone()[0], "ok")


class LoggingRedactionTests(ApiTestBase):
    def test_redact_removes_secrets(self):
        cases = [
            ('{"username": "a", "password": "hunter2hunter"}', "hunter2"),
            ("Authorization: Bearer abc.DEF-123_x", "abc.DEF"),
            ("GET /x?token=sekret123&a=1", "sekret123"),
            ('"token": "tok123"', "tok123"),
            ("api_key=KEY999 next", "KEY999"),
        ]
        for text, secret in cases:
            self.assertNotIn(secret, log.redact(text), text)

    def test_mask_name(self):
        self.assertEqual(log.mask_name("alice"), "a***(5)")
        self.assertEqual(log.mask_name(""), "<empty>")

    def test_login_logs_never_contain_password_or_token(self):
        self.call("POST", "/api/auth/register", {"username": "logtester", "password": "secret-pass-123"})
        with self.assertLogs("ranserver", level="INFO") as cm:
            s, b = self.call("POST", "/api/auth/login", {"username": "logtester", "password": "secret-pass-123"})
            self.assertEqual(s, 200)
            token = b["token"]
            self.call("POST", "/api/auth/login", {"username": "logtester", "password": "wrong-password-1"})
        text = "\n".join(cm.output)
        self.assertNotIn("secret-pass-123", text)
        self.assertNotIn("wrong-password-1", text)
        self.assertNotIn(token, text)
        self.assertNotIn("logtester", text)  # ชื่อเต็มไม่ควรปรากฏ
        self.assertIn("login failed", text)

    def test_redaction_filter_applies_to_formatted_records(self):
        f = log.RedactFilter()
        rec = logging.LogRecord("ranserver.http", logging.INFO, __file__, 1,
                                "POST /x token=%s", ("tokvalue123",), None)
        f.filter(rec)
        self.assertNotIn("tokvalue123", rec.getMessage())

    def test_get_logger_rejects_unknown_category(self):
        with self.assertRaises(ValueError):
            log.get_logger("nope")


class HealthTests(unittest.TestCase):
    def setUp(self):
        fd, self.path = tempfile.mkstemp(suffix=".db")
        os.close(fd)
        conn = connect(self.path)
        init_db(conn)
        conn.close()

    def tearDown(self):
        for suffix in ("", "-wal", "-shm"):
            try:
                os.remove(self.path + suffix)
            except OSError:
                pass

    def test_healthy_database(self):
        r = health.check(self.path)
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["schema_version"], migrations.LATEST)
        self.assertGreaterEqual(r["counts"]["maps"], 1)

    def test_missing_file(self):
        r = health.check(self.path + ".missing")
        self.assertFalse(r["ok"])

    def test_wrong_schema_version_is_error(self):
        conn = connect(self.path)
        conn.execute("PRAGMA user_version = 1")
        conn.close()
        r = health.check(self.path)
        self.assertFalse(r["ok"])
        self.assertTrue(any("schema version" in p for p in r["problems"]))

    def test_stale_world_session_is_warning_only(self):
        conn = connect(self.path)
        conn.execute("INSERT INTO accounts (username, password_hash, created_at) VALUES ('staleuser', 'x', '2026-01-01')")
        aid = conn.execute("SELECT id FROM accounts WHERE username='staleuser'").fetchone()[0]
        cid = insert_character(conn, aid, "StaleOne")
        conn.execute("INSERT INTO world_sessions (character_id, map_id, entered_ms, last_move_ms, last_seen_ms)"
                     " VALUES (?, 1, 0, 0, 0)", (cid,))
        conn.close()
        r = health.check(self.path, now_ms=10 * MIN)
        self.assertTrue(r["ok"], r)
        self.assertEqual(r["counts"]["stale_world_sessions"], 1)
        self.assertTrue(r["warnings"])

    def test_cli_exit_codes(self):
        import contextlib
        import io
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(health.main(["--db", self.path]), 0)
            self.assertEqual(health.main(["--db", self.path + ".missing"]), 1)


class MigrationV2Tests(unittest.TestCase):
    def test_v2_round_trip_keeps_rows(self):
        conn = connect(":memory:")
        conn.executescript(SEED_PATH.parent.joinpath("schema.sql").read_text(encoding="utf-8"))
        self.assertEqual(migrations.migrate(conn, target=1), 1)
        conn.execute("INSERT INTO accounts (username, password_hash, created_at) VALUES ('miguser', 'x', 'now')")
        cid = insert_character(conn, conn.execute("SELECT id FROM accounts").fetchone()[0], "MigOne")
        conn.execute("INSERT INTO maps (map_id, name, spawn_x, spawn_y, blocks_json) VALUES (1, 'm', 0.5, 0.5, '[]')")
        conn.execute("INSERT INTO world_sessions (character_id, map_id, entered_ms, last_move_ms) VALUES (?, 1, 5, 7)",
                     (cid,))
        self.assertEqual(migrations.migrate(conn), migrations.LATEST)
        cols = {r[1] for r in conn.execute("PRAGMA table_info(world_sessions)")}
        self.assertTrue({"last_seen_ms", "move_budget"} <= cols)
        seen = conn.execute("SELECT last_seen_ms, move_budget FROM world_sessions").fetchone()
        self.assertEqual(seen[0], 7)                  # คัดลอกจาก last_move_ms
        self.assertAlmostEqual(seen[1], world.BUDGET_CAP)
        self.assertEqual(migrations.migrate(conn, target=1), 1)
        cols = {r[1] for r in conn.execute("PRAGMA table_info(world_sessions)")}
        self.assertNotIn("move_budget", cols)
        self.assertEqual(conn.execute("SELECT COUNT(*) FROM world_sessions").fetchone()[0], 1)
        self.assertEqual(migrations.migrate(conn), migrations.LATEST)
        conn.close()


class MapDataTests(unittest.TestCase):
    def setUp(self):
        self.conn = connect(":memory:")
        self.conn.row_factory = sqlite3.Row
        init_db(self.conn)

    def tearDown(self):
        self.conn.close()

    def test_seed_maps_are_valid_and_spawn_is_walkable(self):
        rows = self.conn.execute("SELECT map_id, spawn_x, spawn_y, blocks_json FROM maps").fetchall()
        self.assertGreaterEqual(len(rows), 1)
        for r in rows:
            blocks = json.loads(r["blocks_json"])
            self.assertEqual(world.validate_blocks(blocks), [], r["map_id"])
            self.assertTrue(world.is_walkable(r["spawn_x"], r["spawn_y"], blocks), r["map_id"])

    def test_seed_monsters_stand_on_walkable_tiles(self):
        blocks = json.loads(self.conn.execute("SELECT blocks_json FROM maps WHERE map_id = 1").fetchone()[0])
        for r in self.conn.execute("SELECT monster_id, pos_x, pos_y FROM monsters WHERE map_id = 1"):
            self.assertTrue(world.is_walkable(r["pos_x"], r["pos_y"], blocks), r["monster_id"])

    def test_validate_blocks_rejects_bad_data(self):
        self.assertEqual(world.validate_blocks([]), [])
        self.assertEqual(world.validate_blocks([[0.1, 0.1, 0.2, 0.2]]), [])
        bad = [
            "not a list",
            [[0.1, 0.1, 0.2]],                 # สามค่า
            [[0.1, 0.1, 0.0, 0.2]],            # กว้างเป็นศูนย์
            [[0.9, 0.1, 0.2, 0.2]],            # ล้นขอบ
            [[0.1, 0.1, float("nan"), 0.2]],   # NaN
            [[0.1, 0.1, True, 0.2]],           # bool
        ]
        for b in bad:
            self.assertNotEqual(world.validate_blocks(b), [], b)

    def test_check_move_unit_rules(self):
        blocks = [[0.42, 0.12, 0.16, 0.06]]
        ok, code, budget = world.check_move(0.5, 0.5, 0.52, 0.5, 100, world.BUDGET_CAP, blocks)
        self.assertTrue(ok)
        self.assertLess(budget, world.BUDGET_CAP)
        ok, code, _ = world.check_move(0.5, 0.5, 0.95, 0.95, 100, world.BUDGET_CAP, blocks)
        self.assertEqual(code, "position_rejected")
        ok, code, _ = world.check_move(0.5, 0.25, 0.5, 0.15, 10_000, world.BUDGET_CAP, blocks)
        self.assertEqual(code, "blocked")
        ok, code, _ = world.check_move(0.5, 0.5, 0.53, 0.5, 0, 0.0, [])
        self.assertEqual(code, "position_rejected")   # ถังว่างจึงเดินไม่ได้
        self.assertFalse(world.session_expired(0, 300_000, 300_000))
        self.assertTrue(world.session_expired(0, 300_001, 300_000))


class SharedCollisionVectorTests(unittest.TestCase):
    """ไฟล์ collision_vectors.txt ใช้ร่วมกับ Client (mobile/net_client/test_world_map.cpp)"""

    VECTORS = os.path.join(os.path.dirname(__file__), "..", "..", "Database", "fixtures", "collision_vectors.txt")

    def test_server_agrees_with_shared_vectors(self):
        from ranserver.world import in_block
        blocks, points = [], []
        with open(self.VECTORS, encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if parts[0] == "block":
                    blocks.append([float(v) for v in parts[1:5]])
                elif parts[0] == "point":
                    points.append((float(parts[1]), float(parts[2]), int(parts[3])))
                else:
                    self.fail(f"unknown line: {line}")
        self.assertTrue(blocks and points)
        for x, y, expect in points:
            self.assertEqual(in_block(x, y, blocks), bool(expect), f"point ({x}, {y})")


if __name__ == "__main__":
    unittest.main()
