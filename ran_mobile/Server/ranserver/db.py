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


def init_db(conn: sqlite3.Connection) -> None:
    # ลำดับ: สร้างตารางพื้นฐาน -> migration (เพิ่มคอลัมน์/ตารางของโลกเกม) -> ข้อมูลเริ่มต้น
    from .migrations import migrate

    conn.executescript(SCHEMA_PATH.read_text(encoding="utf-8"))
    migrate(conn)
    conn.executescript(SEED_PATH.read_text(encoding="utf-8"))
