# ran_mobile/Server/tests/test_round4.py
# เทสต์ของระบบรอบ 4: แผนที่/ประตูวาร์ป, NPC/ร้านค้า, อุปกรณ์, แชท, ปาร์ตี้
# ใช้เซิร์ฟเวอร์จริง (Api + sqlite in-memory + content.json จริง) ไม่มี mock
import math
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ranserver import game, social  # noqa: E402
from test_api import ApiTestBase  # noqa: E402


class Round4Base(ApiTestBase):
    def enter(self, cid, token):
        s, b = self.call("POST", f"/api/characters/{cid}/world/enter", token=token)
        self.assertEqual(s, 200, b)
        return b

    def move(self, cid, token, x, y):
        s, b = self.call("POST", f"/api/characters/{cid}/world/move", {"x": x, "y": y}, token=token)
        self.assertEqual(s, 200, b)
        return b

    def player_x(self, cid):
        return self.conn.execute("SELECT pos_x, pos_y, map_id FROM characters WHERE id = ?",
                                 (cid,)).fetchone()


class MapAndGateTests(Round4Base):
    def test_content_has_three_maps_with_monsters(self):
        maps = self.conn.execute("SELECT map_id, name FROM maps ORDER BY map_id").fetchall()
        self.assertEqual([m["map_id"] for m in maps], [1, 2, 3])
        for m in maps:
            count = self.conn.execute("SELECT COUNT(*) FROM monsters WHERE map_id = ?",
                                      (m["map_id"],)).fetchone()[0]
            self.assertGreater(count, 0, f"แผนที่ {m['map_id']} ต้องมีมอนสเตอร์")

    def test_maps_endpoint_lists_all_maps(self):
        token = self.register_login()
        s, b = self.call("GET", "/api/maps", token=token)
        self.assertEqual(s, 200)
        self.assertEqual(len(b["maps"]), 3)
        self.assertIn("วิทยาเขตโรงเรียน", [m["name"] for m in b["maps"]])

    def test_world_sync_filters_players_and_monsters_by_map(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("GET", f"/api/characters/{cid}/world/sync", token=token)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["map_id"], 1)
        for mon in b["monsters"]:
            row = self.conn.execute("SELECT map_id FROM monsters WHERE monster_id = ?",
                                    (mon["monster_id"],)).fetchone()
            self.assertEqual(row["map_id"], 1)
        for npc in b["npcs"]:
            self.assertEqual(npc["map_id"], 1)
        for g in b["gates"]:
            self.assertEqual(g["to_map"], 2)   # แผนที่ 1 มีประตูเดียวคือไปตลาด

    def test_gate_list_shows_requirements(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("GET", f"/api/characters/{cid}/map/gates", token=token)
        self.assertEqual(s, 200)
        gates = b["gates"]
        self.assertEqual(len(gates), 1)
        self.assertEqual(gates[0]["to_map_name"], "ตลาดปลอดภาษี")
        self.assertFalse(gates[0]["in_range"])

    def test_gate_rejects_when_too_far(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/map/gate", {"gate_id": 1}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "too_far")

    def test_gate_moves_character_to_other_map(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        gate = self.conn.execute("SELECT * FROM map_gates WHERE gate_id = 1").fetchone()
        self.move(cid, token, gate["from_x"], gate["from_y"])
        s, b = self.call("POST", f"/api/characters/{cid}/map/gate", {"gate_id": 1}, token=token)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["to_map"], 2)
        row = self.player_x(cid)
        self.assertEqual(row["map_id"], 2)
        self.assertAlmostEqual(row["pos_x"], gate["to_x"], places=4)
        # ผู้เล่นอยู่ในแผนที่ 2 แล้ว -> ซิงก์แสดงมอนสเตอร์ของแผนที่ 2
        s, sync = self.call("GET", f"/api/characters/{cid}/world/sync", token=token)
        self.assertEqual(sync["map_id"], 2)
        ids = {m["monster_id"] for m in sync["monsters"]}
        self.assertEqual(ids, {7})          # Market Rat อยู่แผนที่ 2
        move_row = self.conn.execute("SELECT * FROM map_moves WHERE character_id = ?",
                                     (cid,)).fetchone()
        self.assertEqual((move_row["from_map"], move_row["to_map"]), (1, 2))

    def test_gate_requires_level_and_reports_reason(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        # ประตู 3 (ตลาด -> ทุ่งล่า) ต้องเลเวล 3; ย้ายตัวละครไปแผนที่ 2 ก่อน
        gate = self.conn.execute("SELECT * FROM map_gates WHERE gate_id = 3").fetchone()
        self.conn.execute("UPDATE characters SET map_id = 2, pos_x = ?, pos_y = ? WHERE id = ?",
                          (gate["from_x"], gate["from_y"], cid))
        s, b = self.call("POST", f"/api/characters/{cid}/map/gate", {"gate_id": 3}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "gate_locked")
        self.assertIn("3", b["error"]["message"])

    def test_gate_wrong_map_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/map/gate", {"gate_id": 3}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "wrong_map")


class NpcShopTests(Round4Base):
    def npc_pos(self, npc_id):
        return self.conn.execute("SELECT * FROM npcs WHERE npc_id = ?", (npc_id,)).fetchone()

    def test_talk_requires_proximity(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        # NPC 103 (ครูใหญ่) อยู่ที่ (0.5, 0.3) ห่างจากจุดเกิด 0.2 -> ไกลเกินระยะคุย 0.10
        s, b = self.call("POST", f"/api/characters/{cid}/npc/103/talk", {}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "npc_too_far")

    def test_talk_returns_dialog_and_real_shop_stock(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        npc = self.npc_pos(102)
        self.move(cid, token, npc["pos_x"], npc["pos_y"])
        s, b = self.call("POST", f"/api/characters/{cid}/npc/102/talk", {}, token=token)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["npc"]["name"], "พยาบาลประจำวิทยาเขต")
        self.assertEqual(b["npc"]["shop_name"], "ตู้ยาวิทยาเขต")
        self.assertTrue(b["dialog"])
        shop_ids = {i["item_id"] for i in b["shop"]}
        self.assertEqual(shop_ids, {1001, 1002})
        self.assertIn("money", b)

    def test_buy_deducts_real_money_and_adds_item(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        npc = self.npc_pos(102)
        self.move(cid, token, npc["pos_x"], npc["pos_y"])
        money_before = self.conn.execute("SELECT money FROM characters WHERE id = ?", (cid,)).fetchone()[0]
        s, b = self.call("POST", f"/api/characters/{cid}/npc/102/buy",
                         {"item_id": 1001, "quantity": 2}, token=token)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["paid"], 2 * 22)          # ราคาจาก shop_items ไม่ใช่จาก Client
        money_after = self.conn.execute("SELECT money FROM characters WHERE id = ?", (cid,)).fetchone()[0]
        self.assertEqual(money_after, money_before - 44)
        qty = self.conn.execute(
            "SELECT COALESCE(SUM(quantity), 0) FROM inventory WHERE character_id = ? AND item_id = 1001",
            (cid,)).fetchone()[0]
        self.assertGreaterEqual(qty, 2)

    def test_buy_rejected_when_not_enough_money(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.conn.execute("UPDATE characters SET money = 5 WHERE id = ?", (cid,))
        self.enter(cid, token)
        npc = self.npc_pos(102)
        self.conn.execute("UPDATE characters SET pos_x = ?, pos_y = ? WHERE id = ?",
                          (npc["pos_x"], npc["pos_y"], cid))
        s, b = self.call("POST", f"/api/characters/{cid}/npc/102/buy",
                         {"item_id": 1001, "quantity": 1}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_enough_money")
        money = self.conn.execute("SELECT money FROM characters WHERE id = ?", (cid,)).fetchone()[0]
        self.assertEqual(money, 5)      # ไม่ถูกหักเงินเมื่อซื้อไม่สำเร็จ

    def test_buy_item_not_in_shop_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        npc = self.npc_pos(102)
        self.conn.execute("UPDATE characters SET pos_x = ?, pos_y = ? WHERE id = ?",
                          (npc["pos_x"], npc["pos_y"], cid))
        s, b = self.call("POST", f"/api/characters/{cid}/npc/102/buy",
                         {"item_id": 4001, "quantity": 1}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_sold_here")

    def test_sell_uses_npc_sell_rate(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        npc = self.npc_pos(202)          # เภสัชกรตลาด รับซื้อ 40%
        self.conn.execute("UPDATE characters SET map_id = 2, pos_x = ?, pos_y = ? WHERE id = ?",
                          (npc["pos_x"], npc["pos_y"], cid))
        slot = self.conn.execute(
            "SELECT slot FROM inventory WHERE character_id = ? ORDER BY slot LIMIT 1",
            (cid,)).fetchone()["slot"]
        item = self.conn.execute(
            "SELECT it.item_id, it.price FROM inventory inv JOIN items it ON it.item_id = inv.item_id"
            " WHERE inv.character_id = ? AND inv.slot = ?", (cid, slot)).fetchone()
        money_before = self.conn.execute("SELECT money FROM characters WHERE id = ?",
                                         (cid,)).fetchone()[0]
        s, b = self.call("POST", f"/api/characters/{cid}/npc/202/sell",
                         {"slot": slot, "quantity": 1}, token=token)
        self.assertEqual(s, 200, b)
        expected = social.sell_price(item["price"], 1, npc["sell_rate_percent"])
        self.assertEqual(b["income"], expected)
        money_after = self.conn.execute("SELECT money FROM characters WHERE id = ?",
                                        (cid,)).fetchone()[0]
        self.assertEqual(money_after, money_before + expected)

    def test_npc_on_other_map_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/npc/201/talk", {}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "npc_other_map")


class EquipmentTests(Round4Base):
    def test_equipping_weapon_changes_server_side_attack(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        # ให้ดาบนักเรียน (attack +12) ในกระเป๋า
        self.conn.execute(
            "INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 4003, 1, 5)",
            (cid,))
        s, b = self.call("GET", f"/api/characters/{cid}", token=token)
        atk_before = b["player"]["atk"] if "player" in b else b["character"]["atk"]
        s, b = self.call("POST", f"/api/characters/{cid}/equip", {"slot": 5}, token=token)
        self.assertEqual(s, 200, b)
        s, b = self.call("GET", f"/api/characters/{cid}", token=token)
        atk_after = b["player"]["atk"] if "player" in b else b["character"]["atk"]
        self.assertEqual(atk_after - atk_before, 12)
        eq = self.conn.execute("SELECT item_id FROM character_equipment WHERE character_id = ?"
                               " AND equip_slot = 'weapon'", (cid,)).fetchone()
        self.assertEqual(eq["item_id"], 4003)

    def test_armor_increases_defense_and_unequip_reverses(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.conn.execute(
            "INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, 4004, 1, 6)",
            (cid,))
        def defense():
            s, b = self.call("GET", f"/api/characters/{cid}", token=token)
            return (b["player"]["def"] if "player" in b else b["character"]["def"])
        before = defense()
        self.call("POST", f"/api/characters/{cid}/equip", {"slot": 6}, token=token)
        self.assertEqual(defense() - before, 7)
        s, b = self.call("POST", f"/api/characters/{cid}/unequip", {"equip_slot": "armor"}, token=token)
        self.assertEqual(s, 200, b)
        self.assertEqual(defense(), before)

    def test_equipping_non_equipment_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        slot = self.conn.execute("SELECT slot FROM inventory WHERE character_id = ?"
                                 " ORDER BY slot LIMIT 1", (cid,)).fetchone()["slot"]
        s, b = self.call("POST", f"/api/characters/{cid}/equip", {"slot": slot}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "not_equippable")


class ChatTests(Round4Base):
    def test_map_chat_visible_to_player_on_same_map_only(self):
        t1 = self.register_login("chat01")
        t2 = self.register_login("chat02")
        c1 = self.new_character(t1, "Talker1")["id"]
        c2 = self.new_character(t2, "Talker2")["id"]
        self.enter(c1, t1)
        self.enter(c2, t2)
        s, b = self.call("POST", f"/api/characters/{c1}/chat", {"text": "สวัสดีทุกคน"}, token=t1)
        self.assertEqual(s, 200, b)
        self.assertEqual(b["channel"], "map")
        s, hist = self.call("GET", f"/api/characters/{c2}/chat", token=t2)
        self.assertEqual(s, 200)
        self.assertEqual([m["body"] for m in hist["messages"]], ["สวัสดีทุกคน"])
        # ผู้เล่นที่ย้ายไปแผนที่ 2 ต้องไม่เห็นข้อความของแผนที่ 1
        self.conn.execute("UPDATE characters SET map_id = 2 WHERE id = ?", (c2,))
        s, hist2 = self.call("GET", f"/api/characters/{c2}/chat", token=t2)
        self.assertEqual(hist2["messages"], [])

    def test_empty_chat_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/chat", {"text": "   "}, token=token)
        self.assertEqual(s, 400)
        self.assertEqual(b["error"]["code"], "invalid_chat")

    def test_chat_too_long_rejected(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/chat", {"text": "a" * 500}, token=token)
        self.assertEqual(s, 400)

    def test_chat_rate_limited(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        self.assertEqual(self.call("POST", f"/api/characters/{cid}/chat", {"text": "หนึ่ง"}, token=token)[0], 200)
        s, b = self.call("POST", f"/api/characters/{cid}/chat", {"text": "สอง"}, token=token)
        self.assertEqual(s, 429)
        self.assertEqual(b["error"]["code"], "chat_too_fast")
        self.clock.advance(1000)
        self.assertEqual(self.call("POST", f"/api/characters/{cid}/chat", {"text": "สาม"}, token=token)[0], 200)

    def test_party_prefix_requires_party(self):
        token = self.register_login()
        cid = self.new_character(token)["id"]
        self.enter(cid, token)
        s, b = self.call("POST", f"/api/characters/{cid}/chat", {"text": "#สวัสดี"}, token=token)
        self.assertEqual(s, 409)
        self.assertEqual(b["error"]["code"], "no_party")


class PartyTests(Round4Base):
    def two_players(self):
        t1 = self.register_login("party01")
        t2 = self.register_login("party02")
        c1 = self.new_character(t1, "Leader1")["id"]
        c2 = self.new_character(t2, "Member1")["id"]
        self.enter(c1, t1)
        self.enter(c2, t2)
        return (t1, c1), (t2, c2)

    def test_invite_accept_creates_party(self):
        (t1, c1), (t2, c2) = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c1}/party/invite",
                         {"character_id": c2}, token=t1)
        self.assertEqual(s, 201, b)
        self.assertEqual(b["invited"], "Member1")
        s, st = self.call("GET", f"/api/characters/{c2}/party", token=t2)
        self.assertEqual(len(st["invites"]), 1)
        s, b = self.call("POST", f"/api/characters/{c2}/party/accept",
                         {"party_id": st["invites"][0]["party_id"]}, token=t2)
        self.assertEqual(s, 200, b)
        self.assertEqual(len(b["party"]["members"]), 2)
        leaders = [m for m in b["party"]["members"] if m["is_leader"]]
        self.assertEqual(leaders[0]["name"], "Leader1")

    def test_invite_self_rejected(self):
        (t1, c1), _ = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c1}/party/invite",
                         {"character_id": c1}, token=t1)
        self.assertEqual(s, 400)
        self.assertEqual(b["error"]["code"], "cannot_invite_self")

    def test_accept_without_invite_rejected(self):
        (t1, c1), (t2, c2) = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c2}/party/accept",
                         {"party_id": 999}, token=t2)
        self.assertEqual(s, 404)

    def test_leave_party_promotes_new_leader(self):
        (t1, c1), (t2, c2) = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c1}/party/invite", {"character_id": c2}, token=t1)
        pid = b["party"]["party_id"]
        self.call("POST", f"/api/characters/{c2}/party/accept", {"party_id": pid}, token=t2)
        s, b = self.call("POST", f"/api/characters/{c1}/party/leave", {}, token=t1)
        self.assertEqual(s, 200, b)
        row = self.conn.execute("SELECT leader_id FROM parties WHERE party_id = ?", (pid,)).fetchone()
        self.assertEqual(row["leader_id"], c2)

    def test_party_exp_shared_with_nearby_member(self):
        (t1, c1), (t2, c2) = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c1}/party/invite", {"character_id": c2}, token=t1)
        pid = b["party"]["party_id"]
        self.call("POST", f"/api/characters/{c2}/party/accept", {"party_id": pid}, token=t2)
        # วางผู้เล่นทั้งสองไว้ใกล้ตุ๊กตาซ้อม (monster_id 3)
        mon = self.conn.execute("SELECT * FROM monsters WHERE monster_id = 3").fetchone()
        self.conn.execute("UPDATE characters SET pos_x = ?, pos_y = ? WHERE id IN (?, ?)",
                          (mon["pos_x"], mon["pos_y"] + 0.02, c1, c2))
        exp2_before = self.conn.execute("SELECT exp, level FROM characters WHERE id = ?",
                                        (c2,)).fetchone()
        # ตีจนมอนสเตอร์ตาย
        for _ in range(200):
            s, r = self.call("POST", f"/api/characters/{c1}/combat/attack",
                             {"monster_id": 3}, token=t1)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            if s == 409:
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
                continue
            self.assertEqual(s, 200, r)
            if r["monster"]["dead"]:
                self.assertTrue(r.get("party_exp"), "ต้องมีรายการ EXP ของปาร์ตี้")
                mate = [m for m in r["party_exp"] if m["character_id"] == c2]
                self.assertEqual(len(mate), 1)
                self.assertGreaterEqual(mate[0]["exp"], 1)
                break
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
        exp2_after = self.conn.execute("SELECT exp, level FROM characters WHERE id = ?",
                                       (c2,)).fetchone()
        self.assertGreater(exp2_after["exp"] + exp2_after["level"] * 1000,
                           exp2_before["exp"] + exp2_before["level"] * 1000)

    def test_party_exp_not_shared_across_maps(self):
        (t1, c1), (t2, c2) = self.two_players()
        s, b = self.call("POST", f"/api/characters/{c1}/party/invite", {"character_id": c2}, token=t1)
        pid = b["party"]["party_id"]
        self.call("POST", f"/api/characters/{c2}/party/accept", {"party_id": pid}, token=t2)
        self.conn.execute("UPDATE characters SET map_id = 2, pos_x = 0.3, pos_y = 0.6 WHERE id = ?", (c2,))
        mon = self.conn.execute("SELECT * FROM monsters WHERE monster_id = 3").fetchone()
        self.conn.execute("UPDATE characters SET pos_x = ?, pos_y = ? WHERE id = ?",
                          (mon["pos_x"], mon["pos_y"] + 0.02, c1))
        for _ in range(200):
            s, r = self.call("POST", f"/api/characters/{c1}/combat/attack",
                             {"monster_id": 3}, token=t1)
            if s == 429:
                self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)
                continue
            if s == 409:
                self.clock.advance(game.MONSTER_RESPAWN_MS + 1)
                continue
            if r["monster"]["dead"]:
                self.assertEqual(r.get("party_exp"), [])
                break
            self.clock.advance(game.BASIC_ATTACK_COOLDOWN_MS)


class SocialUnitTests(unittest.TestCase):
    """ทดสอบฟังก์ชันบริสุทธิ์ใน social.py (ไม่ต้องใช้ฐานข้อมูล)"""

    def test_split_channel(self):
        self.assertEqual(social.split_channel("hi"), ("map", "hi"))
        self.assertEqual(social.split_channel("#hi"), ("party", "hi"))
        self.assertEqual(social.split_channel("@somchai hi"), ("whisper", "somchai hi"))

    def test_buy_and_sell_price(self):
        self.assertEqual(social.buy_cost(22, 3), 66)
        self.assertEqual(social.sell_price(100, 1, 30), 30)
        self.assertEqual(social.sell_price(100, 2, 25), 50)
        self.assertEqual(social.sell_price(3, 1, 30), 1)      # ปัดลงแล้วอย่างน้อย 1
        with self.assertRaises(ValueError):
            social.buy_cost(10, 0)

    def test_exp_share_shares(self):
        shares = social.exp_share_shares(100, [10, 10])
        self.assertEqual(shares, [50, 50])
        shares = social.exp_share_shares(100, [30, 10])
        self.assertEqual(shares, [75, 25])
        self.assertEqual(len(social.exp_share_shares(0, [1, 1])), 2)

    def test_validate_chat(self):
        self.assertEqual(social.validate_chat("  hi  "), "hi")
        with self.assertRaises(ValueError):
            social.validate_chat("")
        with self.assertRaises(ValueError):
            social.validate_chat("x" * 200)
        with self.assertRaises(ValueError):
            social.validate_chat("bad\x00text")

    def test_gate_helpers(self):
        gate = {"required_level": 3, "required_money": 100}
        self.assertIn("เลเวล", social.gate_block_reason(gate, 1, 1000))
        self.assertIn("เงิน", social.gate_block_reason(gate, 5, 10))
        self.assertIsNone(social.gate_block_reason(gate, 5, 1000))
        self.assertTrue(social.gate_in_range(0.5, 0.5, 0.52, 0.5, 0.05))
        self.assertFalse(social.gate_in_range(0.5, 0.5, 0.80, 0.5, 0.05))


if __name__ == "__main__":
    unittest.main()


class UiStringTests(Round4Base):
    """ข้อความ UI ต้องมาจากไฟล์จริงใน Google Drive (ThaiPack game*_th.txt) ไม่ใช่ข้อความที่แต่งเอง"""

    def test_ui_strings_come_from_drive_thaipack(self):
        token = self.register_login()
        s, b = self.call("GET", "/api/ui/strings", token=token)
        self.assertEqual(s, 200, b)
        self.assertGreater(b["count"], 600, "ต้องมีข้อความไทยจากไฟล์จริงหลายร้อยรายการ")
        self.assertIn("ThaiPack", b["source"])
        strings = b["strings"]
        # ค่าที่ตรวจสอบแล้วตรงกับไฟล์ data/gui/gameintext_th.txt จริง
        self.assertEqual(strings["SENTENSE,BUYITEM_NOMONEY"],
                         "เงินไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้")
        self.assertEqual(strings["SENTENSE,BUYITEM_NOSPACE"],
                         "ช่องเก็บของไม่เพียงพอ! ไม่สามารถซื้อไอเทมได้")
        self.assertEqual(strings["SENTENSE,SELL_ITEM_CONFIRM"],
                         "คุณต้องการขาย [%s] หรือไม่?")
        self.assertEqual(strings["WORD,QUIT_PARTY"], "ยุบ,ออกจากปาร์ตี้")
        self.assertTrue(strings["SENTENSE,LEVELUP"].startswith("ยินดีด้วย!"))
        # ต้องมีอักษรไทยจริงในทุกค่าร้านค้า/ปาร์ตี้ ไม่ใช่ placeholder
        for key in ("SENTENSE,BUY_ITEM_CONFIRM", "SENTENSE,PARTY_QUESTION",
                    "WORD,PARTY_MODAL_TITLE", "SENTENSE,CHAT_PARTY_MODE"):
            self.assertTrue(any("\u0e00" <= c <= "\u0e7f" for c in strings[key]), key)

    def test_ui_strings_rows_persist_in_db_with_source(self):
        token = self.register_login()
        self.call("GET", "/api/ui/strings", token=token)
        row = self.conn.execute(
            "SELECT text, source FROM ui_strings WHERE key = ?",
            ("SENTENSE,MAP_REQUIRE_FAIL",)).fetchone()
        self.assertIsNotNone(row)
        self.assertIn("Drive", row["source"])
        self.assertIn("เงื่อนไขไม่ตรง", row["text"])

    def test_ui_strings_requires_auth(self):
        s, b = self.call("GET", "/api/ui/strings")
        self.assertEqual(s, 401, b)
