# ran_mobile/Server/ranserver/db.py
# เปิดฐานข้อมูล SQLite และสร้างตาราง/ข้อมูลเริ่มต้น
import sqlite3
from pathlib import Path

# ran_mobile/Database/
DB_DIR = Path(__file__).resolve().parents[2] / "Database"
SCHEMA_PATH = DB_DIR / "schema.sql"
SEED_PATH = DB_DIR / "seed.sql"


def connect(path: str = ":memory:") -> sqlite3.Connection:
    # isolation_level=None: เราควบคุม transaction เอง (BEGIN IMMEDIATE ใน api.py)
    conn = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
    conn.execute("PRAGMA foreign_keys = ON")
    conn.execute("PRAGMA journal_mode = WAL" if path != ":memory:" else "PRAGMA journal_mode = MEMORY")
    return conn


class _ConnProxy:
    """ห่อ sqlite3.Connection ให้ทุกเมธอด (execute/executescript/commit/close ...) ถือ Api.lock ให้เอง

    ตัวล็อกจึงไม่ต้องครอบทั้งคำขอ: งานที่อ่าน/เขียนฐานข้อมูลไม่ทับกันเสมอ ส่วนงานที่ไม่แตะฐานข้อมูล
    (แปลง JSON ตรวจรูปแบบ เขียน log คำนวณ scrypt) ทำนอกล็อกได้จริง
    """

    def __init__(self, conn, lock):
        self._conn = conn
        self._lock = lock

    def execute(self, *args, **kwargs):
        with self._lock:
            return self._conn.execute(*args, **kwargs)

    def executescript(self, *args, **kwargs):
        with self._lock:
            return self._conn.executescript(*args, **kwargs)

    def __getattr__(self, name):
        attr = getattr(self._conn, name)
        if not callable(attr):
            return attr
        lock = self._lock

        def guarded(*args, **kwargs):
            with lock:
                return attr(*args, **kwargs)

        return guarded


def init_db(conn: sqlite3.Connection, content_path=None) -> None:
    """สร้างตาราง -> migration -> seed -> โหลดชุดข้อมูลเกม (content.json) ลงฐานข้อมูล

    content_path: ไฟล์ข้อมูลเกมอื่น (ใช้ในเทสต์) ค่าเริ่มต้นคือ Database/content/content.json
    """
    from .content import DEFAULT_PATH, load
    from . import content as content_module
    from .migrations import migrate

    conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
    migrate(conn)
    conn.executescript(SEED_PATH.read_text(encoding="utf-8"))
    # seed.sql ใส่ไอเทมพื้นฐานไว้แล้ว; ไอเทมที่ content กำหนดจะถูกนับรวมตอนตรวจด้วย
    item_ids = {r[0] for r in conn.execute("SELECT item_id FROM items").fetchall()}
    data = load(content_path or DEFAULT_PATH, item_ids=item_ids)
    sync(conn, data)
    sync_world(conn, data)
    return None


def load_content(conn, content_path=None):
    """โหลดไฟล์ข้อมูลเกมและคืน Content (ใช้โดย __main__ เพื่อส่งให้ Api)"""
    from .content import DEFAULT_PATH, load
    item_ids = {r[0] for r in conn.execute("SELECT item_id FROM items").fetchall()}
    return load(content_path or DEFAULT_PATH, item_ids=item_ids)


def sync_content(conn, data) -> None:
    """เขียนข้อมูลเกมลงฐานข้อมูลซ้ำ (idempotent) — เรียกได้ทุกครั้งที่รีสตาร์ท"""
    sync(conn, data)


def sync(conn: sqlite3.Connection, data) -> None:
    """เขียนข้อมูลจากไฟล์ content.json ลงฐานข้อมูล (idempotent: รันซ้ำได้ทุกครั้งที่เริ่มเซิร์ฟเวอร์)

    ไฟล์ข้อมูลเป็นแหล่งความจริงของค่าสมดุล: แก้ไฟล์แล้วรีสตาร์ทเซิร์ฟเวอร์ ค่าใหม่มีผลทันที
    """
    # ไอเทมที่ content กำหนดต้องอยู่ในตาราง items ก่อน (monster_drops/shop_items อ้างถึง)
    for it in getattr(data, "items", []):
        conn.execute(
            "INSERT INTO items (item_id, name, type, price, stackable, max_stack,"
            " hp_recovery, mp_recovery, sp_recovery, attack_bonus, defense_bonus, equip_slot)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(item_id) DO UPDATE SET name = excluded.name, type = excluded.type,"
            " price = excluded.price, max_stack = excluded.max_stack,"
            " hp_recovery = excluded.hp_recovery, mp_recovery = excluded.mp_recovery,"
            " sp_recovery = excluded.sp_recovery, attack_bonus = excluded.attack_bonus,"
            " defense_bonus = excluded.defense_bonus, equip_slot = excluded.equip_slot",
            (it["item_id"], it["name"], it["type"], it["price"], it["stackable"],
             it["max_stack"], it["hp_recovery"], it["mp_recovery"], it["sp_recovery"],
             it["attack_bonus"], it["defense_bonus"], it["equip_slot"]),
        )
    for m in data.monsters:
        conn.execute(
            "INSERT INTO monsters (monster_id, name, level, max_hp, hp, defense, attack, exp_reward,"
            " money_reward, respawn_at_ms, map_id, pos_x, pos_y, home_x, home_y, patrol)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(monster_id) DO UPDATE SET name = excluded.name, level = excluded.level,"
            " max_hp = excluded.max_hp, defense = excluded.defense, attack = excluded.attack,"
            " exp_reward = excluded.exp_reward, money_reward = excluded.money_reward,"
            " map_id = excluded.map_id, pos_x = excluded.pos_x, pos_y = excluded.pos_y,"
            " home_x = excluded.home_x, home_y = excluded.home_y, patrol = excluded.patrol",
            (m["monster_id"], m["name"], m["level"], m["max_hp"], m["max_hp"], m["defense"],
             m["attack"], m["exp_reward"], m["money_reward"], m["map_id"], m["pos_x"], m["pos_y"],
             m["pos_x"], m["pos_y"], m["patrol"]),
        )
        conn.execute("DELETE FROM monster_drops WHERE monster_id = ?", (m["monster_id"],))
        for d in m["drops"]:
            conn.execute(
                "INSERT INTO monster_drops (monster_id, item_id, chance, min_qty, max_qty)"
                " VALUES (?, ?, ?, ?, ?)",
                (m["monster_id"], d["item_id"], d["chance"], d["min"], d["max"]),
            )
    for n in data.npcs:
        import json as _json
        conn.execute(
            "INSERT INTO npcs (npc_id, name, role, map_id, pos_x, pos_y, dialog_json)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(npc_id) DO UPDATE SET name = excluded.name, role = excluded.role,"
            " map_id = excluded.map_id, pos_x = excluded.pos_x, pos_y = excluded.pos_y,"
            " dialog_json = excluded.dialog_json",
            (n["npc_id"], n["name"], n["role"], n["map_id"], n["pos_x"], n["pos_y"],
             _json.dumps(n["dialog"], ensure_ascii=False)),
        )
    for sk in data.skills:
        conn.execute(
            "INSERT INTO skills (skill_id, name, damage, mana_cost, cooldown_ms, range)"
            " VALUES (?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(skill_id) DO UPDATE SET name = excluded.name, damage = excluded.damage,"
            " mana_cost = excluded.mana_cost, cooldown_ms = excluded.cooldown_ms,"
            " range = excluded.range",
            (sk["skill_id"], sk["name"], sk["damage"], sk["mana_cost"] + sk["sp_cost"],
             sk["cooldown_ms"], sk["range"]),
        )


def sync_world(conn: sqlite3.Connection, data) -> None:
    """เขียนโดเมนที่เพิ่มในรอบ 4 (ไอเทม/แผนที่/ประตูวาร์ป/ร้านค้า/เควสต์) ลงฐานข้อมูล

    เรียกหลัง sync() เสมอ เพราะ shop_items อ้างไอเทม และ gates อ้างแผนที่
    เป็น idempotent: รันซ้ำได้ทุกครั้งที่เริ่มเซิร์ฟเวอร์
    """
    # 1) แผนที่: ชื่อ/จุดเกิด/สิ่งกีดขวาง/เขตปลอดภัย
    import json as _json
    for m in getattr(data, "maps", []):
        conn.execute(
            "INSERT INTO maps (map_id, name, spawn_x, spawn_y, blocks_json)"
            " VALUES (?, ?, ?, ?, ?)"
            " ON CONFLICT(map_id) DO UPDATE SET name = excluded.name,"
            " spawn_x = excluded.spawn_x, spawn_y = excluded.spawn_y,"
            " blocks_json = excluded.blocks_json",
            (m["map_id"], m["name"], m["spawn_x"], m["spawn_y"],
             _json.dumps(m["blocks"])),
        )

    # 3) ประตูวาร์ป
    for g in getattr(data, "gates", []):
        conn.execute(
            "INSERT INTO map_gates (gate_id, name, from_map, to_map, from_x, from_y,"
            " to_x, to_y, radius, required_level, required_money)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(gate_id) DO UPDATE SET name = excluded.name,"
            " from_map = excluded.from_map, to_map = excluded.to_map,"
            " from_x = excluded.from_x, from_y = excluded.from_y,"
            " to_x = excluded.to_x, to_y = excluded.to_y, radius = excluded.radius,"
            " required_level = excluded.required_level, required_money = excluded.required_money",
            (g["gate_id"], g["name"], g["from_map"], g["to_map"], g["from_x"], g["from_y"],
             g["to_x"], g["to_y"], g["radius"], g["required_level"], g["required_money"]),
        )

    # 4) ร้านค้าของ NPC + ตั้งชื่อร้าน/อัตรารับซื้อ
    for n in data.npcs:
        conn.execute("UPDATE npcs SET shop_name = ?, sell_rate_percent = ? WHERE npc_id = ?",
                     (n.get("shop_name", ""), n.get("sell_rate_percent", 30), n["npc_id"]))
    for s in getattr(data, "shop_items", []):
        conn.execute(
            "INSERT INTO shop_items (npc_id, item_id, buy_price) VALUES (?, ?, ?)"
            " ON CONFLICT(npc_id, item_id) DO UPDATE SET buy_price = excluded.buy_price",
            (s["npc_id"], s["item_id"], s["buy_price"]),
        )

    # 5) เควสต์ที่ผูกกับ NPC (ของเดิมใน seed.sql ไม่ถูกแตะ)
    for q in getattr(data, "quests", []):
        conn.execute(
            "INSERT INTO quests (quest_id, name, target_monster, required_count,"
            " reward_exp, reward_money) VALUES (?, ?, ?, ?, ?, ?)"
            " ON CONFLICT(quest_id) DO UPDATE SET name = excluded.name,"
            " target_monster = excluded.target_monster,"
            " required_count = excluded.required_count,"
            " reward_exp = excluded.reward_exp, reward_money = excluded.reward_money",
            (q["quest_id"], q["name"], q["target_monster"], q["required_count"],
             q["reward_exp"], q["reward_money"]),
        )

    # 6) ข้อความ UI ไทยจากไฟล์จริงของ Drive (ThaiPack data/gui/game*_th.txt)
    #    ไฟล์นี้สร้างโดย tools/ep7_drive_importer/round4_source_import.py — ไม่มีข้อความที่แต่งขึ้นเอง
    ui_path = Path(__file__).resolve().parents[2] / "Database" / "content" / "ep7" / \
        "ep7_round4_ui_th.json"
    if ui_path.is_file():
        import json as _json2
        ui = _json2.loads(ui_path.read_text(encoding="utf-8"))
        source = ui.get("_source", {}).get("drive", "Google Drive ThaiPack")
        for key, text in ui.get("all", {}).items():
            conn.execute(
                "INSERT INTO ui_strings (key, text, source) VALUES (?, ?, ?)"
                " ON CONFLICT(key) DO UPDATE SET text = excluded.text,"
                " source = excluded.source",
                (key, text, source),
            )
