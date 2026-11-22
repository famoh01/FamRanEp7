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
        out.append({
            "npc_id": _int(n, "npc_id", 1, 2 ** 31 - 1, where), "name": _str(n, "name", where),
            "role": _str(n, "role", where, 24), "map_id": _int(n, "map_id", 1, 2 ** 31 - 1, where),
            "pos_x": x, "pos_y": y, "dialog": dialog,
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


class Content:
    """ชุดข้อมูลเกมที่โหลดและตรวจแล้ว อ่านได้แบบ attribute (content.combat["attack_range"])"""

    def __init__(self, raw, item_ids=None):
        if not isinstance(raw, dict):
            raise ContentError("ไฟล์ข้อมูลต้องเป็นออบเจกต์ JSON")
        self.version = _int(raw, "version", 1, 2 ** 31 - 1, "content")
        self.combat = _combat(raw.get("combat"))
        self.level_up = _level_up(raw.get("level_up"))
        self.monsters = _monsters(_list(raw, "monsters", "content"), item_ids)
        self.npcs = _npcs(_list(raw, "npcs", "content"))
        self.skills = _skills(_list(raw, "skills", "content"))
        self.skills_by_id = {s["skill_id"]: s for s in self.skills}

    def skill(self, skill_id):
        return self.skills_by_id.get(skill_id)

    def monsters_on(self, map_id):
        return [m for m in self.monsters if m["map_id"] == map_id]


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
