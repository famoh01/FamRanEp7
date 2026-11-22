# ran_mobile/Server/ranserver/migrations.py
# การย้ายสคีมาแบบมีเวอร์ชัน เก็บเวอร์ชันปัจจุบันใน PRAGMA user_version
# ทุก migration มีคู่ down เพื่อย้อนกลับได้ (DROP COLUMN ต้องใช้ SQLite 3.35 ขึ้นไป)
# ฐานข้อมูลเก่าที่ยังไม่มีเวอร์ชัน (user_version = 0) จะถูกอัปเกรดตาม migration โดยไม่ลบข้อมูล


def _v1_up(conn):
    # ตำแหน่งและแผนที่ของตัวละคร (บันทึกฝั่ง Server เท่านั้น)
    conn.execute("ALTER TABLE characters ADD COLUMN map_id INTEGER NOT NULL DEFAULT 1")
    conn.execute("ALTER TABLE characters ADD COLUMN pos_x REAL NOT NULL DEFAULT 0.5 CHECK (pos_x BETWEEN 0 AND 1)")
    conn.execute("ALTER TABLE characters ADD COLUMN pos_y REAL NOT NULL DEFAULT 0.5 CHECK (pos_y BETWEEN 0 AND 1)")
    conn.execute("ALTER TABLE characters ADD COLUMN pos_updated_ms INTEGER NOT NULL DEFAULT 0")
    # แผนที่: spawn และสิ่งกีดขวางเป็นสัดส่วน 0..1, blocks_json = [[x, y, w, h], ...]
    conn.execute(
        "CREATE TABLE IF NOT EXISTS maps ("
        " map_id      INTEGER PRIMARY KEY,"
        " name        TEXT    NOT NULL,"
        " spawn_x     REAL    NOT NULL CHECK (spawn_x BETWEEN 0 AND 1),"
        " spawn_y     REAL    NOT NULL CHECK (spawn_y BETWEEN 0 AND 1),"
        " blocks_json TEXT    NOT NULL)"
    )
    # ตัวละครที่อยู่ในโลกเกมตอนนี้ (ลบเมื่อออกจากโลก/ออกจากระบบ/ลบตัวละคร)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS world_sessions ("
        " character_id INTEGER PRIMARY KEY REFERENCES characters(id) ON DELETE CASCADE,"
        " map_id       INTEGER NOT NULL,"
        " entered_ms   INTEGER NOT NULL,"
        " last_move_ms INTEGER NOT NULL)"
    )
    # มอนสเตอร์มีตำแหน่งบนแผนที่ (ค่าเริ่มต้นตรงกับ seed.sql)
    conn.execute("ALTER TABLE monsters ADD COLUMN map_id INTEGER NOT NULL DEFAULT 1")
    conn.execute("ALTER TABLE monsters ADD COLUMN pos_x REAL NOT NULL DEFAULT 0.5 CHECK (pos_x BETWEEN 0 AND 1)")
    conn.execute("ALTER TABLE monsters ADD COLUMN pos_y REAL NOT NULL DEFAULT 0.5 CHECK (pos_y BETWEEN 0 AND 1)")


def _v1_down(conn):
    conn.execute("DROP TABLE IF EXISTS world_sessions")
    conn.execute("DROP TABLE IF EXISTS maps")
    for col in ("pos_y", "pos_x", "map_id"):
        conn.execute(f"ALTER TABLE monsters DROP COLUMN {col}")
    for col in ("pos_updated_ms", "pos_y", "pos_x", "map_id"):
        conn.execute(f"ALTER TABLE characters DROP COLUMN {col}")


def _v2_up(conn):
    # last_seen_ms: เวลาที่ Client ติดต่อล่าสุด (ใช้หมดอายุ session ที่ไม่มีการติดต่อ)
    # move_budget: ถังระยะเดินสะสม (token bucket) ค่าเริ่มต้นเท่ากับ world.BUDGET_CAP
    conn.execute("ALTER TABLE world_sessions ADD COLUMN last_seen_ms INTEGER NOT NULL DEFAULT 0")
    conn.execute("ALTER TABLE world_sessions ADD COLUMN move_budget REAL NOT NULL DEFAULT 0.48 "
                 "CHECK (move_budget >= 0)")
    conn.execute("UPDATE world_sessions SET last_seen_ms = last_move_ms")


def _v2_down(conn):
    conn.execute("ALTER TABLE world_sessions DROP COLUMN move_budget")
    conn.execute("ALTER TABLE world_sessions DROP COLUMN last_seen_ms")


def _v3_up(conn):
    # last_move_req_ms: เวลาของ "คำขอเดิน" ล่าสุดที่ผ่านการจำกัดความถี่ (ทั้งที่ยอมรับและถูกปฏิเสธ)
    # ใช้แยกจาก last_move_ms ซึ่งบันทึกเฉพาะการเดินที่สำเร็จ; last_seen_ms ใช้เฉพาะการหมดอายุ session
    # (ถ้าใช้ last_seen_ms ตรวจความถี่ คำขออ่านสถานะจะทำให้การเดินครั้งถัดไปถูกปฏิเสธโดยไม่ควร)
    conn.execute("ALTER TABLE world_sessions ADD COLUMN last_move_req_ms INTEGER NOT NULL DEFAULT 0")
    conn.execute("UPDATE world_sessions SET last_move_req_ms = last_move_ms")


def _v3_down(conn):
    conn.execute("ALTER TABLE world_sessions DROP COLUMN last_move_req_ms")


def _has_column(conn, table, column):
    return any(r[1] == column for r in conn.execute(f"PRAGMA table_info({table})").fetchall())


def _v4_up(conn):
    # monsters.level: ระดับของมอนสเตอร์ (ใช้แสดงผลและคำนวณรางวัล)
    # (ฐานข้อมูลใหม่ที่สร้างจาก schema.sql มีคอลัมน์นี้แล้ว จึงข้ามได้)
    if not _has_column(conn, "monsters", "level"):
        conn.execute("ALTER TABLE monsters ADD COLUMN level INTEGER NOT NULL DEFAULT 1 "
                     "CHECK (level >= 1)")
    # NPC ในแผนที่: คำพูดเก็บเป็น JSON ที่โปรเจกต์เขียนเอง
    conn.execute(
        "CREATE TABLE IF NOT EXISTS npcs ("
        " id          INTEGER PRIMARY KEY AUTOINCREMENT,"
        " npc_id      INTEGER NOT NULL UNIQUE,"
        " name        TEXT    NOT NULL,"
        " role        TEXT    NOT NULL,"
        " map_id      INTEGER NOT NULL DEFAULT 1,"
        " pos_x       REAL    NOT NULL DEFAULT 0.5 CHECK (pos_x BETWEEN 0 AND 1),"
        " pos_y       REAL    NOT NULL DEFAULT 0.5 CHECK (pos_y BETWEEN 0 AND 1),"
        " dialog_json TEXT    NOT NULL DEFAULT '[]')"
    )


    # characters.last_attack_ms: คุมคูลดาวน์ของการโจมตีพื้นฐาน (0 = ยังไม่เคยตีในเซสชันฐานข้อมูลนี้)
    if not _has_column(conn, "characters", "last_attack_ms"):
        conn.execute("ALTER TABLE characters ADD COLUMN last_attack_ms INTEGER NOT NULL DEFAULT 0")


def _v4_down(conn):
    conn.execute("DROP TABLE IF EXISTS npcs")
    conn.execute("ALTER TABLE characters DROP COLUMN last_attack_ms")
    conn.execute("ALTER TABLE monsters DROP COLUMN level")


# (เวอร์ชัน, คำอธิบาย, up, down) เรียงตามเวอร์ชัน
MIGRATIONS = [
    (1, "world: map, character position, world sessions, monster positions", _v1_up, _v1_down),
    (2, "world sessions: last_seen_ms (idle expiry) and move_budget (token bucket)", _v2_up, _v2_down),
    (3, "world sessions: last_move_req_ms (minimum interval between move requests)", _v3_up, _v3_down),
    (4, "combat: characters.last_attack_ms; monsters.level; npcs table with self-written dialog", _v4_up, _v4_down),
]
LATEST = MIGRATIONS[-1][0]


def current_version(conn):
    return conn.execute("PRAGMA user_version").fetchone()[0]


def migrate(conn, target=LATEST):
    """อัปเกรดหรือย้อนกลับไปยัง target แต่ละขั้นทำในธุรกรรมของตัวเอง ล้มเหลวแล้ว rollback ทั้งขั้น"""
    version = current_version(conn)
    for ver, _, up, _ in MIGRATIONS:
        if version < ver <= target:
            _apply(conn, up, ver)
    for ver, _, _, down in reversed(MIGRATIONS):
        if target < ver <= version:
            _apply(conn, down, ver - 1)
    return current_version(conn)


def _apply(conn, fn, new_version):
    conn.execute("BEGIN IMMEDIATE")
    try:
        fn(conn)
        conn.execute(f"PRAGMA user_version = {int(new_version)}")
        conn.execute("COMMIT")
    except Exception:
        conn.execute("ROLLBACK")
        raise
