# ran_mobile/Server/tests/test_api.py
# รันเทสต์: cd ran_mobile/Server && python -m unittest discover -s tests -v
import json
import os
import random
import sys
import threading
import unittest
import urllib.request
from urllib.error import HTTPError

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))

from ranserver import game  # noqa: E402
from ranserver.api import Api  # noqa: E402
from ranserver.db import connect, init_db  # noqa: E402
from ranserver.http_server import make_server  # noqa: E402

# การทดสอบไม่ควรพิมพ์ log ลงหน้าจอ (เทสต์ที่ตรวจ log ใช้ assertLogs เอง)
import logging  # noqa: E402
logging.getLogger("ranserver").addHandler(logging.NullHandler())
logging.getLogger("ranserver").propagate = False


class Clock:
    def __init__(self):
        self.ms = 1_700_000_000_000

    def ms_now(self):
        return self.ms

    def s_now(self):
        return self.ms // 1000

    def advance(self, ms):
        self.ms += ms


class ApiTestBase(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.conn = connect(":memory:")
        init_db(self.conn)
        self.api = Api(self.conn, rng=random.Random(12345),
                       clock_ms=self.clock.ms_now, clock_s=self.clock.s_now)

    def tearDown(self):
        self.conn.close()

    def call(self, method, path, body=None, token=None, key=None):
        headers = {}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        if key:
            headers["Idempotency-Key"] = key
        data = json.dumps(body).encode("utf-8") if body is not None else b""
        return self.api.handle(method, path, headers, data)

    def register_login(self, username="player01", password="correct-horse-1"):
        s, _ = self.call("POST", "/api/auth/register", {"username": username, "password": password})
        self.assertEqual(s, 201)
        s, b = self.call("POST", "/api/auth/login", {"username": username, "password": password})
        self.assertEqual(s, 200)
        return b["token"]

    def new_character(self, token, name="Hero01", cls="fighter"):
        s, b = self.call("POST", "/api/characters", {"name": name, "gender": "male", "class": cls}, token=token)
        self.assertEqual(s, 201, b)
        return b


class AuthTests(ApiTestBase):
    def test_register_login_logout_flow(self):
        token = self.register_login()
        s, b = self.call("GET", "/api/characters", token=token)
        self.assertEqual(s, 200)
        self.assertEqual(b["characters"], [])
        s, _ = self.call("POST", "/api/auth/logout", token=token)
        self.assertEqual(s, 200)
        s, b = self.call("GET", "/api/characters", token=token)
        self.assertEqual(s, 401)
        self.assertEqual(b["error"]["code"], "unauthorized")

    def test_duplicate_username_rejected(self):
        self.register_login()
        s, b = self.call("POST", "/api/auth/register", {"username": "player01", "password": "another-pass-2"})
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "username_taken")

    def test_wrong_password_and_unknown_user_return_same_error(self):
        self.register_login()
        s1, b1 = self.call("POST", "/api/auth/login", {"username": "player01", "password": "wrong-pass-9"})
        s2, b2 = self.call("POST", "/api/auth/login", {"username": "nobody99", "password": "wrong-pass-9"})
        self.assertEqual((s1, s2), (401, 401))
        self.assertEqual(b1, b2)

    def test_password_stored_as_hash_not_plaintext(self):
        self.register_login(password="correct-horse-1")
        row = self.conn.execute("SELECT password_hash FROM accounts WHERE username='player01'").fetchone()
        self.assertTrue(row["password_hash"].startswith("scrypt$"))
        self.assertNotIn("correct-horse-1", row["password_hash"])

    def test_session_token_not_stored_in_plaintext(self):
        token = self.register_login()
        rows = [r[0] for r in self.conn.execute("SELECT token_hash FROM sessions").fetchall()]
        self.assertNotIn(token, rows)

    def test_expired_session_rejected(self):
        token = self.register_login()
        self.clock.advance((24 * 3600 + 1) * 1000)
        s, _ = self.call("GET", "/api/characters", token=token)
        self.assertEqual(s, 401)

    def test_input_validation(self):
        s, b = self.call("POST", "/api/auth/register", {"username": "a", "password": "short"})
        self.assertEqual(s, 400)
        s, b = self.call("POST", "/api/auth/register", {"username": "valid_user", "password": "12345678",
                                                       "email": "not-an-email"})
        self.assertEqual(s, 400)
        s, b = self.call("POST", "/api/auth/login", {"username": 123, "password": "x"})
        self.assertEqual(s, 401)

    def test_invalid_json_body(self):
        s, b = self.api.handle("POST", "/api/auth/login", {}, b"{not json")
        self.assertEqual(s, 400)
        self.assertEqual(b["error"]["code"], "invalid_json")


class CharacterTests(ApiTestBase):
    def test_create_character_gives_starter_state(self):
        token = self.register_login()
        c = self.new_character(token, "Fighter1", "fighter")
        self.assertEqual(c["level"], 1)
        self.assertEqual(c["money"], game.STARTING_MONEY)
        self.assertEqual(c["hp"], c["max_hp"])
        self.assertEqual(c["stats"]["str"], 14)
        self.assertEqual(sorted(i["item_id"] for i in c["inventory"]), [1001, 1002, 1003])
        self.assertEqual(len(c["skills"]), len(game.STARTER_SKILL_IDS))

    def test_character_limit_and_name_rules(self):
        token = self.register_login()
        self.new_character(token, "Alpha", "mage")
        self.new_character(token, "Bravo", "archer")
        self.new_character(token, "Charlie", "fighter")
        s, b = self.call("POST", "/api/characters", {"name": "Delta", "gender": "female", "class": "mage"}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "character_limit")

    def test_thai_character_name_allowed_and_bad_name_rejected(self):
        token = self.register_login()
        c = self.new_character(token, "นักรบ", "fighter")
        self.assertEqual(c["name"], "นักรบ")
        s, _ = self.call("POST", "/api/characters", {"name": "x", "gender": "male", "class": "mage"}, token=token)
        self.assertEqual(s, 400)

    def test_other_account_cannot_see_character(self):
        t1 = self.register_login("owner01")
        c = self.new_character(t1)
        t2 = self.register_login("other01")
        s, b = self.call("GET", f"/api/characters/{c['id']}", token=t2)
        self.assertEqual(s, 404)
        self.assertEqual(b["error"]["code"], "character_not_found")
        s, _ = self.call("DELETE", f"/api/characters/{c['id']}", token=t2)
        self.assertEqual(s, 404)

    def test_delete_character(self):
        token = self.register_login()
        c = self.new_character(token)
        s, _ = self.call("DELETE", f"/api/characters/{c['id']}", token=token)
        self.assertEqual(s, 200)
        s, _ = self.call("GET", f"/api/characters/{c['id']}", token=token)
        self.assertEqual(s, 404)

    def test_wrong_method_returns_405(self):
        token = self.register_login()
        s, b = self.call("PUT", "/api/characters", token=token)
        self.assertEqual(s, 405)


class CombatTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login()
        self.char = self.new_character(self.token)
        self.cid = self.char["id"]
        # การโจมตีต้องทำในโลกเกม (ตำแหน่งของผู้เล่นใช้ตรวจระยะ)
        s, _ = self.call("POST", f"/api/characters/{self.cid}/world/enter", token=self.token)
        self.assertEqual(s, 200)

    def attack(self, monster_id=1, key=None):
        return self.call("POST", f"/api/characters/{self.cid}/combat/attack",
                         {"monster_id": monster_id}, token=self.token, key=key)

    def walk_to(self, x, y):
        """เดินเข้าหาเป้าหมายด้วยคำขอเดินจริง (ผ่านการตรวจระยะ/สิ่งกีดขวางของ Server)"""
        s, b = self.call("POST", f"/api/characters/{self.cid}/world/move", {"x": x, "y": y}, token=self.token)
        self.assertEqual(s, 200, b)
        self.clock.advance(60)

    def test_attack_out_of_range_rejected(self):
        # เกิดที่ 0.5,0.5 ส่วนมอนสเตอร์ 1 อยู่ที่ 0.15,0.75 → ไกลเกินระยะ
        s, b = self.attack(1)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "out_of_range")
        self.assertIn("monster", b)
        self.assertEqual(b["range"], game.BASIC_ATTACK_RANGE)

    def test_attack_damage_is_computed_by_server(self):
        self.walk_to(0.44, 0.57)  # เข้าใกล้ตุ๊กตาซ้อม
        s, b = self.attack(3)
        self.assertEqual(s, 200, b)
        self.assertIn(b["damage"], range(0, 200))
        self.assertLessEqual(b["monster"]["hp"], 80)
        self.assertEqual(b["monster"]["max_hp"], 80)
        # ตัวเลขดาเมจที่ Client ส่งมาถูกเมิน (รอพ้นคูลดาวน์ของครั้งก่อน)
        self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/attack",
                         {"monster_id": 3, "damage": 999999}, token=self.token)
        self.assertEqual(s, 200)
        self.assertLess(b["damage"], 999999)

    def test_attack_cooldown_blocks_second_hit(self):
        self.walk_to(0.44, 0.57)
        s, b = self.attack(3)
        self.assertEqual(s, 200)
        s, b = self.attack(3)  # ทันทีทันใด → ยังติดคูลดาวน์
        self.assertEqual(s, 429)
        self.assertEqual(b["error"]["code"], "attack_too_fast")
        self.assertGreater(b["retry_after_ms"], 0)
        self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        self.assertEqual(self.attack(3)[0], 200)

    def test_out_of_range_attack_does_not_start_cooldown(self):
        s, _ = self.attack(1)
        self.assertEqual(s, 409)
        self.walk_to(0.44, 0.57)
        self.assertEqual(self.attack(3)[0], 200)

    def test_kill_gives_exp_and_money_then_monster_respawns(self):
        self.walk_to(0.44, 0.57)
        killed = False
        for _ in range(200):
            s, b = self.attack(3)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            self.assertEqual(s, 200, b)
            if b["monster"]["dead"]:
                killed = True
                self.assertEqual(b["rewards"]["exp"], 20)
                self.assertEqual(b["rewards"]["money"], 10)
                break
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        self.assertTrue(killed)
        self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        s, b = self.attack(3)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "monster_dead")
        self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
        s, mons = self.call("GET", "/api/world/monsters", token=self.token)
        self.assertEqual(s, 200)
        m1 = next(m for m in mons["monsters"] if m["monster_id"] == 3)
        self.assertFalse(m1["dead"])
        self.assertEqual(m1["hp"], m1["max_hp"])

    def test_attack_requires_world_and_monster_in_map(self):
        self.call("POST", f"/api/characters/{self.cid}/world/leave", token=self.token)
        s, b = self.attack(3)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_in_world")

    def test_unknown_monster_404_and_bad_monster_id_400(self):
        s, b = self.attack(9999)
        self.assertEqual(s, 404)
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/attack", {"monster_id": "1"}, token=self.token)
        self.assertEqual(s, 400)

    def test_skill_cooldown_mana_and_range(self):
        # ตุ๊กตาซ้อมมี HP 80: ตั้ง HP ให้สูงพอก่อน เพื่อวัดคูลดาวน์/MP ได้หลายครั้ง
        self.conn.execute("UPDATE monsters SET max_hp = 5000, hp = 5000 WHERE monster_id = 3")
        self.walk_to(0.44, 0.57)   # เข้าใกล้ตุ๊กตาซ้อม (ระยะสกิล 1 = 0.12)
        url = f"/api/characters/{self.cid}/combat/skill"
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["skill"]["skill_id"], 1)
        self.assertGreater(b["damage"], 0)
        # คูลดาวน์ของสกิล (3 วินาที) ยังไม่ผ่าน
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 429)
        self.assertEqual(b["error"]["code"], "skill_cooldown")
        self.clock.advance(3001)
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 200, b)
        # MP ไม่พอ
        self.conn.execute("UPDATE characters SET mp = 0 WHERE id = ?", (self.cid,))
        self.clock.advance(10_000)
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_enough_mp")

    def test_skill_out_of_range_is_rejected(self):
        # ดอกไฟ (skill 2) มีระยะ 0.35 ตุ๊กตาซ้อมอยู่ห่าง 0.09 → อยู่ในระยะตั้งแต่จุดเกิด
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 200, b)
        # มอนสเตอร์ 1 อยู่ไกลเกินระยะของทุกสกิล
        self.clock.advance(10_000)
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "out_of_range")
        self.assertGreater(b["distance"], b["range"])

    def test_sp_skill_uses_sp_not_mp(self):
        self.walk_to(0.44, 0.57)
        mp_before = self.conn.execute("SELECT mp FROM characters WHERE id = ?", (self.cid,)).fetchone()[0]
        sp_before = self.conn.execute("SELECT sp FROM characters WHERE id = ?", (self.cid,)).fetchone()[0]
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 3, "monster_id": 3}, token=self.token)  # Quick Step ใช้ SP 15
        self.assertEqual(s, 200, b)
        self.assertEqual(b["skill"]["cost_stat"], "sp")
        row = self.conn.execute("SELECT mp, sp FROM characters WHERE id = ?", (self.cid,)).fetchone()
        self.assertEqual(row["mp"], mp_before)          # MP ไม่ถูกใช้
        self.assertEqual(row["sp"], sp_before - 15)

    def test_skill_not_learned(self):
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 200)
        self.conn.execute("DELETE FROM character_skills WHERE character_id = ? AND skill_id = 2", (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 3}, token=self.token)
        self.assertEqual(s, 404)

    def test_dead_character_cannot_attack_until_revived(self):
        self.conn.execute("UPDATE characters SET hp = 0 WHERE id = ?", (self.cid,))
        s, b = self.attack()
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "character_dead")
        s, b = self.call("POST", f"/api/characters/{self.cid}/revive", token=self.token)
        self.assertEqual(s, 200)
        self.assertFalse(b["dead"])
        s, b = self.call("POST", f"/api/characters/{self.cid}/revive", token=self.token)
        self.assertEqual(s, 409)

    def test_level_up_restores_and_grows_stats(self):
        self.walk_to(0.44, 0.57)
        self.conn.execute("UPDATE characters SET exp = 90 WHERE id = ?", (self.cid,))
        for _ in range(200):
            s, b = self.attack(3)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            if s == 200 and b["rewards"] and b["rewards"]["levels_gained"] > 0:
                break
            if s == 409 and b["error"]["code"] == "monster_dead":
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
                continue
            if s == 200 and b["player"]["dead"]:
                self.call("POST", f"/api/characters/{self.cid}/revive", token=self.token)
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        c = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        self.assertGreaterEqual(c["level"], 2)
        self.assertEqual(c["hp"], c["max_hp"])


class QuestTests(ApiTestBase):
    def test_hunt_quest_completes_and_pays_reward(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        # เควสต์ 2 = ตีตุ๊กตาซ้อม 3 ครั้ง (ตุ๊กตาอยู่ในระยะหลังเดินเข้าใกล้)
        s, b = self.call("POST", f"/api/characters/{cid}/quests/2/accept", token=token)
        self.assertEqual(s, 201)
        self.assertEqual(b["quests"][0]["status"], "accepted")
        money_before = b["money"]
        self.call("POST", f"/api/characters/{cid}/world/enter", token=token)
        self.assertEqual(self.call("POST", f"/api/characters/{cid}/world/move",
                                   {"x": 0.44, "y": 0.57}, token=token)[0], 200)
        for _ in range(2000):
            s, b = self.call("POST", f"/api/characters/{cid}/combat/attack", {"monster_id": 3}, token=token)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            if s == 409 and b["error"]["code"] == "monster_dead":
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
                continue
            if s == 409 and b["error"]["code"] == "character_dead":
                self.call("POST", f"/api/characters/{cid}/revive", token=token)
                continue
            self.assertEqual(s, 200, b)
            if b["quest_progress"] and b["quest_progress"][0]["status"] == "rewarded":
                break
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        c = self.call("GET", f"/api/characters/{cid}", token=token)[1]
        self.assertEqual(c["quests"][0]["status"], "rewarded")
        self.assertEqual(c["quests"][0]["progress"], 3)
        self.assertGreater(c["money"], money_before)

    def test_accept_quest_twice_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.call("POST", f"/api/characters/{cid}/quests/1/accept", token=token)
        s, b = self.call("POST", f"/api/characters/{cid}/quests/1/accept", token=token)
        self.assertEqual(s, 409)


class ShopAndInventoryTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login()
        self.cid = self.new_character(self.token)["id"]

    def buy(self, item_id, qty, key=None):
        return self.call("POST", f"/api/characters/{self.cid}/shop/buy",
                         {"item_id": item_id, "quantity": qty}, token=self.token, key=key)

    def test_buy_deducts_money_and_stacks(self):
        s, b = self.buy(1001, 5)
        self.assertEqual(s, 200)
        self.assertEqual(b["money"], game.STARTING_MONEY - 5 * 20)
        potion = next(i for i in b["inventory"] if i["item_id"] == 1001)
        self.assertEqual(potion["quantity"], 8)

    def test_buy_without_money_rejected_and_nothing_changes(self):
        s, b = self.buy(1001, 999)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_enough_money")
        c = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        self.assertEqual(c["money"], game.STARTING_MONEY)

    def test_buy_quest_item_not_for_sale(self):
        s, b = self.buy(2001, 1)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_for_sale")

    def test_idempotency_key_prevents_double_charge(self):
        s1, b1 = self.buy(1002, 2, key="buy-key-0001")
        s2, b2 = self.buy(1002, 2, key="buy-key-0001")
        self.assertEqual((s1, s2), (200, 200))
        self.assertEqual(b1, b2)
        c = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        self.assertEqual(c["money"], game.STARTING_MONEY - 2 * 25)

    def test_bad_idempotency_key_rejected(self):
        s, b = self.buy(1002, 1, key="bad key!")
        self.assertEqual(s, 400)
        self.assertEqual(b["error"]["code"], "invalid_idempotency_key")

    def test_inventory_full(self):
        self.conn.execute("UPDATE characters SET money = 1000000000 WHERE id = ?", (self.cid,))
        for _ in range(27):
            s, _ = self.buy(3001, 999)
            self.assertEqual(s, 200)
        s, b = self.buy(3001, 1)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "inventory_full")

    def test_use_potion_heals_and_consumes(self):
        self.conn.execute("UPDATE characters SET hp = 10 WHERE id = ?", (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/inventory/use", {"slot": 0}, token=self.token)
        self.assertEqual(s, 200)
        self.assertEqual(b["hp"], min(b["max_hp"], 110))
        slot0 = next(i for i in b["inventory"] if i["slot"] == 0)
        self.assertEqual(slot0["quantity"], 2)

    def test_use_empty_or_non_consumable_slot(self):
        s, b = self.call("POST", f"/api/characters/{self.cid}/inventory/use", {"slot": 17}, token=self.token)
        self.assertEqual(s, 404)
        self.conn.execute("INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 3001, 1, 20)",
                          (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/inventory/use", {"slot": 20}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_consumable")

    def test_sell_pays_half_and_removes_stack(self):
        self.conn.execute("INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 3001, 10, 20)",
                          (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/shop/sell",
                         {"slot": 20, "quantity": 10}, token=self.token)
        self.assertEqual(s, 200)
        self.assertEqual(b["money"], game.STARTING_MONEY + 10 * (5 // 2))
        self.assertIsNone(next((i for i in b["inventory"] if i["slot"] == 20), None))

    def test_sell_quest_item_rejected(self):
        self.conn.execute("INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 2001, 1, 21)",
                          (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/shop/sell",
                         {"slot": 21, "quantity": 1}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_sellable")


class HttpSmokeTest(unittest.TestCase):
    def test_real_http_round_trip(self):
        clock = Clock()
        conn = connect(":memory:")
        init_db(conn)
        api = Api(conn, rng=random.Random(7), clock_ms=clock.ms_now, clock_s=clock.s_now)
        httpd = make_server(api, "127.0.0.1", 0)
        port = httpd.server_address[1]
        t = threading.Thread(target=httpd.serve_forever, daemon=True)
        t.start()
        base = f"http://127.0.0.1:{port}"
        try:
            with urllib.request.urlopen(base + "/api/health") as r:
                self.assertEqual(r.status, 200)
                self.assertTrue(json.loads(r.read())["ok"])

            req = urllib.request.Request(
                base + "/api/auth/register",
                data=json.dumps({"username": "httpuser", "password": "long-enough-pw"}).encode(),
                headers={"Content-Type": "application/json"}, method="POST")
            with urllib.request.urlopen(req) as r:
                self.assertEqual(r.status, 201)

            req = urllib.request.Request(base + "/api/characters", data=b"{}", method="POST")
            with self.assertRaises(HTTPError) as ctx:
                urllib.request.urlopen(req)
            self.assertEqual(ctx.exception.code, 401)
            ctx.exception.close()

            big = b"x" * (64 * 1024 + 1)
            req = urllib.request.Request(base + "/api/auth/login", data=big, method="POST")
            with self.assertRaises(HTTPError) as ctx:
                urllib.request.urlopen(req)
            self.assertEqual(ctx.exception.code, 413)
            ctx.exception.close()
        finally:
            httpd.shutdown()
            httpd.server_close()
            conn.close()


class LoginLockoutTests(ApiTestBase):
    def test_lock_after_repeated_failures_even_with_correct_password(self):
        self.register_login("locker01", "correct-horse-1")
        for _ in range(game_login_max()):
            s, b = self.call("POST", "/api/auth/login", {"username": "locker01", "password": "bad-password-x"})
            self.assertEqual(s, 401)
        s, b = self.call("POST", "/api/auth/login", {"username": "locker01", "password": "correct-horse-1"})
        self.assertEqual(s, 429)
        self.assertEqual(b["error"]["code"], "too_many_attempts")

    def test_lock_expires_and_success_clears_counter(self):
        from ranserver.api import LOGIN_LOCK_S
        self.register_login("locker02", "correct-horse-1")
        for _ in range(game_login_max()):
            self.call("POST", "/api/auth/login", {"username": "locker02", "password": "bad-password-x"})
        self.clock.advance((LOGIN_LOCK_S + 1) * 1000)
        s, b = self.call("POST", "/api/auth/login", {"username": "locker02", "password": "correct-horse-1"})
        self.assertEqual(s, 200)
        row = self.conn.execute("SELECT 1 FROM login_failures WHERE username='locker02'").fetchone()
        self.assertIsNone(row)

    def test_failures_outside_window_do_not_accumulate(self):
        from ranserver.api import LOGIN_WINDOW_S
        self.register_login("locker03", "correct-horse-1")
        for _ in range(game_login_max() - 1):
            self.call("POST", "/api/auth/login", {"username": "locker03", "password": "bad-password-x"})
        self.clock.advance((LOGIN_WINDOW_S + 1) * 1000)
        self.call("POST", "/api/auth/login", {"username": "locker03", "password": "bad-password-x"})
        s, _ = self.call("POST", "/api/auth/login", {"username": "locker03", "password": "correct-horse-1"})
        self.assertEqual(s, 200)


def game_login_max():
    from ranserver.api import LOGIN_MAX_FAILURES
    return LOGIN_MAX_FAILURES


class EquipmentTests(ApiTestBase):
    def setUp(self):
        super().setUp()
        self.token = self.register_login()
        self.cid = self.new_character(self.token)["id"]
        self.conn.execute("UPDATE characters SET money = 100000 WHERE id = ?", (self.cid,))

    def buy(self, item_id, qty=1):
        return self.call("POST", f"/api/characters/{self.cid}/shop/buy",
                         {"item_id": item_id, "quantity": qty}, token=self.token)

    def equip(self, slot):
        return self.call("POST", f"/api/characters/{self.cid}/equip", {"slot": slot}, token=self.token)

    def unequip(self, name):
        return self.call("POST", f"/api/characters/{self.cid}/unequip", {"equip_slot": name}, token=self.token)

    def free_slot(self, view):
        used = {i["slot"] for i in view["inventory"]}
        return next(s for s in range(game.INVENTORY_SLOTS) if s not in used)

    def test_equip_moves_item_out_of_inventory_and_grants_bonus(self):
        s, c = self.buy(4001, 1)
        self.assertEqual(s, 200)
        slot = next(i["slot"] for i in c["inventory"] if i["item_id"] == 4001)
        s, c = self.equip(slot)
        self.assertEqual(s, 200)
        self.assertEqual(c["equipment"]["weapon"]["item_id"], 4001)
        self.assertIsNone(next((i for i in c["inventory"] if i["item_id"] == 4001), None))
        self.assertEqual(self.api._equipment_bonus(self.cid), (6, 0))

    def test_unequip_returns_item_to_inventory(self):
        s, c = self.buy(4002, 1)
        slot = next(i["slot"] for i in c["inventory"] if i["item_id"] == 4002)
        self.equip(slot)
        s, c = self.unequip("armor")
        self.assertEqual(s, 200)
        self.assertIsNone(c["equipment"]["armor"])
        self.assertTrue(any(i["item_id"] == 4002 for i in c["inventory"]))
        s, _ = self.unequip("armor")
        self.assertEqual(s, 404)

    def test_swap_returns_previous_weapon(self):
        s, c = self.buy(4001, 2)
        slots = [i["slot"] for i in c["inventory"] if i["item_id"] == 4001]
        self.assertEqual(len(slots), 2)
        self.equip(slots[0])
        s, c = self.equip(slots[1])
        self.assertEqual(s, 200)
        self.assertEqual(c["equipment"]["weapon"]["item_id"], 4001)
        self.assertEqual(sum(i["quantity"] for i in c["inventory"] if i["item_id"] == 4001), 1)

    def test_not_equippable_item_rejected(self):
        s, c = self.call("POST", f"/api/characters/{self.cid}/shop/buy",
                         {"item_id": 1001, "quantity": 1}, token=self.token)
        slot = next(i["slot"] for i in c["inventory"] if i["item_id"] == 1001)
        s, b = self.equip(slot)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_equippable")

    def test_unequip_with_full_inventory_rolls_back_completely(self):
        s, c = self.buy(4001, 1)
        sword_slot = next(i["slot"] for i in c["inventory"] if i["item_id"] == 4001)
        self.equip(sword_slot)
        # ทำให้กระเป๋าเต็มทุกช่อง แล้วลองถอดดาบ: ต้องล้มเหลวและอุปกรณ์ยังอยู่ที่เดิม
        used = {i["slot"] for i in self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]["inventory"]}
        for slot in range(game.INVENTORY_SLOTS):
            if slot not in used:
                self.conn.execute(
                    "INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 3001, 1, ?)",
                    (self.cid, slot))
        before = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        s, b = self.unequip("weapon")
        self.assertEqual(s, 409)
        after = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        self.assertEqual(before["equipment"], after["equipment"])
        self.assertEqual(before["inventory"], after["inventory"])
        self.assertEqual(after["equipment"]["weapon"]["item_id"], 4001)

    def test_armor_bonus_value_is_defense(self):
        # เกราะเพิ่มค่าป้องกันจริง: ค่า defense รวมในการคำนวณตอบโต้ต้องเพิ่มขึ้น
        s, c = self.buy(4002, 1)
        slot = next(i["slot"] for i in c["inventory"] if i["item_id"] == 4002)
        self.equip(slot)
        self.assertEqual(self.api._equipment_bonus(self.cid), (0, 3))


if __name__ == "__main__":
    unittest.main()
