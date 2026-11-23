# ran_mobile/Server/ranserver/content.py
# โหลด "ชุดข้อมูลเกม" จากไฟล์ JSON (ran_mobile/Database/content/content.json)
# ทุกค่าที่เป็นกติกาเกม (ระยะโจมตี คูลดาวน์ ค่ามอนสเตอร์ สกิล ของดรอป บทสนทนา NPC) อยู่ในไฟล์ข้อมูล
# ไม่ใช่ในโค้ด: ปรับ балансได้โดยไม่ต้องแก้โปรแกรม และไฟล์ที่ผิดรูปแบบจะถูกปฏิเสธพร้อมบอกบรรทัด/ฟิลด์
# ข้อมูลทุกอย่างในไฟล์นี้เขียนขึ้นเองสำหรับโปรเจกต์นี้ ไม่ได้คัดลอกจากเกมอื่น
import json
from pathlib import Path

DEFAULT_PATH = Path(__file__).resolve().parents[2] / "Database" / "content" / "content.json"

STAT_KEYS = ("str", "dex", "int", "con")
COST_STATS = ("mp", "sp")


class ContentError(Exception):
    """ไฟล์ข้อมูลเกมผิดรูปแบบ (ข้อความบอกฟิลด์ที่ผิด)"""


def _num(obj, key, lo, hi, where):
    v = obj.get(key)
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise ContentError(f"{where}.{key} ต้องเป็นตัวเลข")
    if v < lo or v > hi:
        raise ContentError(f"{where}.{key} ต้องอยู่ระหว่าง {lo} ถึง {hi}")
    return v


def _int(obj, key, lo, hi, where):
    v = obj.get(key)
    if isinstance(v, bool) or not isinstance(v, int):
        raise ContentError(f"{where}.{key} ต้องเป็นจำนวนเต็ม")
    if v < lo or v > hi:
        raise ContentError(f"{where}.{key} ต้องอยู่ระหว่าง {lo} ถึง {hi}")
    return v


def _str(obj, key, where, max_len=64):
    v = obj.get(key)
    if not isinstance(v, str) or not v.strip() or len(v) > max_len:
        raise ContentError(f"{where}.{key} ต้องเป็นข้อความ 1-{max_len} ตัวอักษร")
    return v


def _pos(obj, where):
    p = obj.get("pos")
    if not isinstance(p, list) or len(p) != 2:
        raise ContentError(f"{where}.pos ต้องเป็น [x, y]")
    for i, v in enumerate(p):
        if isinstance(v, bool) or not isinstance(v, (int, float)) or not (0.0 <= v <= 1.0):
            raise ContentError(f"{where}.pos[{i}] ต้องอยู่ระหว่าง 0 ถึง 1")
    return float(p[0]), float(p[1])


def _list(obj, key, where):
    v = obj.get(key, [])
    if not isinstance(v, list):
        raise ContentError(f"{where}.{key} ต้องเป็นรายการ")
    return v


def _combat(raw):
    where = "combat"
    if not isinstance(raw, dict):
        raise ContentError("combat ต้องเป็นออบเจกต์")
    cfg = {
        "attack_range": _num(raw, "attack_range", 0.01, 1.0, where),
        "attack_cooldown_ms": _int(raw, "attack_cooldown_ms", 100, 60_000, where),
        "basic_attack_base": _int(raw, "basic_attack_base", 1, 10_000, where),
        "basic_attack_str_scale": _num(raw, "basic_attack_str_scale", 0.0, 10.0, where),
        "crit_chance": _num(raw, "crit_chance", 0.0, 1.0, where),
        "crit_multiplier": _num(raw, "crit_multiplier", 1.0, 10.0, where),
        "miss_chance": _num(raw, "miss_chance", 0.0, 1.0, where),
        "monster_respawn_ms": _int(raw, "monster_respawn_ms", 1000, 3_600_000, where),
        "monster_aggro_range": _num(raw, "monster_aggro_range", 0.01, 1.0, where),
        "monster_attack_range": _num(raw, "monster_attack_range", 0.01, 1.0, where),
        "monster_attack_cooldown_ms": _int(raw, "monster_attack_cooldown_ms", 100, 60_000, where),
        "monster_chase_speed": _num(raw, "monster_chase_speed", 0.0, 1.0, where),
        "monster_patrol_radius": _num(raw, "monster_patrol_radius", 0.0, 0.5, where),
        "monster_xp_per_level": _int(raw, "monster_xp_per_level", 0, 1000, where),
    }
    if cfg["crit_chance"] + cfg["miss_chance"] > 1.0:
        raise ContentError("combat: crit_chance + miss_chance ต้องไม่เกิน 1")
    return cfg


def _level_up(raw):
    where = "level_up"
    if not isinstance(raw, dict):
        raise ContentError("level_up ต้องเป็นออบเจกต์")
    return {
        "hp_per_level": _int(raw, "hp_per_level", 0, 10_000, where),
        "mp_per_level": _int(raw, "mp_per_level", 0, 10_000, where),
        "sp_per_level": _int(raw, "sp_per_level", 0, 10_000, where),
        "atk_per_level": _int(raw, "atk_per_level", 0, 100, where),
        "def_per_level": _int(raw, "def_per_level", 0, 100, where),
        "exp_base": _int(raw, "exp_base", 1, 1_000_000, where),
        "exp_growth": _int(raw, "exp_growth", 0, 1_000_000, where),
    }


def _monsters(raw_list, item_ids):
    out = []
    for i, m in enumerate(raw_list):
        where = f"monsters[{i}]"
        if not isinstance(m, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        mid = _int(m, "monster_id", 1, 2 ** 31 - 1, where)
        drops = []
        for j, d in enumerate(_list(m, "drops", where)):
            dwhere = f"{where}.drops[{j}]"
            if not isinstance(d, dict):
                raise ContentError(f"{dwhere} ต้องเป็นออบเจกต์")
            item_id = _int(d, "item_id", 1, 2 ** 31 - 1, dwhere)
            if item_ids is not None and item_id not in item_ids:
                raise ContentError(f"{dwhere}.item_id {item_id} ไม่มีในตาราง items")
            lo = _int(d, "min", 1, 999, dwhere)
            hi = _int(d, "max", 1, 999, dwhere)
            if hi < lo:
                raise ContentError(f"{dwhere}: max ต้องไม่น้อยกว่า min")
            drops.append({"item_id": item_id, "chance": _num(d, "chance", 0.0, 1.0, dwhere),
                          "min": lo, "max": hi})
        x, y = _pos(m, where)
        out.append({
            "monster_id": mid, "name": _str(m, "name", where), "level": _int(m, "level", 1, 999, where),
            "max_hp": _int(m, "max_hp", 1, 1_000_000, where),
            "defense": _int(m, "defense", 0, 10_000, where),
            "attack": _int(m, "attack", 0, 10_000, where),
            "exp_reward": _int(m, "exp_reward", 0, 10_000_000, where),
            "money_reward": _int(m, "money_reward", 0, 10_000_000, where),
            "map_id": _int(m, "map_id", 1, 2 ** 31 - 1, where),
            "pos_x": x, "pos_y": y,
            "patrol": _num(m, "patrol", 0.0, 1.0, where),
            "drops": drops,
        })
    if len({m["monster_id"] for m in out}) != len(out):
        raise ContentError("monsters: monster_id ซ้ำกัน")
    return out


def _npcs(raw_list):
    out = []
    for i, n in enumerate(raw_list):
        where = f"npcs[{i}]"
        if not isinstance(n, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        dialog = _list(n, "dialog", where)
        for j, line in enumerate(dialog):
            if not isinstance(line, str) or not line or len(line) > 200:
                raise ContentError(f"{where}.dialog[{j}] ต้องเป็นข้อความ 1-200 ตัวอักษร")
        x, y = _pos(n, where)
        shop_name = n.get("shop_name", "")
        if not isinstance(shop_name, str) or len(shop_name) > 60:
            raise ContentError(f"{where}.shop_name ต้องเป็นข้อความไม่เกิน 60 ตัวอักษร")
        # อัตรารับซื้อของร้าน (เปอร์เซ็นต์ของราคาปกติ) — ค่าเริ่มต้น 30 ตามร้านค้าทั่วไป
        rate = n.get("sell_rate_percent", 30)
        if not isinstance(rate, int) or isinstance(rate, bool) or not 1 <= rate <= 100:
            raise ContentError(f"{where}.sell_rate_percent ต้องเป็นจำนวนเต็ม 1-100")
        out.append({
            "npc_id": _int(n, "npc_id", 1, 2 ** 31 - 1, where), "name": _str(n, "name", where),
            "role": _str(n, "role", where, 24), "map_id": _int(n, "map_id", 1, 2 ** 31 - 1, where),
            "pos_x": x, "pos_y": y, "dialog": dialog,
            "shop_name": shop_name, "sell_rate_percent": rate,
        })
    if len({n["npc_id"] for n in out}) != len(out):
        raise ContentError("npcs: npc_id ซ้ำกัน")
    return out


def _skills(raw_list):
    out = []
    for i, s in enumerate(raw_list):
        where = f"skills[{i}]"
        if not isinstance(s, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        scale = s.get("stat_scale", "str")
        if scale not in STAT_KEYS:
            raise ContentError(f"{where}.stat_scale ต้องเป็นหนึ่งใน {', '.join(STAT_KEYS)}")
        cost_stat = s.get("cost_stat", "mp")
        if cost_stat not in COST_STATS:
            raise ContentError(f"{where}.cost_stat ต้องเป็น mp หรือ sp")
        out.append({
            "skill_id": _int(s, "skill_id", 1, 2 ** 31 - 1, where),
            "name": _str(s, "name", where),
            "damage": _int(s, "damage", 0, 1_000_000, where),
            "mana_cost": _int(s, "mana_cost", 0, 100_000, where),
            "sp_cost": _int(s, "sp_cost", 0, 100_000, where) if "sp_cost" in s else 0,
            "cooldown_ms": _int(s, "cooldown_ms", 0, 600_000, where),
            "range": _num(s, "range", 0.01, 1.0, where),
            "stat_scale": scale, "cost_stat": cost_stat,
            "effect": _str(s, "effect", where, 24) if "effect" in s else "slash",
        })
    if len({x["skill_id"] for x in out}) != len(out):
        raise ContentError("skills: skill_id ซ้ำกัน")
    return out


def _maps(raw_list):
    out = []
    for i, m in enumerate(raw_list):
        where = f"maps[{i}]"
        if not isinstance(m, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        x = _num(m, "spawn_x", 0.0, 1.0, where)
        y = _num(m, "spawn_y", 0.0, 1.0, where)
        blocks = _list(m, "blocks", where)
        clean_blocks = []
        for j, b in enumerate(blocks):
            if (not isinstance(b, list) or len(b) != 4
                    or any(isinstance(v, bool) or not isinstance(v, (int, float)) for v in b)):
                raise ContentError(f"{where}.blocks[{j}] ต้องเป็น [x, y, w, h]")
            clean_blocks.append([float(v) for v in b])
        out.append({
            "map_id": _int(m, "map_id", 1, 2 ** 31 - 1, where),
            "name": _str(m, "name", where, 48),
            "subtitle": _str(m, "subtitle", where, 96) if "subtitle" in m else "",
            "spawn_x": x, "spawn_y": y,
            "peace_zone": bool(m.get("peace_zone", True)),
            "blocks": clean_blocks,
            "background": m.get("background") if isinstance(m.get("background"), str) else "school",
        })
    if len({m["map_id"] for m in out}) != len(out):
        raise ContentError("maps: map_id ซ้ำกัน")
    return out


def _gates(raw_list, map_ids):
    out = []
    for i, g in enumerate(raw_list):
        where = f"gates[{i}]"
        if not isinstance(g, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        fm = _int(g, "from_map", 1, 2 ** 31 - 1, where)
        tm = _int(g, "to_map", 1, 2 ** 31 - 1, where)
        if map_ids is not None:
            for v, label in ((fm, "from_map"), (tm, "to_map")):
                if v not in map_ids:
                    raise ContentError(f"{where}.{label} {v} ไม่มีในตาราง maps")
        if fm == tm:
            raise ContentError(f"{where}: from_map กับ to_map ต้องต่างกัน")
        out.append({
            "gate_id": _int(g, "gate_id", 1, 2 ** 31 - 1, where),
            "name": _str(g, "name", where, 48),
            "from_map": fm, "to_map": tm,
            "from_x": _num(g, "from_x", 0.0, 1.0, where),
            "from_y": _num(g, "from_y", 0.0, 1.0, where),
            "to_x": _num(g, "to_x", 0.0, 1.0, where),
            "to_y": _num(g, "to_y", 0.0, 1.0, where),
            "radius": _num(g, "radius", 0.01, 0.5, where),
            "required_level": _int(g, "required_level", 1, 999, where),
            "required_money": _int(g, "required_money", 0, 10 ** 9, where),
        })
    if len({g["gate_id"] for g in out}) != len(out):
        raise ContentError("gates: gate_id ซ้ำกัน")
    return out


def _items(raw_list):
    """ไอเทมเพิ่มเติมที่ content กำหนดเอง (ของเดิมอยู่ใน seed.sql)"""
    out = []
    for i, it in enumerate(raw_list):
        where = f"items[{i}]"
        if not isinstance(it, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        kind = _str(it, "type", where, 24)
        if kind not in ("Consumable", "Equipment", "Quest", "Material"):
            raise ContentError(f"{where}.type ต้องเป็น Consumable/Equipment/Quest/Material")
        slot = it.get("equip_slot")
        if slot is not None and slot not in ("weapon", "armor"):
            raise ContentError(f"{where}.equip_slot ต้องเป็น weapon หรือ armor")
        out.append({
            "item_id": _int(it, "item_id", 1, 2 ** 31 - 1, where),
            "name": _str(it, "name", where, 64),
            "type": kind,
            "price": _int(it, "price", 0, 10 ** 9, where),
            "stackable": 1 if it.get("stackable") else 0,
            "max_stack": _int(it, "max_stack", 1, 9999, where),
            "hp_recovery": _int(it, "hp_recovery", 0, 10 ** 6, where) if "hp_recovery" in it else 0,
            "mp_recovery": _int(it, "mp_recovery", 0, 10 ** 6, where) if "mp_recovery" in it else 0,
            "sp_recovery": _int(it, "sp_recovery", 0, 10 ** 6, where) if "sp_recovery" in it else 0,
            "attack_bonus": _int(it, "attack_bonus", 0, 10 ** 6, where) if "attack_bonus" in it else 0,
            "defense_bonus": _int(it, "defense_bonus", 0, 10 ** 6, where) if "defense_bonus" in it else 0,
            "equip_slot": slot,
            "class": it.get("class") if isinstance(it.get("class"), str) else "all",
            "required_level": _int(it, "required_level", 1, 999, where) if "required_level" in it else 1,
        })
    if len({x["item_id"] for x in out}) != len(out):
        raise ContentError("items: item_id ซ้ำกัน")
    return out


def _shop_items(raw_list, npc_ids, item_ids):
    out = []
    for i, s in enumerate(raw_list):
        where = f"shop_items[{i}]"
        if not isinstance(s, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        npc_id = _int(s, "npc_id", 1, 2 ** 31 - 1, where)
        item_id = _int(s, "item_id", 1, 2 ** 31 - 1, where)
        if npc_ids is not None and npc_id not in npc_ids:
            raise ContentError(f"{where}.npc_id {npc_id} ไม่มีในตาราง npcs")
        if item_ids is not None and item_id not in item_ids:
            raise ContentError(f"{where}.item_id {item_id} ไม่มีในตาราง items")
        out.append({"npc_id": npc_id, "item_id": item_id,
                    "buy_price": _int(s, "buy_price", 0, 10 ** 9, where)})
    seen = set()
    for s in out:
        key = (s["npc_id"], s["item_id"])
        if key in seen:
            raise ContentError(f"shop_items: ร้าน {s['npc_id']} ขายไอเทม {s['item_id']} ซ้ำ")
        seen.add(key)
    return out


def _quests(raw_list, monster_ids, npc_ids):
    out = []
    for i, q in enumerate(raw_list):
        where = f"quests[{i}]"
        if not isinstance(q, dict):
            raise ContentError(f"{where} ต้องเป็นออบเจกต์")
        target = _int(q, "target_monster", 1, 2 ** 31 - 1, where)
        if monster_ids is not None and target not in monster_ids:
            raise ContentError(f"{where}.target_monster {target} ไม่มีในตาราง monsters")
        npc_id = _int(q, "npc_id", 1, 2 ** 31 - 1, where) if "npc_id" in q else None
        if npc_id is not None and npc_ids is not None and npc_id not in npc_ids:
            raise ContentError(f"{where}.npc_id {npc_id} ไม่มีในตาราง npcs")
        text = _list(q, "text", where)
        for j, line in enumerate(text):
            if not isinstance(line, str) or not line or len(line) > 200:
                raise ContentError(f"{where}.text[{j}] ต้องเป็นข้อความ 1-200 ตัวอักษร")
        out.append({
            "quest_id": _int(q, "quest_id", 1, 2 ** 31 - 1, where),
            "name": _str(q, "name", where, 64),
            "npc_id": npc_id,
            "target_monster": target,
            "required_count": _int(q, "required_count", 1, 9999, where),
            "reward_exp": _int(q, "reward_exp", 0, 10 ** 9, where),
            "reward_money": _int(q, "reward_money", 0, 10 ** 9, where),
            "text": text,
        })
    if len({q["quest_id"] for q in out}) != len(out):
        raise ContentError("quests: quest_id ซ้ำกัน")
    return out


def _chat(raw):
    where = "chat"
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ContentError("chat ต้องเป็นออบเจกต์")
    return {
        "party_prefix": raw.get("party_prefix", "#") if isinstance(raw.get("party_prefix", "#"), str) else "#",
        "whisper_prefix": raw.get("whisper_prefix", "@") if isinstance(raw.get("whisper_prefix", "@"), str) else "@",
        "max_length": _int(raw, "max_length", 1, 500, where) if "max_length" in raw else 120,
        "history_limit": _int(raw, "history_limit", 1, 200, where) if "history_limit" in raw else 30,
        "rate_limit_ms": _int(raw, "rate_limit_ms", 0, 60_000, where) if "rate_limit_ms" in raw else 800,
    }


def _party(raw):
    where = "party"
    if raw is None:
        raw = {}
    if not isinstance(raw, dict):
        raise ContentError("party ต้องเป็นออบเจกต์")
    return {
        "max_members": _int(raw, "max_members", 2, 8, where) if "max_members" in raw else 4,
        "invite_ttl_ms": _int(raw, "invite_ttl_ms", 1000, 3_600_000, where) if "invite_ttl_ms" in raw else 60_000,
        "exp_share_radius": _num(raw, "exp_share_radius", 0.0, 1.0, where) if "exp_share_radius" in raw else 0.4,
        "exp_share_needs_same_map": bool(raw.get("exp_share_needs_same_map", True)),
    }


class Content:
    """ชุดข้อมูลเกมที่โหลดและตรวจแล้ว อ่านได้แบบ attribute (content.combat["attack_range"])"""

    def __init__(self, raw, item_ids=None):
        if not isinstance(raw, dict):
            raise ContentError("ไฟล์ข้อมูลต้องเป็นออบเจกต์ JSON")
        self.version = _int(raw, "version", 1, 2 ** 31 - 1, "content")
        self.combat = _combat(raw.get("combat"))
        self.level_up = _level_up(raw.get("level_up"))
        # ไอเทมที่ content กำหนดเองต้องถูกตรวจรู้ก่อนของดรอป/ร้านค้า (ของดรอปอาจอ้างไอเทมใหม่)
        self.items = _items(_list(raw, "items", "content")) if "items" in raw else []
        extra_item_ids = {i["item_id"] for i in self.items}
        # item_ids=None แปลว่า "ยังไม่รู้รายการไอเทมในฐานข้อมูล" -> ไม่ตรวจการอ้างถึงไอเทม
        all_item_ids = None
        if item_ids is not None:
            all_item_ids = set(item_ids) | extra_item_ids
        self.monsters = _monsters(_list(raw, "monsters", "content"), all_item_ids)
        self.npcs = _npcs(_list(raw, "npcs", "content"))
        self.skills = _skills(_list(raw, "skills", "content"))
        self.skills_by_id = {s["skill_id"]: s for s in self.skills}

        # --- โดเมนที่เพิ่มในรอบ 4 (ทั้งหมด optional เพื่อให้ไฟล์ข้อมูลชุดเก่ายังโหลดได้) ---
        self.maps = _maps(_list(raw, "maps", "content")) if "maps" in raw else []
        map_ids = {m["map_id"] for m in self.maps} or None
        self.gates = _gates(_list(raw, "gates", "content"), map_ids) if "gates" in raw else []
        self.shop_items = (_shop_items(_list(raw, "shop_items", "content"),
                                       {n["npc_id"] for n in self.npcs}, all_item_ids)
                           if "shop_items" in raw else [])
        self.quests = (_quests(_list(raw, "quests", "content"),
                               {m["monster_id"] for m in self.monsters},
                               {n["npc_id"] for n in self.npcs})
                       if "quests" in raw else [])
        self.chat = _chat(raw.get("chat"))
        self.party = _party(raw.get("party"))
        self.items_by_id = {i["item_id"]: i for i in self.items}
        self.maps_by_id = {m["map_id"]: m for m in self.maps}

    def skill(self, skill_id):
        return self.skills_by_id.get(skill_id)

    def monsters_on(self, map_id):
        return [m for m in self.monsters if m["map_id"] == map_id]

    def npcs_on(self, map_id):
        return [n for n in self.npcs if n["map_id"] == map_id]

    def gates_from(self, map_id):
        return [g for g in self.gates if g["from_map"] == map_id]

    def shop_for(self, npc_id):
        return [s for s in self.shop_items if s["npc_id"] == npc_id]

    def quests_for_npc(self, npc_id):
        return [q for q in self.quests if q["npc_id"] == npc_id]



def load(path=DEFAULT_PATH, item_ids=None):
    """อ่านและตรวจไฟล์ข้อมูล คืน Content (โยน ContentError ถ้าผิดรูปแบบ)

    item_ids: ถ้าให้มา ของดรอปที่อ้างไอเทมที่ไม่มีในฐานข้อมูลจะถูกปฏิเสธตั้งแต่เริ่มเซิร์ฟเวอร์
    """
    p = Path(path)
    try:
        raw = json.loads(p.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise ContentError(f"ไม่พบไฟล์ข้อมูลเกม: {p}")
    except json.JSONDecodeError as e:
        raise ContentError(f"{p.name} อ่าน JSON ไม่ได้ (บรรทัด {e.lineno} คอลัมน์ {e.colno}): {e.msg}")
    return Content(raw, item_ids=item_ids)
