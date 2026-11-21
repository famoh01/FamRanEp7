# ran_mobile/Server/ranserver/api.py
# API ของเซิร์ฟเวอร์ RAN MOBILE
# - ทุกการตัดสิน (ดาเมจ ไอเทม เงิน EXP เลเวล) ทำในเซิร์ฟเวอร์เท่านั้น Client ส่งแค่ "คำขอ"
# - ทุกคำสั่งที่เปลี่ยนข้อมูลทำในธุรกรรม (transaction) เดียว ถ้าผิดพลาดจะ rollback ทั้งหมด
# - รองรับ Idempotency-Key เพื่อกันคำขอซ้ำ (เช่นกดซื้อสองครั้งเพราะเน็ตหลุด)
import json
import random
import re
import sqlite3
import threading
import time
from datetime import datetime, timezone

from . import game
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

    def __init__(self, conn, rng=None, clock_ms=None, clock_s=None):
        self.conn = conn
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA foreign_keys = ON")
        self.rng = rng or random.Random()
        self.clock_ms = clock_ms or (lambda: int(time.time() * 1000))
        self.clock_s = clock_s or (lambda: int(time.time()))
        self.lock = threading.RLock()

    # ------------------------------------------------------------ infrastructure
    def handle(self, method, path, headers, body_bytes):
        with self.lock:
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
            account_id = None
            token = None
            if needs_auth:
                account_id, token = self._authenticate(headers)
            idem_key = headers.get("idempotency-key") if mutating else None
            if idem_key is not None:
                if not IDEM_KEY_RE.match(idem_key):
                    raise _bad("Idempotency-Key ไม่ถูกต้อง", "invalid_idempotency_key")
                cached = self._idem_lookup(account_id, idem_key)
                if cached is not None:
                    return cached
            args = m.groups()
            with self._tx():
                status, payload = handler(self, account_id, token, body, *args)
                if idem_key is not None and 200 <= status < 300:
                    self._idem_store(account_id, idem_key, status, payload)
            return status, payload
        if path_matched:
            raise ApiError(405, "method_not_allowed", "method ไม่ได้รับอนุญาต")
        raise ApiError(404, "not_found", "ไม่พบเส้นทาง")

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

    def _resolve_hit(self, char, monster_id, raw_damage, now):
        """ใช้ดาเมจกับมอนสเตอร์ คืน dict ผลลัพธ์ และอัปเดตแถวตัวละครใน self.conn (ไม่ commit เอง)"""
        mon = self.conn.execute("SELECT * FROM monsters WHERE monster_id = ?", (monster_id,)).fetchone()
        if mon is None:
            raise ApiError(404, "monster_not_found", "ไม่พบมอนสเตอร์")
        if mon["respawn_at_ms"] > 0 or mon["hp"] <= 0:
            raise ApiError(409, "monster_dead", "มอนสเตอร์ตายแล้ว รอเกิดใหม่")

        damage, crit, missed = game.roll_hit(raw_damage, mon["defense"], self.rng)
        new_mon_hp = max(0, mon["hp"] - damage)
        killed = new_mon_hp == 0
        if killed:
            respawn = now + game.MONSTER_RESPAWN_MS
            self.conn.execute(
                "UPDATE monsters SET hp = 0, respawn_at_ms = ? WHERE monster_id = ?", (respawn, monster_id)
            )
        else:
            self.conn.execute("UPDATE monsters SET hp = ? WHERE monster_id = ?", (new_mon_hp, monster_id))

        result = {
            "hit": not missed, "crit": crit, "damage": damage,
            "monster": {"monster_id": monster_id, "name": mon["name"], "hp": new_mon_hp,
                        "max_hp": mon["max_hp"], "dead": killed},
            "rewards": None, "quest_progress": [], "retaliation_damage": 0,
        }

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
        elif not missed:
            # มอนสเตอร์ยังไม่ตายจึงตอบโต้
            retaliation, _, _ = game.roll_hit(mon["attack"], char["stat_con"] // 2, self.rng)
            hp = max(0, hp - retaliation)
            result["retaliation_damage"] = retaliation

        level, exp, gained = game.apply_exp(char["level"], char["exp"], exp_gain)
        money = char["money"] + money_gain
        if gained > 0:
            hp = game.max_hp(level, char["stat_con"])
            mp = game.max_mp(level, char["stat_int"])
            sp = game.max_sp(level)
        else:
            mp = char["mp"]
            sp = char["sp"]
        self.conn.execute(
            "UPDATE characters SET level = ?, exp = ?, money = ?, hp = ?, mp = ?, sp = ? WHERE id = ?",
            (level, exp, money, hp, mp, sp, char["id"]),
        )
        if exp_gain or money_gain:
            result["rewards"] = {"exp": exp_gain, "money": money_gain, "levels_gained": gained}
        result["player"] = {"hp": hp, "max_hp": game.max_hp(level, char["stat_con"]), "dead": hp <= 0}
        return result

    # ------------------------------------------------------------ handlers (กลุ่มบัญชี)
    @staticmethod
    def h_health(api, account_id, token, body):
        return 200, {"ok": True, "service": "ran_mobile"}

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
        if api.conn.execute("SELECT 1 FROM accounts WHERE username = ?", (username,)).fetchone():
            raise ApiError(409, "username_taken", "ชื่อผู้ใช้นี้ถูกใช้แล้ว")
        cur = api.conn.execute(
            "INSERT INTO accounts (username, password_hash, email, created_at) VALUES (?, ?, ?, ?)",
            (username, hash_password(password), email, _now_iso()),
        )
        return 201, {"account_id": cur.lastrowid, "username": username}

    @staticmethod
    def h_login(api, account_id, token, body):
        username = body.get("username")
        password = body.get("password")
        if not isinstance(username, str) or not isinstance(password, str) or len(password) > PASSWORD_MAX:
            raise ApiError(401, "invalid_credentials", "ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง")
        row = api.conn.execute(
            "SELECT id, password_hash FROM accounts WHERE username = ?", (username,)
        ).fetchone()
        stored = row["password_hash"] if row else DUMMY_HASH
        ok = verify_password(password, stored)
        if row is None or not ok:
            raise ApiError(401, "invalid_credentials", "ชื่อผู้ใช้หรือรหัสผ่านไม่ถูกต้อง")
        now_s = api.clock_s()
        api.conn.execute("DELETE FROM sessions WHERE account_id = ? AND expires_at <= ?", (row["id"], now_s))
        new = new_token()
        expires = now_s + SESSION_TTL_SECONDS
        api.conn.execute(
            "INSERT INTO sessions (token_hash, account_id, expires_at) VALUES (?, ?, ?)",
            (hash_token(new), row["id"], expires),
        )
        api.conn.execute("UPDATE accounts SET last_login = ? WHERE id = ?", (_now_iso(), row["id"]))
        return 200, {"token": new, "expires_at": expires, "account_id": row["id"]}

    @staticmethod
    def h_logout(api, account_id, token, body):
        api.conn.execute("DELETE FROM sessions WHERE token_hash = ?", (hash_token(token),))
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
        api._refresh_monsters()
        now = api.clock_ms()
        rows = api.conn.execute("SELECT * FROM monsters ORDER BY monster_id").fetchall()
        return 200, {"monsters": [
            {"monster_id": m["monster_id"], "name": m["name"], "hp": m["hp"], "max_hp": m["max_hp"],
             "dead": m["respawn_at_ms"] > 0 or m["hp"] <= 0,
             "respawn_in_ms": max(0, m["respawn_at_ms"] - now) if m["respawn_at_ms"] > 0 else 0}
            for m in rows
        ]}

    @staticmethod
    def h_attack(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        if char["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว ต้องฟื้นคืนชีพก่อน")
        monster_id = _need_int(body, "monster_id", 1, 2 ** 31 - 1)
        api._refresh_monsters()
        raw = game.BASIC_ATTACK_BASE + char["stat_str"]
        result = api._resolve_hit(char, monster_id, raw, api.clock_ms())
        return 200, result

    @staticmethod
    def h_skill(api, account_id, token, body, cid):
        char = api._own_character(account_id, cid)
        if char["hp"] <= 0:
            raise ApiError(409, "character_dead", "ตัวละครตายแล้ว ต้องฟื้นคืนชีพก่อน")
        skill_id = _need_int(body, "skill_id", 1, 2 ** 31 - 1)
        monster_id = _need_int(body, "monster_id", 1, 2 ** 31 - 1)
        now = api.clock_ms()
        learned = api.conn.execute(
            "SELECT cs.last_used_ms, sk.damage, sk.mana_cost, sk.cooldown_ms, sk.name "
            "FROM character_skills cs JOIN skills sk ON sk.skill_id = cs.skill_id "
            "WHERE cs.character_id = ? AND cs.skill_id = ?",
            (cid, skill_id),
        ).fetchone()
        if learned is None:
            raise ApiError(404, "skill_not_learned", "ยังไม่ได้เรียนรู้สกิลนี้")
        if learned["last_used_ms"] and now < learned["last_used_ms"] + learned["cooldown_ms"]:
            retry = learned["last_used_ms"] + learned["cooldown_ms"] - now
            raise ApiError(429, "skill_cooldown", f"สกิลยังคูลดาวน์อยู่ อีก {retry} ms")
        if char["mp"] < learned["mana_cost"]:
            raise ApiError(409, "not_enough_mp", "MP ไม่พอ")
        api._refresh_monsters()
        api.conn.execute("UPDATE characters SET mp = mp - ? WHERE id = ?", (learned["mana_cost"], cid))
        api.conn.execute(
            "UPDATE character_skills SET last_used_ms = ? WHERE character_id = ? AND skill_id = ?",
            (now, cid, skill_id),
        )
        char = api.conn.execute("SELECT * FROM characters WHERE id = ?", (cid,)).fetchone()
        raw = learned["damage"] + char["stat_int"]
        result = api._resolve_hit(char, monster_id, raw, now)
        result["skill"] = {"skill_id": skill_id, "name": learned["name"]}
        return 200, result

    @staticmethod
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


class _Tx:
    """BEGIN IMMEDIATE ... COMMIT / ROLLBACK"""

    def __init__(self, conn):
        self.conn = conn

    def __enter__(self):
        self.conn.execute("BEGIN IMMEDIATE")
        return self

    def __exit__(self, exc_type, exc, tb):
        if exc_type is None:
            self.conn.execute("COMMIT")
        else:
            self.conn.execute("ROLLBACK")
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
    ("POST", re.compile(_P + r"/characters/(\d+)/quests/(\d+)/accept"), Api.h_accept_quest, True, True),
    ("GET", re.compile(_P + r"/world/monsters"), Api.h_monsters, True, False),
    ("GET", re.compile(_P + r"/shop/items"), Api.h_shop_items, True, False),
]
