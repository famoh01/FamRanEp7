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


def _v5_up(conn):
    # สเตตัสถาวรของตัวละคร: เพิ่มค่าจริงลงฐานข้อมูลเมื่อเลเวลอัป (ไม่คำนวณใหม่จากเลเวลทุกครั้ง)
    for col, dflt in (("base_atk", 0), ("base_def", 0)):
        if not _has_column(conn, "characters", col):
            conn.execute(f"ALTER TABLE characters ADD COLUMN {col} INTEGER NOT NULL DEFAULT {dflt}")
    # monsters: จุดเกิด (home) + คูลดาวน์การโจมตี + ทิศ/สถานะของ AI
    for col, decl in (
        ("home_x", "REAL NOT NULL DEFAULT 0.5"),
        ("home_y", "REAL NOT NULL DEFAULT 0.5"),
        ("patrol", "REAL NOT NULL DEFAULT 0.06"),
        ("last_attack_ms", "INTEGER NOT NULL DEFAULT 0"),
        ("target_id", "INTEGER NOT NULL DEFAULT 0"),
        ("state", "TEXT NOT NULL DEFAULT 'idle'"),
        ("last_tick_ms", "INTEGER NOT NULL DEFAULT 0"),
    ):
        if not _has_column(conn, "monsters", col):
            conn.execute(f"ALTER TABLE monsters ADD COLUMN {col} {decl}")
    conn.execute("UPDATE monsters SET home_x = pos_x, home_y = pos_y WHERE home_x = 0.5 AND home_y = 0.5")
    # ของดรอปของมอนสเตอร์ (ของทุกชิ้นมีอยู่ในตาราง items)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS monster_drops ("
        " monster_id INTEGER NOT NULL REFERENCES monsters(monster_id) ON DELETE CASCADE,"
        " item_id    INTEGER NOT NULL REFERENCES items(item_id),"
        " chance     REAL    NOT NULL CHECK (chance BETWEEN 0 AND 1),"
        " min_qty    INTEGER NOT NULL DEFAULT 1 CHECK (min_qty >= 1),"
        " max_qty    INTEGER NOT NULL DEFAULT 1 CHECK (max_qty >= min_qty),"
        " PRIMARY KEY (monster_id, item_id))"
    )
    # ผู้เล่นหลายคนบนแผนที่เดียวกัน: ตำแหน่งล่าสุดของทุกตัวละครที่อยู่ในโลก (อ่านเร็ว)
    conn.execute("CREATE INDEX IF NOT EXISTS idx_world_sessions_map ON world_sessions(map_id)")


def _v5_down(conn):
    conn.execute("DROP INDEX IF EXISTS idx_world_sessions_map")
    conn.execute("DROP TABLE IF EXISTS monster_drops")
    for col in ("last_tick_ms", "state", "target_id", "last_attack_ms", "patrol", "home_y", "home_x"):
        conn.execute(f"ALTER TABLE monsters DROP COLUMN {col}")
    for col in ("base_def", "base_atk"):
        conn.execute(f"ALTER TABLE characters DROP COLUMN {col}")


def _v6_up(conn):
    # ---- ระบบร้านค้า (NPC shop) ----
    # ของที่ร้านแต่ละร้านขาย: ราคาและสต็อกกำหนดจากข้อมูล ไม่ใช่จาก Client
    conn.execute(
        "CREATE TABLE IF NOT EXISTS shop_items ("
        " id         INTEGER PRIMARY KEY AUTOINCREMENT,"
        " npc_id     INTEGER NOT NULL,"
        " item_id    INTEGER NOT NULL REFERENCES items(item_id),"
        " buy_price  INTEGER NOT NULL CHECK (buy_price >= 0),"
        " restock_ms INTEGER NOT NULL DEFAULT 0,"
        " UNIQUE (npc_id, item_id))"
    )
    # ราคาที่ร้านรับซื้อของจากผู้เล่น (สัดส่วนของราคาปกติ, นับเป็นเปอร์เซ็นต์)
    if not _has_column(conn, "npcs", "sell_rate_percent"):
        conn.execute("ALTER TABLE npcs ADD COLUMN sell_rate_percent INTEGER NOT NULL DEFAULT 30")
    if not _has_column(conn, "npcs", "shop_name"):
        conn.execute("ALTER TABLE npcs ADD COLUMN shop_name TEXT NOT NULL DEFAULT ''")

    # ---- ประตูวาร์ป / การย้ายแผนที่ ----
    # gate_id: รหัสประตู (ไม่ซ้ำ), from_map -> to_map พร้อมจุดหมาย (สัดส่วน 0..1)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS map_gates ("
        " gate_id    INTEGER PRIMARY KEY,"
        " name       TEXT    NOT NULL,"
        " from_map   INTEGER NOT NULL,"
        " to_map     INTEGER NOT NULL,"
        " from_x     REAL    NOT NULL CHECK (from_x BETWEEN 0 AND 1),"
        " from_y     REAL    NOT NULL CHECK (from_y BETWEEN 0 AND 1),"
        " to_x       REAL    NOT NULL CHECK (to_x BETWEEN 0 AND 1),"
        " to_y       REAL    NOT NULL CHECK (to_y BETWEEN 0 AND 1),"
        " radius     REAL    NOT NULL DEFAULT 0.05 CHECK (radius > 0),"
        " required_level INTEGER NOT NULL DEFAULT 1,"
        " required_money INTEGER NOT NULL DEFAULT 0)"
    )
    # มอนสเตอร์/ผู้เล่นแยกตามแผนที่: เพิ่มคอลัมน์ map_id ให้ผู้เล่น (มีอยู่แล้วใน monsters)
    if not _has_column(conn, "characters", "pk_mode"):
        # โหมด PK: 0 = ปกติ (ไม่ทำร้ายผู้เล่น), 1 = เปิดโหมดต่อสู้ผู้เล่น
        conn.execute("ALTER TABLE characters ADD COLUMN pk_mode INTEGER NOT NULL DEFAULT 0")
    # ตารางประวัติการย้ายแผนที่ (ไว้ตรวจสอบ/แสดงข้อความ)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS map_moves ("
        " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        " character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        " from_map     INTEGER NOT NULL,"
        " to_map       INTEGER NOT NULL,"
        " at_ms        INTEGER NOT NULL)"
    )

    # ---- แชท ----
    # เก็บข้อความสั้น ๆ ต่อแผนที่ (map) และต่อปาร์ตี้ (party) — เซิร์ฟเวอร์เป็นผู้บันทึก
    conn.execute(
        "CREATE TABLE IF NOT EXISTS chat_messages ("
        " id           INTEGER PRIMARY KEY AUTOINCREMENT,"
        " channel      TEXT    NOT NULL CHECK (channel IN ('map', 'party', 'system')),"
        " scope_id     INTEGER NOT NULL,"   # map_id สำหรับ map, party_id สำหรับ party
        " character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        " name         TEXT    NOT NULL,"
        " body         TEXT    NOT NULL,"
        " at_ms        INTEGER NOT NULL)"
    )
    conn.execute("CREATE INDEX IF NOT EXISTS idx_chat_scope ON chat_messages(channel, scope_id, at_ms)")

    # ---- ปาร์ตี้ ----
    conn.execute(
        "CREATE TABLE IF NOT EXISTS parties ("
        " party_id     INTEGER PRIMARY KEY AUTOINCREMENT,"
        " leader_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        " created_ms   INTEGER NOT NULL)"
    )
    conn.execute(
        "CREATE TABLE IF NOT EXISTS party_members ("
        " party_id     INTEGER NOT NULL REFERENCES parties(party_id) ON DELETE CASCADE,"
        " character_id INTEGER NOT NULL UNIQUE REFERENCES characters(id) ON DELETE CASCADE,"
        " joined_ms    INTEGER NOT NULL,"
        " PRIMARY KEY (party_id, character_id))"
    )
    # คำเชิญที่ยังไม่ตอบรับ (ผู้ถูกเชิญต้องกดรับเอง — ป้องกันการถูกดึงเข้าปาร์ตี้โดยไม่ยินยอม)
    conn.execute(
        "CREATE TABLE IF NOT EXISTS party_invites ("
        " party_id     INTEGER NOT NULL REFERENCES parties(party_id) ON DELETE CASCADE,"
        " character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,"
        " invited_ms   INTEGER NOT NULL,"
        " PRIMARY KEY (party_id, character_id))"
    )


def _v6_down(conn):
    conn.execute("DROP TABLE IF EXISTS party_invites")
    conn.execute("DROP TABLE IF EXISTS party_members")
    conn.execute("DROP TABLE IF EXISTS parties")
    conn.execute("DROP INDEX IF EXISTS idx_chat_scope")
    conn.execute("DROP TABLE IF EXISTS chat_messages")
    conn.execute("DROP TABLE IF EXISTS map_moves")
    conn.execute("ALTER TABLE characters DROP COLUMN pk_mode")
    conn.execute("DROP TABLE IF EXISTS map_gates")
    for col in ("shop_name", "sell_rate_percent"):
        conn.execute(f"ALTER TABLE npcs DROP COLUMN {col}")
    conn.execute("DROP TABLE IF EXISTS shop_items")


# (เวอร์ชัน, คำอธิบาย, up, down) เรียงตามเวอร์ชัน

def _v7_up(conn):
    """ตารางข้อความ UI ที่มาจากไฟล์จริงของ Drive (ThaiPack game*_th.txt) — Client ใช้แสดงข้อความ"""
    conn.execute(
        """
        CREATE TABLE ui_strings (
            key        TEXT PRIMARY KEY,   -- SENTENSE,BUYITEM_NOMONEY | WORD,QUIT_PARTY
            text       TEXT NOT NULL,      -- ข้อความไทยจริงจากไฟล์ Drive
            source     TEXT NOT NULL       -- ชื่อไฟล์ใน Drive ที่ดึงมา
        )
        """
    )


def _v7_down(conn):
    conn.execute("DROP TABLE IF EXISTS ui_strings")


MIGRATIONS = [
    (1, "world: map, character position, world sessions, monster positions", _v1_up, _v1_down),
    (2, "world sessions: last_seen_ms (idle expiry) and move_budget (token bucket)", _v2_up, _v2_down),
    (3, "world sessions: last_move_req_ms (minimum interval between move requests)", _v3_up, _v3_down),
    (4, "combat: characters.last_attack_ms; monsters.level; npcs table with self-written dialog", _v4_up, _v4_down),
    (5, "combat: permanent atk/def, monster home/patrol/AI state, monster_drops", _v5_up, _v5_down),
    (6, "round4: shop_items, map_gates, chat, parties/invites, pk_mode", _v6_up, _v6_down),
    (7, "round4: ui_strings (real Thai UI text from the Drive ThaiPack)", _v7_up, _v7_down),
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
