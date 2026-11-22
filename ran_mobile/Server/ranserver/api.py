# ran_mobile/Server/ranserver/api.py
# API ของเซิร์ฟเวอร์ RAN MOBILE
# - ทุกการตัดสิน (ดาเมจ ไอเทม เงิน EXP เลเวล) ทำในเซิร์ฟเวอร์เท่านั้น Client ส่งแค่ "คำขอ"
# - ทุกคำสั่งที่เปลี่ยนข้อมูลทำในธุรกรรม (transaction) เดียว ถ้าผิดพลาดจะ rollback ทั้งหมด
# - รองรับ Idempotency-Key เพื่อกันคำขอซ้ำ (เช่นกดซื้อสองครั้งเพราะเน็ตหลุด)
import json
import math
import random
import re
import sqlite3
import threading
import time
from datetime import datetime, timezone

from . import game
from . import monster_ai
from . import world
from .log import get_logger, mask_name
from .db import _ConnProxy
from .security import (
    CHARACTER_NAME_RE,
    DUMMY_HASH,
    EMAIL_RE,
    PASSWORD_MAX,
    PASSWORD_MIN,
    USERNAME_RE,
    hash_password,
    hash_token,
    new_token,
    verify_password,
)

SESSION_TTL_SECONDS = 24 * 3600
IDEM_KEY_RE = re.compile(r"^[A-Za-z0-9_-]{8,64}$")
IDEM_TTL_SECONDS = 24 * 3600
LOGIN_MAX_FAILURES = 5          # ผิดติดกันเกินนี้ใน window จะถูกล็อก
LOGIN_WINDOW_S = 15 * 60
LOGIN_LOCK_S = 15 * 60
LOGIN_USERNAME_MAX = 64
# session โลกเกมที่ไม่มีการติดต่อเกินเวลานี้จะหมดอายุ (ปรับได้จาก --world-idle-s)
WORLD_IDLE_MS_DEFAULT = 5 * 60 * 1000

# ระยะสายตาของผู้เล่น (สัดส่วนแผนที่): ผู้เล่นคนอื่นที่ไกลกว่านี้ไม่ถูกส่งให้ Client
# (มอนสเตอร์ถูกส่งทั้งแผนที่เพราะมีไม่กี่ตัว และ Client ต้องเห็นตอนมันเดินเข้ามา)
VIEW_RANGE_DEFAULT = 0.40
# สถานะ AI ที่ไม่ใช่ "ยังมีชีวิตและว่างอยู่"
MONSTER_MAX_STEP_MS = 250          # ก้าวของ AI ต่อครั้งยาวสุด (กันกระโดดหลังเซิร์ฟเวอร์หยุดนาน)

log_auth = get_logger("auth")
log_world = get_logger("world")
log_server = get_logger("server")
EQUIP_SLOTS = ("weapon", "armor")


class ApiError(Exception):
    def __init__(self, status, code, message):
        super().__init__(message)
        self.status = status
        self.code = code
        self.message = message


def _bad(msg, code="invalid_field"):
    return ApiError(400, code, msg)


def _need_int(body, key, lo, hi):
    v = body.get(key)
    if isinstance(v, bool) or not isinstance(v, int):
        raise _bad(f"'{key}' ต้องเป็นจำนวนเต็ม")
    if v < lo or v > hi:
        raise _bad(f"'{key}' ต้องอยู่ระหว่าง {lo} ถึง {hi}")
    return v


def _need_str(body, key, pattern, what):
    v = body.get(key)
    if not isinstance(v, str) or not pattern.match(v):
        raise _bad(f"'{key}' ไม่ถูกต้อง ({what})")
    return v


def _now_iso():
    return datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")


class Api:
    """ตัวจัดการคำขอ เรียก handle() ด้วย method, path, headers, body(bytes)"""

    def __init__(self, conn, rng=None, clock_ms=None, clock_s=None, world_idle_ms=WORLD_IDLE_MS_DEFAULT,
                 content=None):
        self.world_idle_ms = int(world_idle_ms)
        # connection ตัวเดียวถูกใช้ร่วมหลายเธรด (check_same_thread=False) การเข้าถึงจึงต้องไม่ทับกัน
        # _ConnProxy ถือ lock ให้ทุก execute: ตัวล็อกแคบลงเหลือเฉพาะจังหวะที่แตะฐานข้อมูลจริง
        self.lock = threading.RLock()
        self.conn = _ConnProxy(conn, self.lock)
        # ชุดข้อมูลเกม (content.json): ค่ากติกาของทั้งเซิร์ฟเวอร์มาจากที่นี่
        if content is None:
            from .content import load
            content = load()
        self.content = content
        cb = content.combat
        self.attack_range = cb["attack_range"]
        self.attack_cooldown = cb["attack_cooldown_ms"]
        self.view_range = VIEW_RANGE_DEFAULT
        self.ai = {
            "aggro_range": cb["monster_aggro_range"],
            "attack_range": cb["monster_attack_range"],
            "chase_speed": cb["monster_chase_speed"],
            "patrol_radius": cb["monster_patrol_radius"],
        }
        self.monster_attack_cooldown = cb["monster_attack_cooldown_ms"]
        self.monster_respawn_ms = cb["monster_respawn_ms"]
        conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA foreign_keys = ON")
        self.rng = rng or random.Random()
        self.clock_ms = clock_ms or (lambda: int(time.time() * 1000))
        self.clock_s = clock_s or (lambda: int(time.time()))

    # ------------------------------------------------------------ infrastructure
    def handle(self, method, path, headers, body_bytes):
        """ประมวลผลหนึ่งคำขอ

        ตัวล็อก (self.lock) ครอบเฉพาะช่วงที่ใช้ sqlite3 connection เท่านั้น เพราะ connection
        ตัวเดียวถูกใช้ร่วมกันหลายเธรด (check_same_thread=False) การอ่าน/เขียนต้องไม่ทับกัน
        งานที่ไม่แตะฐานข้อมูล (แปลง header, parse JSON, ตรวจรูปแบบ, เขียน log) ทำนอกล็อก
        จึงไม่ถูกจัดคิวตามกัน และคำขออื่นรอได้ระหว่างการตรวจรหัสผ่าน (scrypt) ของ login
        """
        try:
            return self._dispatch(method, path, headers, body_bytes)
        except ApiError as e:
            return e.status, {"error": {"code": e.code, "message": e.message}}
        except sqlite3.Error:
            # ไม่เปิดเผยรายละเอียดของฐานข้อมูลให้ Client
            return 500, {"error": {"code": "database_error", "message": "เกิดข้อผิดพลาดของฐานข้อมูล"}}
        except Exception:
            return 500, {"error": {"code": "internal", "message": "เกิดข้อผิดพลาดภายในเซิร์ฟเวอร์"}}

    def _tx(self):
        return _Tx(self.conn)

    def _parse_body(self, body_bytes):
        if not body_bytes:
            return {}
        try:
            data = json.loads(body_bytes.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            raise _bad("JSON ไม่ถูกต้อง", "invalid_json")
        if not isinstance(data, dict):
            raise _bad("ต้องส่ง JSON เป็นอ็อบเจกต์", "invalid_json")
        return data

    def _dispatch(self, method, path, headers, body_bytes):
        # งานนอกฐานข้อมูล: ทำก่อนขอตัวล็อก
        headers = {k.lower(): v for k, v in (headers or {}).items()}
        path_matched = False
        for route_method, pattern, handler, needs_auth, mutating in ROUTES:
            m = pattern.fullmatch(path)
            if not m:
                continue
            path_matched = True
            if route_method != method:
                continue  # path เดียวกันอาจมีหลาย method (เช่น GET และ DELETE)
            body = self._parse_body(body_bytes)
            idem_key = headers.get("idempotency-key") if mutating else None
            if idem_key is not None and not IDEM_KEY_RE.match(idem_key):
                raise _bad("Idempotency-Key ไม่ถูกต้อง", "invalid_idempotency_key")
            args = m.groups()
            account_id = None
            token = None
            if needs_auth:
                account_id, token = self._authenticate(headers)
            if idem_key is not None:
                cached = self._idem_lookup(account_id, idem_key)
                if cached is not None:
                    return cached
            # ธุรกรรมถือตัวล็อกไว้ตลอด (BEGIN ... COMMIT ของ connection เดียวกันต้องไม่ถูกแทรก)
            with self._tx():
                self._sweep_world()
                status, payload = handler(self, account_id, token, body, *args)
                if idem_key is not None and 200 <= status < 300:
                    self._idem_store(account_id, idem_key, status, payload)
            return status, payload
        if path_matched:
            raise ApiError(405, "method_not_allowed", "method ไม่ได้รับอนุญาต")
        raise ApiError(404, "not_found", "ไม่พบเส้นทาง")

    # การเข้ารหัสรหัสผ่านช้าโดยตั้งใจ (scrypt) จึงทำนอกล็อกฐานข้อมูล: hash_password/verify_password
    # ไม่แตะ connection เลย และการทำนอกล็อกทำให้คำขอของบัญชีอื่นไม่ต้องรอ
    def _prepare_new_password(self, password):
        """คำนวณ hash ล่วงหน้า (นอกตัวล็อก) คืนค่าที่บันทึกได้ หรือ None ถ้ารหัสไม่ผ่านกติกา"""
        if not isinstance(password, str) or not (PASSWORD_MIN <= len(password) <= PASSWORD_MAX):
            return None
        return hash_password(password)

    def _account_for_login(self, username):
        """อ่านแถวบัญชีที่ต้องใช้ตรวจรหัสผ่าน (ในล็อก) คืน None เมื่อไม่พบหรือชื่อยาวผิดปกติ"""
        if not isinstance(username, str) or len(username) > LOGIN_USERNAME_MAX:
            return None
        return self.conn.execute(
            "SELECT id, password_hash FROM accounts WHERE username = ?", (username,)
        ).fetchone()

    def _verify_password(self, password, stored):
        """ตรวจรหัสผ่านนอกตัวล็อก คืน True/False

        รับเฉพาะสตริงที่สั้นพอ (กันการป้อนข้อมูลยาวผิดปกติ) และใช้ DUMMY_HASH เมื่อไม่พบบัญชี
        เพื่อให้เวลาตอบใกล้เคียงกันทั้งสองกรณี
        """
        if not isinstance(password, str) or len(password) > PASSWORD_MAX:
            return False
        return verify_password(password, stored)


    def _sweep_world(self):
        """ปิด session โลกเกมที่หมดอายุ: ไม่มีการติดต่อเกินกำหนด หรือโทเคนของบัญชีหมดอายุแล้ว

        ตำแหน่งล่าสุดถูกบันทึกในตาราง characters ตั้งแต่การเดินที่ได้รับอนุมัติแต่ละครั้งแล้ว
        จึงไม่ต้องบันทึกเพิ่มตอนหมดอายุ
        """
        now = self.clock_ms()
        idle = self.conn.execute(
            "DELETE FROM world_sessions WHERE last_seen_ms < ?", (now - self.world_idle_ms,)).rowcount
        tokens = self.conn.execute(
            "DELETE FROM world_sessions WHERE character_id IN ("
            " SELECT c.id FROM characters c WHERE c.account_id NOT IN"
            " (SELECT account_id FROM sessions WHERE expires_at > ?))", (self.clock_s(),)).rowcount
        if idle:
            log_world.info("world session expired (idle): %d", idle)
        if tokens:
            log_world.info("world session ended (login expired): %d", tokens)

    def _authenticate(self, headers):
        auth = headers.get("authorization", "")
        if not auth.startswith("Bearer ") or len(auth) < 16:
            raise ApiError(401, "unauthorized", "ต้องเข้าสู่ระบบก่อน")
        token = auth[7:].strip()
        row = self.conn.execute(
            "SELECT account_id, expires_at FROM sessions WHERE token_hash = ?",
            (hash_token(token),),
        ).fetchone()
        if row is None or row["expires_at"] <= self.clock_s():
            raise ApiError(401, "unauthorized", "เซสชันหมดอายุหรือไม่ถูกต้อง")
        return row["account_id"], token

    def _idem_lookup(self, account_id, key):
        row = self.conn.execute(
            "SELECT status_code, response, created_at FROM request_log WHERE account_id = ? AND idem_key = ?",
            (account_id, key),
        ).fetchone()
        if row is None:
            return None
        if row["created_at"] + IDEM_TTL_SECONDS < self.clock_s():
            return None
        return row["status_code"], json.loads(row["response"])

    def _idem_store(self, account_id, key, status, payload):
        self.conn.execute(
            "INSERT OR REPLACE INTO request_log (idem_key, account_id, status_code, response, created_at) "
            "VALUES (?, ?, ?, ?, ?)",
            (key, account_id, status, json.dumps(payload, ensure_ascii=False), self.clock_s()),
        )

    # ------------------------------------------------------------ helpers
    def _own_character(self, account_id, cid):
        row = self.conn.execute(
            "SELECT * FROM characters WHERE id = ? AND account_id = ?", (cid, account_id)
        ).fetchone()
        if row is None:
            # ตอบเหมือนกันทั้งกรณีไม่มีตัวละครและไม่ใช่ของบัญชีนี้ ไม่เปิดเผยการมีอยู่
            raise ApiError(404, "character_not_found", "ไม่พบตัวละคร")
        return row

    def _character_row(self, cid):
        return self.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()

    def _character_position(self, cid):
        """ตำแหน่งของตัวละครถ้าอยู่ในโลก (มี world_session) ไม่งั้นคืน None"""
        row = self.conn.execute(
            "SELECT c.pos_x, c.pos_y FROM characters c"
            " JOIN world_sessions w ON w.character_id = c.id WHERE c.id = ?", (cid,)).fetchone()
        return (row["pos_x"], row["pos_y"]) if row else None

    def _monster_view(self, mon, now):
        dead = mon["respawn_at_ms"] > 0 or mon["hp"] <= 0
        alive = not dead
        return {
            "monster_id": mon["monster_id"], "name": mon["name"], "level": mon["level"],
            "hp": mon["hp"], "max_hp": mon["max_hp"], "dead": dead,
            "state": "dead" if dead else (mon["state"] or "idle"),
            "target_id": (mon["target_id"] or 0) if alive else 0,
            "respawn_in_ms": max(0, mon["respawn_at_ms"] - now) if mon["respawn_at_ms"] > 0 else 0,
            "position": {"x": mon["pos_x"], "y": mon["pos_y"]},
            "home": {"x": mon["home_x"], "y": mon["home_y"]},
            "distance": None,  # filled by callers that know the player position
        }

    def _npc_view(self, npc):
        return {"npc_id": npc["npc_id"], "name": npc["name"], "role": npc["role"],
                "position": {"x": npc["pos_x"], "y": npc["pos_y"]},
                "dialog": json.loads(npc["dialog_json"]) if npc["dialog_json"] else []}

    def _refresh_monsters(self):
        now = self.clock_ms()
        self.conn.execute(
            "UPDATE monsters SET hp = max_hp, respawn_at_ms = 0 WHERE respawn_at_ms > 0 AND respawn_at_ms <= ?",
            (now,),
        )

    def _inventory_rows(self, cid):
        return self.conn.execute(
            "SELECT inv.id, inv.slot, inv.quantity, inv.item_id, it.name, it.type, it.price, it.stackable, "
            "it.max_stack, it.hp_recovery, it.mp_recovery, it.sp_recovery "
            "FROM inventory inv JOIN items it ON it.item_id = inv.item_id "
            "WHERE inv.character_id = ? ORDER BY inv.slot",
            (cid,),
        ).fetchall()

    def _character_view(self, cid):
        c = self.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        mhp = game.max_hp(c["level"], c["stat_con"])
        mmp = game.max_mp(c["level"], c["stat_int"])
        msp = game.max_sp(c["level"])
        now = self.clock_ms()
        skills = []
        for s in self.conn.execute(
            "SELECT sk.skill_id, sk.name, sk.damage, sk.mana_cost, sk.cooldown_ms, sk.range, cs.level, cs.last_used_ms "
            "FROM character_skills cs JOIN skills sk ON sk.skill_id = cs.skill_id "
            "WHERE cs.character_id = ? ORDER BY sk.skill_id",
            (cid,),
        ).fetchall():
            remaining = max(0, s["last_used_ms"] + s["cooldown_ms"] - now) if s["last_used_ms"] else 0
            skills.append({
                "skill_id": s["skill_id"], "name": s["name"], "damage": s["damage"],
                "mana_cost": s["mana_cost"], "cooldown_ms": s["cooldown_ms"], "range": s["range"],
                "level": s["level"], "cooldown_remaining_ms": remaining,
            })
        quests = [
            {"quest_id": q["quest_id"], "name": q["name"], "progress": q["progress"],
             "required_count": q["required_count"], "status": q["status"]}
            for q in self.conn.execute(
                "SELECT cq.quest_id, cq.progress, cq.status, q.name, q.required_count "
                "FROM character_quests cq JOIN quests q ON q.quest_id = cq.quest_id "
                "WHERE cq.character_id = ? ORDER BY cq.quest_id",
                (cid,),
            ).fetchall()
        ]
        return {
            "id": c["id"],
            "name": c["name"],
            "gender": c["gender"],
            "class": c["class"],
            "level": c["level"],
            "exp": c["exp"],
            "exp_required": game.exp_required(c["level"]),
            "money": c["money"],
            "hp": c["hp"], "max_hp": mhp,
            "mp": c["mp"], "max_mp": mmp,
            "sp": c["sp"], "max_sp": msp,
            "stats": {"str": c["stat_str"], "dex": c["stat_dex"], "int": c["stat_int"], "con": c["stat_con"]},
            "dead": c["hp"] <= 0,
            "inventory": [
                {"slot": r["slot"], "item_id": r["item_id"], "name": r["name"], "type": r["type"],
                 "quantity": r["quantity"], "stackable": bool(r["stackable"]), "max_stack": r["max_stack"],
                 "price": r["price"]}
                for r in self._inventory_rows(cid)
            ],
            "skills": skills,
            "quests": quests,
            "equipment": self._equipment_view(cid),
            "map_id": c["map_id"],
            "position": {"x": c["pos_x"], "y": c["pos_y"]},
            "in_world": self.conn.execute(
                "SELECT 1 FROM world_sessions WHERE character_id = ?", (cid,)).fetchone() is not None,
        }

    def _plan_add(self, cid, item, qty):
        """วางแผนการเพิ่มไอเทมโดยยังไม่เขียนลงฐานข้อมูล คืน (updates, inserts) หรือ error ถ้าที่ไม่พอ"""
        rows = self._inventory_rows(cid)
        used = {r["slot"] for r in rows}
        updates, inserts = [], []
        remaining = qty
        if item["stackable"]:
            for r in rows:
                if remaining <= 0:
                    break
                if r["item_id"] == item["item_id"] and r["quantity"] < item["max_stack"]:
                    add = min(item["max_stack"] - r["quantity"], remaining)
                    updates.append((r["id"], r["quantity"] + add))
                    remaining -= add
        while remaining > 0:
            free = next((s for s in range(game.INVENTORY_SLOTS) if s not in used), None)
            if free is None:
                raise ApiError(409, "inventory_full", "กระเป๋าเต็ม")
            add = min(remaining, item["max_stack"]) if item["stackable"] else 1
            inserts.append((free, add))
            used.add(free)
            remaining -= add
        return updates, inserts

    def _apply_add(self, cid, item_id, updates, inserts):
        for row_id, qty in updates:
            self.conn.execute("UPDATE inventory SET quantity = ? WHERE id = ?", (qty, row_id))
        for slot, qty in inserts:
            self.conn.execute(
                "INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, ?, ?, ?)",
                (cid, item_id, qty, slot),
            )

    def _item(self, item_id):
        row = self.conn.execute("SELECT * FROM items WHERE item_id = ?", (item_id,)).fetchone()
        if row is None:
            raise ApiError(404, "item_not_found", "ไม่พบไอเทม")
        return row

    def _grant_exp(self, char, exp_gain, money_gain):
        """ให้ EXP/เงิน และเลเวลอัป คืน (level, exp, money, gained) — ค่าสเตตัสจริงถูกเพิ่มใน _apply_level_growth"""
        level, exp, gained = game.apply_exp(char["level"], char["exp"], exp_gain)
        return level, exp, char["money"] + money_gain, gained

    def _apply_level_growth(self, char, level, new_level, hp_after, mp_after, sp_after):
        """เลเวลอัป: เพิ่ม HP/MP สูงสุดและค่าถาวรลงฐานข้อมูลจริง (ตาม content.level_up)"""
        if new_level <= level:
            return {"atk_added": 0, "def_added": 0, "hp_added": 0, "mp_added": 0}
        steps = new_level - level
        lu = self.content.level_up
        atk_add = lu["atk_per_level"] * steps
        def_add = lu["def_per_level"] * steps
        hp_add = lu["hp_per_level"] * steps
        mp_add = lu["mp_per_level"] * steps
        self.conn.execute(
            "UPDATE characters SET base_atk = base_atk + ?, base_def = base_def + ? WHERE id = ?",
            (atk_add, def_add, char["id"]),
        )
        return {"atk_added": atk_add, "def_added": def_add, "hp_added": hp_add, "mp_added": mp_add}

    def _resolve_hit(self, char, monster_id, raw_damage, now, skill=None):
        """ใช้ดาเมจกับมอนสเตอร์ คืน dict ผลลัพธ์ และอัปเดตแถวตัวละครใน self.conn (ไม่ commit เอง)

        skill: ถ้าเป็นการใช้สกิล จะส่ง {skill_id, name, ...} มาด้วยเพื่อรายงานในผลลัพธ์
        """
        mon = self.conn.execute("SELECT * FROM monsters WHERE monster_id = ?", (monster_id,)).fetchone()
        if mon is None:
            raise ApiError(404, "monster_not_found", "ไม่พบมอนสเตอร์")
        if mon["respawn_at_ms"] > 0 or mon["hp"] <= 0:
            raise ApiError(409, "monster_dead", "มอนสเตอร์ตายแล้ว รอเกิดใหม่")

        damage, crit, missed = game.roll_hit(raw_damage, mon["defense"], self.rng)
        new_mon_hp = max(0, mon["hp"] - damage)
        killed = new_mon_hp == 0
        if killed:
            respawn = now + self.monster_respawn_ms
            self.conn.execute(
                "UPDATE monsters SET hp = 0, respawn_at_ms = ?, target_id = 0, state = 'dead'"
                " WHERE monster_id = ?", (respawn, monster_id))
        else:
            # ถูกตีแล้วจดจำผู้เล่นที่ตี (AI จะไล่/สวนกลับใน tick ถัดไป)
            self.conn.execute("UPDATE monsters SET hp = ?, target_id = ? WHERE monster_id = ?",
                              (new_mon_hp, char["id"], monster_id))

        result = {
            "hit": not missed, "crit": crit, "damage": damage,
            "monster": {"monster_id": monster_id, "name": mon["name"], "hp": new_mon_hp,
                        "max_hp": mon["max_hp"], "dead": killed,
                        "position": {"x": mon["pos_x"], "y": mon["pos_y"]}},
            "rewards": None, "quest_progress": [], "retaliation_damage": 0, "drops": [],
        }
        if skill is not None:
            result["skill"] = skill

        hp = char["hp"]
        exp_gain = 0
        money_gain = 0
        if killed:
            exp_gain += mon["exp_reward"]
            money_gain += mon["money_reward"]
            for q in self.conn.execute(
                "SELECT cq.id, cq.quest_id, cq.progress, cq.status, q.target_monster, q.required_count, "
                "q.reward_exp, q.reward_money, q.name FROM character_quests cq "
                "JOIN quests q ON q.quest_id = cq.quest_id "
                "WHERE cq.character_id = ? AND cq.status = 'accepted' AND q.target_monster = ?",
                (char["id"], monster_id),
            ).fetchall():
                progress = min(q["required_count"], q["progress"] + 1)
                status = "accepted"
                if progress >= q["required_count"]:
                    status = "rewarded"
                    exp_gain += q["reward_exp"]
                    money_gain += q["reward_money"]
                self.conn.execute(
                    "UPDATE character_quests SET progress = ?, status = ? WHERE id = ?",
                    (progress, status, q["id"]),
                )
                result["quest_progress"].append({
                    "quest_id": q["quest_id"], "name": q["name"], "progress": progress,
                    "required_count": q["required_count"], "status": status,
                })
            result["drops"] = self._roll_drops(mon, char["id"])
        elif not missed:
            # มอนสเตอร์ยังไม่ตายจึงตอบโต้ทันทีด้วยดาเมจของมันเอง
            _, armor_def = self._equipment_bonus(char["id"])
            player_def = game.defense_power(self._col(char, "base_def"), char["stat_con"] // 2, armor_def)
            retaliation, _, _ = monster_ai.hit_player(mon["attack"], player_def, self.rng)
            hp = max(0, hp - retaliation)
            result["retaliation_damage"] = retaliation

        level, exp, money, gained = self._grant_exp(char, exp_gain, money_gain)
        mp = char["mp"]
        sp = char["sp"]
        growth = {"atk_added": 0, "def_added": 0, "hp_added": 0, "mp_added": 0}
        if gained > 0:
            growth = self._apply_level_growth(char, char["level"], level, hp, mp, sp)
            # เลเวลอัปฟื้น HP/MP เต็มหลอด (เช่นเดียวกับเกมแนวเดียวกัน)
            hp = game.max_hp(level, char["stat_con"])
            mp = game.max_mp(level, char["stat_int"])
            sp = game.max_sp(level)
        self.conn.execute(
            "UPDATE characters SET level = ?, exp = ?, money = ?, hp = ?, mp = ?, sp = ? WHERE id = ?",
            (level, exp, money, hp, mp, sp, char["id"]),
        )
        if exp_gain or money_gain:
            result["rewards"] = {"exp": exp_gain, "money": money_gain, "levels_gained": gained,
                                 "growth": growth}
        fresh = self._character_row(char["id"])
        result["player"] = self._player_stats(fresh) if fresh is not None else {
            "hp": hp, "max_hp": game.max_hp(level, char["stat_con"]), "dead": hp <= 0}
        return result

    def _roll_drops(self, mon, cid):
        """สุ่มของดรอปของมอนสเตอร์ที่ตาย แล้วใส่ลงกระเป๋าจริง คืนรายการที่ได้"""
        rows = self.conn.execute(
            "SELECT d.item_id, d.chance, d.min_qty, d.max_qty, it.name FROM monster_drops d"
            " JOIN items it ON it.item_id = d.item_id WHERE d.monster_id = ? ORDER BY d.item_id",
            (mon["monster_id"],),
        ).fetchall()
        if not rows:
            return []
        drops = monster_ai.roll_drops(
            [{"item_id": r["item_id"], "chance": r["chance"], "min": r["min_qty"], "max": r["max_qty"]}
             for r in rows], self.rng)
        out = []
        names = {r["item_id"]: r["name"] for r in rows}
        for item_id, qty in drops:
            try:
                item = self._item(item_id)
                updates, inserts = self._plan_add(cid, item, qty)
                self._apply_add(cid, item_id, updates, inserts)
            except ApiError:
                # กระเป๋าเต็ม: ของยังดรอปแต่เก็บไม่ได้ (Client เห็นในข้อความ) ไม่ทำให้การโจมตีล้มเหลว
                out.append({"item_id": item_id, "name": names.get(item_id, ""), "quantity": qty,
                            "stored": False})
                continue
            out.append({"item_id": item_id, "name": names.get(item_id, ""), "quantity": qty,
                        "stored": True})
        if out:
            self._refresh_quests_for_drops(cid, out)
        return out

    def _refresh_quests_for_drops(self, cid, drops):
        """เควสต์แบบเก็บของ (ถ้ามีในอนาคต) — ตอนนี้เป็นที่วางไว้ให้ส่วนขยาย ไม่ทำอะไร"""
        return None

    # ------------------------------------------------------------ มอนสเตอร์เดิน/โจมตี (AI)
    def tick_monsters(self, now=None):
        """ให้ AI ของมอนสเตอร์ทุกตัวก้าวหนึ่งครั้ง และให้มอนสเตอร์ที่ถึงจังหวะโจมตีผู้เล่น

        เรียกจากผู้เล่นทุกคนที่อยู่ในโลก (ทุกคำขอ world) และจาก health check: ไม่มีเธรดพื้นหลัง
        จึงไม่มีปัญหาเรื่อง transaction ซ้อนกัน และตำแหน่งมอนสเตอร์เดินหน้าต่อแม้ไม่มีใครขยับ
        """
        now = self.clock_ms() if now is None else now
        self._refresh_monsters()
        players_by_map = {}
        for r in self.conn.execute(
                "SELECT c.id, c.map_id, c.pos_x, c.pos_y, c.hp, c.level, c.stat_con, c.base_def"
                " FROM characters c JOIN world_sessions w ON w.character_id = c.id").fetchall():
            players_by_map.setdefault(r["map_id"], []).append(
                (r["id"], r["pos_x"], r["pos_y"], r["hp"]))
        events = {"moved": 0, "attacked": 0, "killed_players": 0, "monsters": []}
        for mon in self.conn.execute(
                "SELECT * FROM monsters WHERE respawn_at_ms = 0 AND hp > 0 ORDER BY monster_id").fetchall():
            players = players_by_map.get(mon["map_id"], [])
            blocks = json.loads(self._map_row(mon["map_id"])["blocks_json"])
            last = self._col(mon, "last_tick_ms")
            dt = MONSTER_MAX_STEP_MS if not last else max(0, min(MONSTER_MAX_STEP_MS, now - last))
            m = {"pos_x": mon["pos_x"], "pos_y": mon["pos_y"], "home_x": mon["home_x"],
                 "home_y": mon["home_y"], "patrol": mon["patrol"]}
            # ผู้เล่นที่เพิ่งตีมอนสเตอร์ตัวนี้ถือเป็นเป้าหมายก่อน (ถ้ายังอยู่ในระยะตรวจจับ)
            ordered = [(p[0], p[1], p[2]) for p in players if p[0] == mon["target_id"]]
            ordered += [(p[0], p[1], p[2]) for p in players if p[0] != mon["target_id"]]
            step = monster_ai.step(m, ordered, dt, blocks, self.rng, ai=self.ai)
            moved = (step["x"], step["y"]) != (mon["pos_x"], mon["pos_y"])
            if moved:
                events["moved"] += 1
            attack_damage = 0
            if step["attack"] and step["target_id"]:
                wait_ok = now - self._col(mon, "last_attack_ms") >= self.monster_attack_cooldown
                if wait_ok:
                    attack_damage = self._monster_attack(mon, step["target_id"], players, now, blocks)
                    if attack_damage > 0:
                        events["attacked"] += 1
            self.conn.execute(
                "UPDATE monsters SET pos_x = ?, pos_y = ?, state = ?, target_id = ?, last_tick_ms = ?"
                " WHERE monster_id = ?",
                (step["x"], step["y"], step["state"], step["target_id"] or 0, now, mon["monster_id"]))
            if moved or step["state"] in ("chase", "attack"):
                events["monsters"].append({
                    "monster_id": mon["monster_id"], "state": step["state"],
                    "position": {"x": round(step["x"], 4), "y": round(step["y"], 4)},
                    "target_id": step["target_id"] or 0, "attack_damage": attack_damage,
                })
        return events

    def _monster_attack(self, mon, player_id, players, now, blocks):
        """มอนสเตอร์โจมตีผู้เล่นหนึ่งคน คืนดาเมจ (0 ถ้าตีไม่โดน/เป้าหมายไม่อยู่แล้ว)"""
        target = next((p for p in players if p[0] == player_id), None)
        if target is None:
            return 0
        row = self._character_row(player_id)
        if row is None or row["hp"] <= 0:
            return 0
        _, armor_def = self._equipment_bonus(player_id)
        player_def = game.defense_power(self._col(row, "base_def"), row["stat_con"] // 2, armor_def)
        damage, _crit, missed = monster_ai.hit_player(mon["attack"], player_def, self.rng)
        if missed:
            damage = 0
        hp = max(0, row["hp"] - damage)
        self.conn.execute("UPDATE characters SET hp = ? WHERE id = ?", (hp, player_id))
        self.conn.execute("UPDATE monsters SET last_attack_ms = ? WHERE monster_id = ?",
                          (now, mon["monster_id"]))
        if hp <= 0:
            self.conn.execute("DELETE FROM world_sessions WHERE character_id = ?", (player_id,))
            log_world.info("character %s died to monster %s", player_id, mon["monster_id"])
        return damage

    # ------------------------------------------------------------ handlers (กลุ่มบัญชี)
    @staticmethod
    def h_health(api, account_id, token, body):
        version = api.conn.execute("PRAGMA user_version").fetchone()[0]
        return 200, {"ok": True, "service": "ran_mobile", "schema_version": version}

    @staticmethod
    def h_register(api, account_id, token, body):
        username = _need_str(body, "username", USERNAME_RE, "3-20 ตัว: A-Z a-z 0-9 _")
        password = body.get("password")
        if not isinstance(password, str) or not (PASSWORD_MIN <= len(password) <= PASSWORD_MAX):
            raise _bad(f"รหัสผ่านต้องยาว {PASSWORD_MIN}-{PASSWORD_MAX} ตัวอักษร")
        email = body.get("email")
        if email is not None:
            if not isinstance(email, str) or not EMAIL_RE.match(email):
                raise _bad("อีเมลไม่ถูกต้อง")
        # hash นอกตัวล็อก (scrypt ช้า) แล้วค่อยตรวจชื่อซ้ำ+บันทึกในล็อกเดียวกัน
        password_hash = api._prepare_new_password(password)
        if api.conn.execute("SELECT 1 FROM accounts WHERE username = ?", (username,)).fetchone():
            raise ApiError(409, "username_taken", "ชื่อผู้ใช้นี้ถูกใช้แล้ว")
        cur = api.conn.execute(
            "INSERT INTO accounts (username, password_hash, email, created_at) VALUES (?, ?, ?, ?)",
            (username, password_hash, email, _now_iso()),
        )
        log_auth.info("account registered id=%s user=%s", cur.lastrowid, mask_name(username))
        return 201, {"account_id": cur.lastrowid, "username": username}

    @staticmethod
    def h_login(api, account_id, token, body):
        username = body.get("username")
        password = body.get("password")
        if (not isinstance(username, str) or not isinstance(password, str)
                or len(password) > PASSWORD_MAX or len(username) > LOGIN_USERNAME_MAX):
            raise ApiError(401, "invalid_credentials", "ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง")
        now_s = api.clock_s()
        rec = api.conn.execute(
            "SELECT failures, window_start_s, locked_until_s FROM login_failures WHERE username = ?",
            (username,),
        ).fetchone()
        locked = rec is not None and rec["locked_until_s"] > now_s
        if locked:
            raise ApiError(429, "too_many_attempts",
                           f"เข้าสู่ระบบผิดหลายครั้ง กรุณารอ {rec['locked_until_s'] - now_s} วินาที")
        # ตรวจรหัสผ่าน (scrypt) นอกตัวล็อก และใช้ DUMMY_HASH เมื่อไม่พบบัญชี
        row = api._account_for_login(username)
        ok = api._verify_password(password, row["password_hash"] if row else DUMMY_HASH)
        if row is None or not ok:
            failures, window_start = 1, now_s
            if rec is not None and rec["window_start_s"] + LOGIN_WINDOW_S > now_s:
                failures, window_start = rec["failures"] + 1, rec["window_start_s"]
            locked_until = now_s + LOGIN_LOCK_S if failures >= LOGIN_MAX_FAILURES else 0
            log_auth.warning("login failed user=%s failures=%d%s", mask_name(username), failures,
                             " (locked)" if locked_until else "")
            # คืน 401 (ไม่ raise) เพื่อให้การบันทึกตัวนับถูก commit
            api.conn.execute(
                "INSERT OR REPLACE INTO login_failures (username, failures, window_start_s, locked_until_s) "
                "VALUES (?, ?, ?, ?)",
                (username, failures, window_start, locked_until),
            )
            return 401, {"error": {"code": "invalid_credentials", "message": "ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง"}}
        api.conn.execute("DELETE FROM login_failures WHERE username = ?", (username,))
        api.conn.execute("DELETE FROM sessions WHERE account_id = ? AND expires_at <= ?", (row["id"], now_s))
        new = new_token()
        expires = now_s + SESSION_TTL_SECONDS
        api.conn.execute(
            "INSERT INTO sessions (token_hash, account_id, expires_at) VALUES (?, ?, ?)",
            (hash_token(new), row["id"], expires),
        )
        api.conn.execute("UPDATE accounts SET last_login = ? WHERE id = ?", (_now_iso(), row["id"]))
        log_auth.info("login ok account_id=%s", row["id"])
        return 200, {"token": new, "expires_at": expires, "account_id": row["id"]}

    @staticmethod
    def h_logout(api, account_id, token, body):
        api.conn.execute("DELETE FROM sessions WHERE token_hash = ?", (hash_token(token),))
        api.conn.execute(
            "DELETE FROM world_sessions WHERE character_id IN (SELECT id FROM characters WHERE account_id = ?)",
            (account_id,),
        )
        log_auth.info("logout account_id=%s", account_id)
        return 200, {"ok": True}

    # ------------------------------------------------------------ handlers (ตัวละคร)
    @staticmethod
    def h_list_characters(api, account_id, token, body):
        rows = api.conn.execute(
            "SELECT id, name, gender, class, level FROM characters WHERE account_id = ? ORDER BY id",
            (account_id,),
        ).fetchall()
        return 200, {"characters": [dict(r) for r in rows], "limit": game.MAX_CHARACTERS_PER_ACCOUNT}

    @staticmethod
    def h_create_character(api, account_id, token, body):
        name = _need_str(body, "name", CHARACTER_NAME_RE, "2-16 ตัวอักษร")
        gender = body.get("gender")
        if gender not in ("male", "female"):
            raise _bad("gender ต้องเป็น male หรือ female")
        cls = body.get("class")
        if cls not in game.CLASS_START_STATS:
            raise _bad("class ไม่ถูกต้อง")
        count = api.conn.execute(
            "SELECT COUNT(*) AS n FROM characters WHERE account_id = ?", (account_id,)
        ).fetchone()["n"]
        if count >= game.MAX_CHARACTERS_PER_ACCOUNT:
            raise ApiError(409, "character_limit", "ครบจำนวนตัวละครสูงสุดแล้ว")
        if api.conn.execute("SELECT 1 FROM characters WHERE name = ?", (name,)).fetchone():
            raise ApiError(409, "name_taken", "ชื่อตัวละครนี้ถูกใช้แล้ว")
        st, dx, it, cn = game.CLASS_START_STATS[cls]
        lvl = 1
        cur = api.conn.execute(
            "INSERT INTO characters (account_id, name, gender, class, level, exp, money, hp, mp, sp, "
            "stat_str, stat_dex, stat_int, stat_con, created_at) VALUES (?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (account_id, name, gender, cls, lvl, game.STARTING_MONEY,
             game.max_hp(lvl, cn), game.max_mp(lvl, it), game.max_sp(lvl),
             st, dx, it, cn, _now_iso()),
        )
        cid = cur.lastrowid
        for item_id, qty, slot in game.STARTER_ITEMS:
            api.conn.execute(
                "INSERT INTO inventory (character_id, item_id, quantity, slot) VALUES (?, ?, ?, ?)",
                (cid, item_id, qty, slot),
            )
        for skill_id in game.STARTER_SKILL_IDS:
            api.conn.execute(
                "INSERT INTO character_skills (character_id, skill_id, level, last_used_ms) VALUES (?, ?, 1, 0)",
                (cid, skill_id),
            )
        return 201, api._character_view(cid)

    @staticmethod
    def h_delete_character(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        api.conn.execute("DELETE FROM characters WHERE id = ?", (cid,))
        return 200, {"ok": True}

    @staticmethod
    def h_get_character(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        return 200, api._character_view(cid)

    @staticmethod
    def h_monsters(api, account_id, token, body):
        """มอนสเตอร์ในแผนที่ของผู้เล่น พร้อมระยะจากตัวละครและธง in_range"""
        row = api.conn.execute(
            "SELECT c.id, c.map_id, c.pos_x, c.pos_y FROM characters c WHERE c.account_id = ?"
            " ORDER BY c.id LIMIT 1", (account_id,)).fetchone()
        api._refresh_monsters()
        now = api.clock_ms()
        if row is None:
            return 200, {"monsters": [], "map_id": None,
                         "attack": {"range": api.attack_range, "cooldown_ms": api.attack_cooldown}}
        out = []
        for r in api.conn.execute("SELECT * FROM monsters WHERE map_id = ? ORDER BY monster_id",
                                  (row["map_id"],)).fetchall():
            v = api._monster_view(r, now)
            v["distance"] = round(math.hypot(r["pos_x"] - row["pos_x"], r["pos_y"] - row["pos_y"]), 4)
            v["in_range"] = v["distance"] <= api.attack_range
            out.append(v)
        return 200, {"monsters": out, "map_id": row["map_id"], "position": {"x": row["pos_x"], "y": row["pos_y"]},
                     "attack": {"range": api.attack_range, "cooldown_ms": api.attack_cooldown}}

    @staticmethod
    def h_npcs(api, account_id, token, body):
        """NPC ของแผนที่ตัวละครแรกของบัญชี (คำพูดเป็นข้อความที่โปรเจกต์เขียนเอง)"""
        row = api.conn.execute(
            "SELECT map_id FROM characters WHERE account_id = ? ORDER BY id LIMIT 1", (account_id,)).fetchone()
        if row is None:
            return 200, {"npcs": []}
        return 200, {"npcs": [api._npc_view(r) for r in api.conn.execute(
            "SELECT * FROM npcs WHERE map_id = ? ORDER BY npc_id", (row["map_id"],)).fetchall()]}

    @staticmethod
    def h_attack(api, account_id, token, body, cid):
        """โจมตีพื้นฐาน: ตรวจว่าอยู่ในโลก ในระยะ และพ้นคูลดาวน์ แล้วให้ Server คิดดาเมจเอง"""
        # ตรวจตัวละคร/มอนสเตอร์ก่อน แล้วจึงตรวจว่าอยู่ในโลก (Client ที่ไม่ได้เข้าโลกยังได้ 404/400 ที่ถูกต้อง)
        ch = api._own_character(account_id, cid)
        if ch["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว ต้องฟื้นคืนชีพก่อน")
        monster_id = _need_int(body, "monster_id", 1, 2 ** 31 - 1)
        now = api.clock_ms()
        cooldown = api.attack_cooldown
        # ต้องอยู่ในโลกจึงจะตีได้ (ตำแหน่งที่ใช้ตรวจระยะคือตำแหน่งที่ Server ยอมรับล่าสุด)
        pos = api._character_position(cid)
        if pos is None:
            return 409, {"error": {"code": "not_in_world", "message": "ตัวละครยังไม่ได้เข้าโลก"}}
        ok, wait_ms = game.attack_cooldown_ok(ch["last_attack_ms"], now, cooldown)
        if not ok:
            return 429, {"error": {"code": "attack_too_fast",
                                   "message": f"รอคูลดาวน์อีก {wait_ms} ms"},
                         "retry_after_ms": wait_ms, "cooldown_ms": cooldown}
        api._refresh_monsters()
        mon = api.conn.execute("SELECT * FROM monsters WHERE monster_id = ?", (monster_id,)).fetchone()
        if mon is None:
            raise ApiError(404, "monster_not_found", "ไม่พบมอนสเตอร์")
        if mon["map_id"] != ch["map_id"]:
            raise ApiError(409, "monster_not_here", "มอนสเตอร์ไม่ได้อยู่ในแผนที่นี้")
        if not game.monster_in_range(pos[0], pos[1], mon["pos_x"], mon["pos_y"], api.attack_range):
            return 409, {"error": {"code": "out_of_range",
                                   "message": "เป้าหมายอยู่นอกระยะโจมตี"},
                         "position": {"x": pos[0], "y": pos[1]},
                         "monster": api._monster_view(mon, now),
                         "range": api.attack_range}
        # นับเวลาการโจมตีนี้ (เฉพาะครั้งที่ผ่านการตรวจ) เพื่อคุมคูลดาวน์
        api.conn.execute("UPDATE characters SET last_attack_ms = ? WHERE id = ?", (now, cid))
        weapon_atk, _ = api._equipment_bonus(cid)
        raw = game.attack_power(api._col(ch, "base_atk"), ch["stat_str"], weapon_atk)
        result = api._resolve_hit(ch, monster_id, raw, now)
        result["cooldown_ms"] = cooldown
        result["range"] = api.attack_range
        # มอนสเตอร์ตัวอื่นได้เดิน/โจมตีสวนในจังหวะนี้ด้วย (AI ไม่มีเธรดพื้นหลัง)
        result["world_tick"] = api.tick_monsters(now)
        return 200, result

    @staticmethod
    def h_skill(api, account_id, token, body, cid):
        """ใช้สกิล: ตรวจว่ารู้จักสกิล อยู่ในโลก อยู่ในระยะ พ้นคูลดาวน์ และมี MP/SP พอ"""
        char = api._own_character(account_id, cid)
        if char["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว ต้องฟื้นคืนชีพก่อน")
        skill_id = _need_int(body, "skill_id", 1, 2 ** 31 - 1)
        monster_id = _need_int(body, "monster_id", 1, 2 ** 31 - 1)
        skill = api.content.skill(skill_id)
        if skill is None:
            raise ApiError(404, "skill_not_found", "ไม่พบสกิลนี้")
        learned = api.conn.execute(
            "SELECT last_used_ms FROM character_skills WHERE character_id = ? AND skill_id = ?",
            (cid, skill_id),
        ).fetchone()
        if learned is None:
            raise ApiError(404, "skill_not_learned", "ยังไม่ได้เรียนรู้สกิลนี้")
        now = api.clock_ms()
        if learned["last_used_ms"] and now < learned["last_used_ms"] + skill["cooldown_ms"]:
            retry = learned["last_used_ms"] + skill["cooldown_ms"] - now
            raise ApiError(429, "skill_cooldown", f"สกิลยังคูลดาวน์อยู่ อีก {retry} ms")
        # ต้องอยู่ในโลก และเป้าหมายต้องอยู่ในระยะของสกิลนี้
        pos = api._character_position(cid)
        if pos is None:
            return 409, {"error": {"code": "not_in_world", "message": "ตัวละครยังไม่ได้เข้าโลก"}}
        api._refresh_monsters()
        mon = api.conn.execute("SELECT * FROM monsters WHERE monster_id = ?", (monster_id,)).fetchone()
        if mon is None:
            raise ApiError(404, "monster_not_found", "ไม่พบมอนสเตอร์")
        if mon["respawn_at_ms"] > 0 or mon["hp"] <= 0:
            raise ApiError(409, "monster_dead", "มอนสเตอร์ตายแล้ว รอเกิดใหม่")
        distance = math.hypot(mon["pos_x"] - pos[0], mon["pos_y"] - pos[1])
        if distance > skill["range"]:
            return 409, {"error": {"code": "out_of_range", "message": "เป้าหมายอยู่นอกระยะของสกิล"},
                         "distance": round(distance, 4), "range": skill["range"],
                         "monster": api._monster_view(mon, now)}
        # ค่าใช้จ่าย: MP หรือ SP ตามที่สกิลกำหนด
        cost = skill["mana_cost"] + skill["sp_cost"]
        if skill["cost_stat"] == "sp":
            if char["sp"] < cost:
                raise ApiError(409, "not_enough_sp", "SP ไม่พอ")
            api.conn.execute("UPDATE characters SET sp = sp - ? WHERE id = ?", (cost, cid))
        else:
            if char["mp"] < cost:
                raise ApiError(409, "not_enough_mp", "MP ไม่พอ")
            api.conn.execute("UPDATE characters SET mp = mp - ? WHERE id = ?", (cost, cid))
        api.conn.execute(
            "UPDATE character_skills SET last_used_ms = ? WHERE character_id = ? AND skill_id = ?",
            (now, cid, skill_id),
        )
        char = api.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        stat = {"str": char["stat_str"], "dex": char["stat_dex"],
                "int": char["stat_int"], "con": char["stat_con"]}[skill["stat_scale"]]
        weapon_atk, _ = api._equipment_bonus(cid)
        raw = skill["damage"] + stat + weapon_atk // 2
        info = {"skill_id": skill_id, "name": skill["name"], "effect": skill["effect"],
                "cooldown_ms": skill["cooldown_ms"], "cost": cost, "cost_stat": skill["cost_stat"]}
        result = api._resolve_hit(char, monster_id, raw, now, skill=info)
        result["world_tick"] = api.tick_monsters(now)
        return 200, result

    def h_revive(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        if char["hp"] > 0:
            raise ApiError(409, "not_dead", "ตัวละครยังไม่ตาย")
        hp = max(1, int(game.max_hp(char["level"], char["stat_con"]) * game.REVIVE_HP_RATIO))
        api.conn.execute("UPDATE characters SET hp = ? WHERE id = ?", (hp, cid))
        return 200, api._character_view(cid)

    @staticmethod
    def h_use_item(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        if char["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว")
        slot = _need_int(body, "slot", 0, game.INVENTORY_SLOTS - 1)
        row = api.conn.execute(
            "SELECT inv.id, inv.quantity, it.type, it.hp_recovery, it.mp_recovery, it.sp_recovery "
            "FROM inventory inv JOIN items it ON it.item_id = inv.item_id "
            "WHERE inv.character_id = ? AND inv.slot = ?",
            (cid, slot),
        ).fetchone()
        if row is None:
            raise ApiError(404, "empty_slot", "ช่องนี้ว่าง")
        if row["type"] != "Consumable":
            raise ApiError(409, "not_consumable", "ไอเทมนี้ใช้ไม่ได้")
        hp = min(game.max_hp(char["level"], char["stat_con"]), char["hp"] + row["hp_recovery"])
        mp = min(game.max_mp(char["level"], char["stat_int"]), char["mp"] + row["mp_recovery"])
        sp = min(game.max_sp(char["level"]), char["sp"] + row["sp_recovery"])
        api.conn.execute("UPDATE characters SET hp = ?, mp = ?, sp = ? WHERE id = ?", (hp, mp, sp, cid))
        if row["quantity"] <= 1:
            api.conn.execute("DELETE FROM inventory WHERE id = ?", (row["id"],))
        else:
            api.conn.execute("UPDATE inventory SET quantity = quantity - 1 WHERE id = ?", (row["id"],))
        return 200, api._character_view(cid)

    # ------------------------------------------------------------ อุปกรณ์
    def _equipment_view(self, cid):
        out = {"weapon": None, "armor": None}
        for r in self.conn.execute(
            "SELECT ce.equip_slot, it.item_id, it.name, it.attack_bonus, it.defense_bonus "
            "FROM character_equipment ce JOIN items it ON it.item_id = ce.item_id WHERE ce.character_id = ?",
            (cid,),
        ).fetchall():
            out[r["equip_slot"]] = {"item_id": r["item_id"], "name": r["name"],
                                    "attack_bonus": r["attack_bonus"], "defense_bonus": r["defense_bonus"]}
        return out

    def _equipment_bonus(self, cid):
        """คืน (attack_bonus รวม, defense_bonus รวม) จากอุปกรณ์ที่สวมอยู่"""
        r = self.conn.execute(
            "SELECT COALESCE(SUM(it.attack_bonus), 0) AS atk, COALESCE(SUM(it.defense_bonus), 0) AS dfn "
            "FROM character_equipment ce JOIN items it ON it.item_id = ce.item_id WHERE ce.character_id = ?",
            (cid,),
        ).fetchone()
        return r["atk"], r["dfn"]

    @staticmethod
    def h_equip(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        if char["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว")
        slot = _need_int(body, "slot", 0, game.INVENTORY_SLOTS - 1)
        row = api.conn.execute(
            "SELECT inv.id, inv.quantity, inv.item_id, it.type, it.equip_slot FROM inventory inv "
            "JOIN items it ON it.item_id = inv.item_id WHERE inv.character_id = ? AND inv.slot = ?",
            (cid, slot),
        ).fetchone()
        if row is None:
            raise ApiError(404, "empty_slot", "ช่องนี้ว่าง")
        if row["type"] != "Equipment" or row["equip_slot"] is None:
            raise ApiError(409, "not_equippable", "ไอเทมนี้สวมใส่ไม่ได้")
        # นำชิ้นที่จะสวมออกจากกระเป๋าก่อน แล้วค่อยคืนชิ้นเดิมในช่องเดียวกัน (สลับชิ้น)
        if row["quantity"] <= 1:
            api.conn.execute("DELETE FROM inventory WHERE id = ?", (row["id"],))
        else:
            api.conn.execute("UPDATE inventory SET quantity = quantity - 1 WHERE id = ?", (row["id"],))
        prev = api.conn.execute(
            "SELECT item_id FROM character_equipment WHERE character_id = ? AND equip_slot = ?",
            (cid, row["equip_slot"]),
        ).fetchone()
        if prev is not None:
            # ถ้ากระเป๋าเต็ม ข้อผิดพลาดนี้จะ rollback การนำชิ้นใหม่ออกด้วย
            api._return_to_inventory(cid, prev["item_id"])
            api.conn.execute(
                "DELETE FROM character_equipment WHERE character_id = ? AND equip_slot = ?",
                (cid, row["equip_slot"]),
            )
        api.conn.execute(
            "INSERT INTO character_equipment (character_id, equip_slot, item_id) VALUES (?, ?, ?)",
            (cid, row["equip_slot"], row["item_id"]),
        )
        return 200, api._character_view(cid)

    @staticmethod
    def h_unequip(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        slot_name = body.get("equip_slot")
        if slot_name not in EQUIP_SLOTS:
            raise _bad("equip_slot ต้องเป็น weapon หรือ armor")
        eq = api.conn.execute(
            "SELECT item_id FROM character_equipment WHERE character_id = ? AND equip_slot = ?",
            (cid, slot_name),
        ).fetchone()
        if eq is None:
            raise ApiError(404, "not_equipped", "ไม่ได้สวมอุปกรณ์ในช่องนี้")
        api._return_to_inventory(cid, eq["item_id"])
        api.conn.execute(
            "DELETE FROM character_equipment WHERE character_id = ? AND equip_slot = ?", (cid, slot_name)
        )
        return 200, api._character_view(cid)

    def _return_to_inventory(self, cid, item_id):
        item = self._item(item_id)
        updates, inserts = self._plan_add(cid, item, 1)
        self._apply_add(cid, item_id, updates, inserts)

    # ------------------------------------------------------------ handlers (ร้านค้า)
    @staticmethod
    def h_shop_items(api, account_id, token, body):
        rows = api.conn.execute(
            "SELECT item_id, name, type, price, stackable, max_stack FROM items WHERE price > 0 ORDER BY item_id"
        ).fetchall()
        return 200, {"items": [dict(r) for r in rows]}

    @staticmethod
    def h_buy(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        item_id = _need_int(body, "item_id", 1, 2 ** 31 - 1)
        qty = _need_int(body, "quantity", 1, game.MAX_QUANTITY_PER_ACTION)
        item = api._item(item_id)
        if item["price"] <= 0:
            raise ApiError(409, "not_for_sale", "ไอเทมนี้ซื้อไม่ได้")
        total = item["price"] * qty
        if char["money"] < total:
            raise ApiError(409, "not_enough_money", "เงินไม่พอ")
        updates, inserts = api._plan_add(cid, item, qty)
        api._apply_add(cid, item_id, updates, inserts)
        api.conn.execute("UPDATE characters SET money = money - ? WHERE id = ?", (total, cid))
        return 200, api._character_view(cid)

    @staticmethod
    def h_sell(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        slot = _need_int(body, "slot", 0, game.INVENTORY_SLOTS - 1)
        row = api.conn.execute(
            "SELECT inv.id, inv.quantity, inv.item_id, it.price FROM inventory inv "
            "JOIN items it ON it.item_id = inv.item_id WHERE inv.character_id = ? AND inv.slot = ?",
            (cid, slot),
        ).fetchone()
        if row is None:
            raise ApiError(404, "empty_slot", "ช่องนี้ว่าง")
        qty = _need_int(body, "quantity", 1, row["quantity"])
        if row["price"] <= 0:
            raise ApiError(409, "not_sellable", "ไอเทมนี้ขายไม่ได้")
        income = game.sell_price(row["price"], qty)
        if qty == row["quantity"]:
            api.conn.execute("DELETE FROM inventory WHERE id = ?", (row["id"],))
        else:
            api.conn.execute("UPDATE inventory SET quantity = quantity - ? WHERE id = ?", (qty, row["id"]))
        api.conn.execute("UPDATE characters SET money = money + ? WHERE id = ?", (income, cid))
        return 200, api._character_view(cid)

    # ------------------------------------------------------------ handlers (เควสต์)
    @staticmethod
    def h_accept_quest(api, account_id, token, body, cid, quest_id):
        api._own_character(account_id, cid)
        q = api.conn.execute("SELECT quest_id FROM quests WHERE quest_id = ?", (quest_id,)).fetchone()
        if q is None:
            raise ApiError(404, "quest_not_found", "ไม่พบเควสต์")
        if api.conn.execute(
            "SELECT 1 FROM character_quests WHERE character_id = ? AND quest_id = ?", (cid, quest_id)
        ).fetchone():
            raise ApiError(409, "quest_already_accepted", "รับเควสต์นี้แล้ว")
        api.conn.execute(
            "INSERT INTO character_quests (character_id, quest_id, progress, status) VALUES (?, ?, 0, 'accepted')",
            (cid, quest_id),
        )
        return 201, api._character_view(cid)

    # ------------------------------------------------------------ โลกเกม (World)
    def _map_row(self, map_id):
        row = self.conn.execute("SELECT * FROM maps WHERE map_id = ?", (map_id,)).fetchone()
        if row is None:
            raise ApiError(404, "map_not_found", "ไม่พบแผนที่")
        return row

    @staticmethod
    def _col(row, name, default=0):
        """อ่านคอลัมน์จาก sqlite3.Row แบบปลอดภัย (คอลัมน์ที่ migration อาจยังไม่เพิ่ม)"""
        try:
            value = row[name]
        except (IndexError, KeyError):
            return default
        return default if value is None else value

    def _player_stats(self, c):
        """ค่าสถานะที่ Client ใช้แสดงผล/คำนวณ (Server ยังเป็นผู้ตัดสินทุกอย่าง)"""
        level = c["level"]
        weapon_atk, armor_def = self._equipment_bonus(c["id"])
        return {
            "level": level, "exp": c["exp"], "exp_required": game.exp_required(level),
            "hp": c["hp"], "max_hp": game.max_hp(level, c["stat_con"]),
            "mp": c["mp"], "max_mp": game.max_mp(level, c["stat_int"]),
            "sp": c["sp"], "max_sp": game.max_sp(level),
            # ค่าโจมตี/ป้องกันที่แสดงผล: มาจากค่าถาวร (base_atk/base_def) + สเตตัส + อุปกรณ์
            "atk": game.attack_power(self._col(c, "base_atk"), c["stat_str"], weapon_atk),
            "def": game.defense_power(self._col(c, "base_def"), c["stat_con"] // 2, armor_def),
            "money": c["money"], "dead": c["hp"] <= 0,
        }

    def _other_players(self, cid, map_id, px, py):
        """ผู้เล่นคนอื่นที่อยู่ในโลกและอยู่แผนที่เดียวกัน ภายในระยะสายตา"""
        rows = self.conn.execute(
            "SELECT c.id, c.name, c.class, c.level, c.hp, c.pos_x, c.pos_y FROM characters c"
            " JOIN world_sessions w ON w.character_id = c.id"
            " WHERE w.map_id = ? AND c.id != ? ORDER BY c.id", (map_id, cid),
        ).fetchall()
        out = []
        for r in rows:
            d = math.hypot(r["pos_x"] - px, r["pos_y"] - py)
            if d > self.view_range:
                continue
            out.append({"character_id": r["id"], "name": r["name"], "class": r["class"],
                        "level": r["level"], "hp": r["hp"],
                        "position": {"x": r["pos_x"], "y": r["pos_y"]}, "distance": round(d, 4)})
        return out

    def _world_view(self, cid):
        c = self.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        m = self._map_row(c["map_id"])
        self._refresh_monsters()
        now = self.clock_ms()
        px, py = c["pos_x"], c["pos_y"]
        monsters = []
        for r in self.conn.execute(
                "SELECT * FROM monsters WHERE map_id = ? ORDER BY monster_id", (c["map_id"],)).fetchall():
            v = self._monster_view(r, now)
            v["distance"] = round(math.hypot(r["pos_x"] - px, r["pos_y"] - py), 4)
            v["in_range"] = (not v["dead"]) and v["distance"] <= self.attack_range
            monsters.append(v)
        npcs = [self._npc_view(r) for r in self.conn.execute(
            "SELECT * FROM npcs WHERE map_id = ? ORDER BY npc_id", (c["map_id"],)).fetchall()]
        return {
            "map_id": m["map_id"],
            "map_name": m["name"],
            "spawn": {"x": m["spawn_x"], "y": m["spawn_y"]},
            "blocks": json.loads(m["blocks_json"]),
            "position": {"x": px, "y": py},
            "monsters": monsters,
            "npcs": npcs,
            "players": self._other_players(cid, c["map_id"], px, py),
            "attack": {"range": self.attack_range, "cooldown_ms": self.attack_cooldown},
            "player": self._player_stats(c),
            "connected": True,
        }

    def _session_for(self, account_id, cid):
        """คืนแถว session ของตัวละครที่เป็นของบัญชีนี้ หรือ None (ตรวจความเป็นเจ้าของก่อนเสมอ)"""
        self._own_character(account_id, cid)
        return self.conn.execute("SELECT * FROM world_sessions WHERE character_id = ?", (cid,)).fetchone()

    @staticmethod
    def h_enter_world(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        now = api.clock_ms()
        row = api.conn.execute("SELECT * FROM world_sessions WHERE character_id = ?", (cid,)).fetchone()
        if row is None:
            c = api.conn.execute("SELECT map_id FROM characters WHERE id = ?", (cid,)).fetchone()
            api._map_row(c["map_id"])
            api.conn.execute(
                "INSERT INTO world_sessions (character_id, map_id, entered_ms, last_move_ms, last_seen_ms,"
                " move_budget, last_move_req_ms)"
                " VALUES (?, ?, ?, ?, ?, ?, 0)",
                (cid, c["map_id"], now, now, now, world.BUDGET_CAP),
            )
            # เริ่มนับระยะเดินจากเวลาที่เข้าโลก
            api.conn.execute("UPDATE characters SET pos_updated_ms = ? WHERE id = ?", (now, cid))
            status = "entered"
            log_world.info("character %s entered world map=%s", cid, c["map_id"])
        else:
            # เชื่อมต่อใหม่ (reconnect) ใช้ตำแหน่งเดิมที่บันทึกไว้
            api.conn.execute("UPDATE world_sessions SET last_seen_ms = ? WHERE character_id = ?", (now, cid))
            status = "resumed"
            log_world.info("character %s resumed world session", cid)
        return 200, {
            "status": status,
            "character": api._character_view(cid),
            "world": api._world_view(cid),
        }

    @staticmethod
    def h_world_state(api, account_id, token, body, cid):
        row = api._session_for(account_id, cid)
        if row is None:
            c = api.conn.execute("SELECT pos_x, pos_y, map_id FROM characters WHERE id = ?", (cid,)).fetchone()
            return 200, {"connected": False, "map_id": c["map_id"],
                         "position": {"x": c["pos_x"], "y": c["pos_y"]}}
        now = api.clock_ms()
        api.conn.execute("UPDATE world_sessions SET last_seen_ms = ? WHERE character_id = ?",
                         (now, cid))
        api.tick_monsters(now)
        return 200, {"connected": True, "world": api._world_view(cid)}

    @staticmethod
    def h_move(api, account_id, token, body, cid):
        row = api._session_for(account_id, cid)
        c = api.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        current = {"x": c["pos_x"], "y": c["pos_y"]}
        if row is None:
            return 409, {"error": {"code": "not_in_world", "message": "ตัวละครยังไม่ได้เข้าโลก"},
                         "position": current}
        x, y = body.get("x"), body.get("y")
        if not world.is_coord(x) or not world.is_coord(y):
            raise _bad("x และ y ต้องเป็นตัวเลขระหว่าง 0 ถึง 1", "invalid_position")
        map_row = api._map_row(c["map_id"])
        now = api.clock_ms()
        # ชั้นที่ 1: จำนวนคำขอ (กัน Client ส่งถี่จนกินทรัพยากร) — ชั้นที่ 2 คือถังระยะด้านล่าง
        fast_ok, wait_ms = world.check_move_interval(row["last_move_req_ms"], now)
        if not fast_ok:
            # ไม่เขียนอะไรลงฐานข้อมูล และไม่นับเป็นการติดต่อ จึงไม่ยืดอายุ session
            log_world.info("move rate-limited character=%s wait_ms=%d", cid, wait_ms)
            return 429, {"error": {"code": "move_too_fast",
                                   "message": f"ส่งคำขอเดินถี่เกินไป กรุณารอ {wait_ms} ms"},
                         "position": current, "retry_after_ms": wait_ms}
        # คำขอที่ผ่านการจำกัดความถี่แล้วกินโควตาทันที (แม้ต่อมาจะถูกปฏิเสธด้วยระยะ/สิ่งกีดขวาง)
        # และถือว่า Client ยังเชื่อมต่ออยู่ (heartbeat)
        api.conn.execute("UPDATE world_sessions SET last_move_req_ms = ?, last_seen_ms = ? WHERE character_id = ?",
                         (now, now, cid))
        ok, code, budget = world.check_move(c["pos_x"], c["pos_y"], float(x), float(y),
                                            now - c["pos_updated_ms"], row["move_budget"],
                                            json.loads(map_row["blocks_json"]))
        if not ok:
            log_world.info("move rejected character=%s code=%s", cid, code)
            message = {"position_rejected": "ระยะเดินเกินที่กำหนด",
                       "blocked": "เดินเข้าสิ่งกีดขวาง"}[code]
            # ตอบตำแหน่งที่ Server ยอมรับจริง เพื่อให้ Client ย้อนกลับ
            return 409, {"error": {"code": code, "message": message}, "position": current}
        api.conn.execute("UPDATE characters SET pos_x = ?, pos_y = ?, pos_updated_ms = ? WHERE id = ?",
                         (float(x), float(y), now, cid))
        api.conn.execute("UPDATE world_sessions SET last_move_ms = ?, move_budget = ? WHERE character_id = ?",
                         (now, budget, cid))
        return 200, {"accepted": True, "position": {"x": float(x), "y": float(y)}}

    @staticmethod
    def h_world_sync(api, account_id, token, body, cid):
        """สถานะโลกแบบเบา สำหรับวาดผู้เล่นคนอื่น/มอนสเตอร์ที่กำลังเคลื่อนไหว (Client เรียกทุก ~250 ms)"""
        row = api._session_for(account_id, cid)
        c = api.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        if row is None:
            return 200, {"connected": False, "player": api._player_stats(c),
                         "position": {"x": c["pos_x"], "y": c["pos_y"]},
                         "players": [], "monsters": [], "map_id": c["map_id"]}
        now = api.clock_ms()
        api.conn.execute("UPDATE world_sessions SET last_seen_ms = ? WHERE character_id = ?", (now, cid))
        tick = api.tick_monsters(now)
        c = api.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        monsters = []
        for r in api.conn.execute("SELECT * FROM monsters WHERE map_id = ? ORDER BY monster_id",
                                  (c["map_id"],)).fetchall():
            v = api._monster_view(r, now)
            v["distance"] = round(math.hypot(r["pos_x"] - c["pos_x"], r["pos_y"] - c["pos_y"]), 4)
            v["in_range"] = (not v["dead"]) and v["distance"] <= api.attack_range
            monsters.append(v)
        return 200, {
            "connected": True, "map_id": c["map_id"], "tick_ms": now,
            "position": {"x": c["pos_x"], "y": c["pos_y"]},
            "player": api._player_stats(c),
            "players": api._other_players(cid, c["map_id"], c["pos_x"], c["pos_y"]),
            "monsters": monsters,
            "attack": {"range": api.attack_range, "cooldown_ms": api.attack_cooldown},
            "events": tick,
        }

    @staticmethod
    def h_leave_world(api, account_id, token, body, cid):
        api._own_character(account_id, cid)
        api.conn.execute("DELETE FROM world_sessions WHERE character_id = ?", (cid,))
        log_world.info("character %s left world", cid)
        c = api.conn.execute("SELECT pos_x, pos_y FROM characters WHERE id = ?", (cid,)).fetchone()
        return 200, {"connected": False, "position": {"x": c["pos_x"], "y": c["pos_y"]}}


class _Tx:
    """BEGIN IMMEDIATE ... COMMIT / ROLLBACK โดยถือตัวล็อกของ connection ไว้ตลอดธุรกรรม"""

    def __init__(self, conn):
        self.conn = conn
        self.lock = getattr(conn, "_lock", None)

    def __enter__(self):
        if self.lock is not None:
            self.lock.acquire()
        try:
            self.conn.execute("BEGIN IMMEDIATE")
        except Exception:
            if self.lock is not None:
                self.lock.release()
            raise
        return self

    def __exit__(self, exc_type, exc, tb):
        try:
            if exc_type is None:
                self.conn.execute("COMMIT")
            else:
                self.conn.execute("ROLLBACK")
        finally:
            if self.lock is not None:
                self.lock.release()
        return False


_P = r"/api"
ROUTES = [
    ("GET", re.compile(_P + r"/health"), Api.h_health, False, False),
    ("POST", re.compile(_P + r"/auth/register"), Api.h_register, False, False),
    ("POST", re.compile(_P + r"/auth/login"), Api.h_login, False, False),
    ("POST", re.compile(_P + r"/auth/logout"), Api.h_logout, True, False),
    ("GET", re.compile(_P + r"/characters"), Api.h_list_characters, True, False),
    ("POST", re.compile(_P + r"/characters"), Api.h_create_character, True, True),
    ("GET", re.compile(_P + r"/characters/(\d+)"), Api.h_get_character, True, False),
    ("DELETE", re.compile(_P + r"/characters/(\d+)"), Api.h_delete_character, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/combat/attack"), Api.h_attack, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/combat/skill"), Api.h_skill, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/revive"), Api.h_revive, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/inventory/use"), Api.h_use_item, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/shop/buy"), Api.h_buy, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/shop/sell"), Api.h_sell, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/equip"), Api.h_equip, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/unequip"), Api.h_unequip, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/quests/(\d+)/accept"), Api.h_accept_quest, True, True),
    ("GET", re.compile(_P + r"/world/monsters"), Api.h_monsters, True, False),
    ("GET", re.compile(_P + r"/world/npcs"), Api.h_npcs, True, False),
    ("POST", re.compile(_P + r"/characters/(\d+)/world/enter"), Api.h_enter_world, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/world/move"), Api.h_move, True, True),
    ("POST", re.compile(_P + r"/characters/(\d+)/world/leave"), Api.h_leave_world, True, True),
    ("GET", re.compile(_P + r"/characters/(\d+)/world"), Api.h_world_state, True, False),
    ("GET", re.compile(_P + r"/characters/(\d+)/world/sync"), Api.h_world_sync, True, False),
    ("GET", re.compile(_P + r"/shop/items"), Api.h_shop_items, True, False),
]
