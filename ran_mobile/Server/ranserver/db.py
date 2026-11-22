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
    item_ids = {r[0] for r in conn.execute("SELECT item_id FROM items").fetchall()}
    data = load(content_path or DEFAULT_PATH, item_ids=item_ids)
    sync(conn, data)
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
