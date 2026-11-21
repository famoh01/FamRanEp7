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
        self.assertEqual(len(c["skills"]), 2)

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

    def attack(self, monster_id=1, key=None):
        return self.call("POST", f"/api/characters/{self.cid}/combat/attack",
                         {"monster_id": monster_id}, token=self.token, key=key)

    def test_attack_damage_is_computed_by_server(self):
        s, b = self.attack()
        self.assertEqual(s, 200)
        self.assertIn(b["damage"], range(0, 200))
        self.assertLessEqual(b["monster"]["hp"], 120)
        self.assertEqual(b["monster"]["max_hp"], 120)

    def test_client_cannot_send_damage(self):
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/attack",
                         {"monster_id": 1, "damage": 999999}, token=self.token)
        self.assertEqual(s, 200)
        self.assertLess(b["damage"], 999999)

    def test_kill_gives_exp_and_money_then_monster_respawns(self):
        killed = False
        for _ in range(200):
            s, b = self.attack(1)
            if s == 409:
                break
            if b["monster"]["dead"]:
                killed = True
                self.assertEqual(b["rewards"]["exp"], 50)
                self.assertEqual(b["rewards"]["money"], 30)
                break
        self.assertTrue(killed)
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/attack", {"monster_id": 1}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "monster_dead")
        self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
        s, mons = self.call("GET", "/api/world/monsters", token=self.token)
        m1 = next(m for m in mons["monsters"] if m["monster_id"] == 1)
        self.assertFalse(m1["dead"])
        self.assertEqual(m1["hp"], m1["max_hp"])

    def test_unknown_monster_404_and_bad_monster_id_400(self):
        s, b = self.attack(9999)
        self.assertEqual(s, 404)
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/attack", {"monster_id": "1"}, token=self.token)
        self.assertEqual(s, 400)

    def test_skill_cooldown_and_mana(self):
        url = f"/api/characters/{self.cid}/combat/skill"
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 200)
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 429)
        self.assertEqual(b["error"]["code"], "skill_cooldown")
        self.clock.advance(3001)
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 200)
        self.conn.execute("UPDATE characters SET mp = 0 WHERE id = ?", (self.cid,))
        self.clock.advance(10_000)
        s, b = self.call("POST", url, {"skill_id": 1, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_enough_mp")

    def test_skill_not_learned(self):
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 1}, token=self.token)
        self.assertEqual(s, 200)
        self.conn.execute("DELETE FROM character_skills WHERE character_id = ? AND skill_id = 2", (self.cid,))
        s, b = self.call("POST", f"/api/characters/{self.cid}/combat/skill",
                         {"skill_id": 2, "monster_id": 1}, token=self.token)
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
        self.conn.execute("UPDATE characters SET exp = 90 WHERE id = ?", (self.cid,))
        for _ in range(200):
            s, b = self.attack(1)
            if s == 200 and b["rewards"] and b["rewards"]["levels_gained"] > 0:
                break
            if s == 409 and b["error"]["code"] == "monster_dead":
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
            if s == 200 and b["player"]["dead"]:
                self.call("POST", f"/api/characters/{self.cid}/revive", token=self.token)
        c = self.call("GET", f"/api/characters/{self.cid}", token=self.token)[1]
        self.assertGreaterEqual(c["level"], 2)
        self.assertEqual(c["hp"], c["max_hp"])


class QuestTests(ApiTestBase):
    def test_hunt_quest_completes_and_pays_reward(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        s, b = self.call("POST", f"/api/characters/{cid}/quests/1/accept", token=token)
        self.assertEqual(s, 201)
        self.assertEqual(b["quests"][0]["status"], "accepted")
        money_before = b["money"]
        for _ in range(2000):
            s, b = self.call("POST", f"/api/characters/{cid}/combat/attack", {"monster_id": 1}, token=token)
            if s == 409 and b["error"]["code"] == "monster_dead":
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
                continue
            if s == 409 and b["error"]["code"] == "character_dead":
                self.call("POST", f"/api/characters/{cid}/revive", token=token)
                continue
            self.assertEqual(s, 200, b)
            if b["quest_progress"] and b["quest_progress"][0]["status"] == "rewarded":
                break
        c = self.call("GET", f"/api/characters/{cid}", token=token)[1]
        self.assertEqual(c["quests"][0]["status"], "rewarded")
        self.assertEqual(c["quests"][0]["progress"], 5)
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


if __name__ == "__main__":
    unittest.main()
