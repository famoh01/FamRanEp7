-- ran_mobile/Database/schema.sql
-- ตารางหลักของ RAN MOBILE (ใช้ SQLite สำหรับพัฒนา; SQL เขียนแบบพกพาได้ไป MySQL/PostgreSQL ได้ในภายหลัง)
-- หมายเหตุการเปลี่ยนชื่อคอลัมน์จากสเปก: "str/int/dex/con" -> stat_str/stat_int/stat_dex/stat_con
-- เพราะ "int" เป็นคำสงวนของ SQL

PRAGMA foreign_keys = ON;

-- ---------------------------------------------------------------------------
-- บัญชีผู้ใช้
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS accounts (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    username      TEXT    NOT NULL UNIQUE CHECK (length(username) BETWEEN 3 AND 20),
    password_hash TEXT    NOT NULL,              -- รูปแบบ scrypt$<salt_hex>$<hash_hex> (ห้ามเก็บรหัสผ่านตรง ๆ)
    email         TEXT,
    created_at    TEXT    NOT NULL DEFAULT (datetime('now')),
    last_login    TEXT
);

-- เซสชัน: เก็บเฉพาะ SHA-256 ของ token ไม่เก็บ token จริง
CREATE TABLE IF NOT EXISTS sessions (
    token_hash  TEXT    PRIMARY KEY,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    expires_at  INTEGER NOT NULL                 -- unix epoch seconds
);
CREATE INDEX IF NOT EXISTS idx_sessions_account ON sessions(account_id);

-- ---------------------------------------------------------------------------
-- ตัวละคร
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS characters (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name        TEXT    NOT NULL UNIQUE CHECK (length(name) BETWEEN 2 AND 16),
    gender      TEXT    NOT NULL CHECK (gender IN ('male', 'female')),
    class       TEXT    NOT NULL CHECK (class IN ('fighter', 'mage', 'archer')),
    level       INTEGER NOT NULL DEFAULT 1 CHECK (level >= 1),
    exp         INTEGER NOT NULL DEFAULT 0 CHECK (exp >= 0),
    money       INTEGER NOT NULL DEFAULT 1000 CHECK (money >= 0),
    hp          INTEGER NOT NULL,
    mp          INTEGER NOT NULL,
    sp          INTEGER NOT NULL,
    stat_str    INTEGER NOT NULL DEFAULT 10 CHECK (stat_str BETWEEN 1 AND 999),
    stat_dex    INTEGER NOT NULL DEFAULT 10 CHECK (stat_dex BETWEEN 1 AND 999),
    stat_int    INTEGER NOT NULL DEFAULT 10 CHECK (stat_int BETWEEN 1 AND 999),
    stat_con    INTEGER NOT NULL DEFAULT 10 CHECK (stat_con BETWEEN 1 AND 999),
    last_attack_ms INTEGER NOT NULL DEFAULT 0,     -- เวลาของการโจมตีพื้นฐานล่าสุด (คุมคูลดาวน์)
    created_at  TEXT    NOT NULL DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_characters_account ON characters(account_id);

-- ---------------------------------------------------------------------------
-- ไอเทมและคลังของ
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS items (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    item_id       INTEGER NOT NULL UNIQUE,       -- รหัสไอเทมที่ใช้อ้างอิงในเกม
    name          TEXT    NOT NULL,
    type          TEXT    NOT NULL CHECK (type IN ('Consumable', 'Equipment', 'Quest', 'Material')),
    price         INTEGER NOT NULL CHECK (price >= 0),
    stackable     INTEGER NOT NULL DEFAULT 1 CHECK (stackable IN (0, 1)),
    max_stack     INTEGER NOT NULL DEFAULT 99 CHECK (max_stack >= 1),
    hp_recovery   INTEGER NOT NULL DEFAULT 0 CHECK (hp_recovery >= 0),
    mp_recovery   INTEGER NOT NULL DEFAULT 0 CHECK (mp_recovery >= 0),
    sp_recovery   INTEGER NOT NULL DEFAULT 0 CHECK (sp_recovery >= 0),
    attack_bonus  INTEGER NOT NULL DEFAULT 0 CHECK (attack_bonus >= 0),
    defense_bonus INTEGER NOT NULL DEFAULT 0 CHECK (defense_bonus >= 0),
    equip_slot    TEXT CHECK (equip_slot IS NULL OR equip_slot IN ('weapon', 'armor'))
);

-- คลังของ: 1 แถว = 1 ช่อง (slot) ของตัวละคร
CREATE TABLE IF NOT EXISTS inventory (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id  INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    item_id       INTEGER NOT NULL REFERENCES items(item_id),
    quantity      INTEGER NOT NULL CHECK (quantity >= 1),
    slot          INTEGER NOT NULL CHECK (slot >= 0),
    UNIQUE (character_id, slot)
);

-- ---------------------------------------------------------------------------
-- สกิล
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS skills (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    skill_id    INTEGER NOT NULL UNIQUE,
    name        TEXT    NOT NULL,
    damage      INTEGER NOT NULL CHECK (damage >= 0),
    mana_cost   INTEGER NOT NULL CHECK (mana_cost >= 0),
    cooldown_ms INTEGER NOT NULL CHECK (cooldown_ms >= 0),
    range       REAL    NOT NULL DEFAULT 3.0 CHECK (range > 0)
);

CREATE TABLE IF NOT EXISTS character_skills (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id  INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    skill_id      INTEGER NOT NULL REFERENCES skills(skill_id),
    level         INTEGER NOT NULL DEFAULT 1 CHECK (level >= 1),
    last_used_ms  INTEGER NOT NULL DEFAULT 0,    -- เวลาที่ใช้ล่าสุด (เซิร์ฟเวอร์เป็นผู้เก็บ เพื่อคุม cooldown)
    UNIQUE (character_id, skill_id)
);

-- ---------------------------------------------------------------------------
-- มอนสเตอร์ (ส่วนที่เพิ่มจากสเปก เพื่อให้ระบบ Combat ทำงานได้จริงฝั่งเซิร์ฟเวอร์)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS monsters (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    monster_id    INTEGER NOT NULL UNIQUE,
    name          TEXT    NOT NULL,
    level         INTEGER NOT NULL DEFAULT 1 CHECK (level >= 1),
    max_hp        INTEGER NOT NULL CHECK (max_hp >= 1),
    hp            INTEGER NOT NULL CHECK (hp >= 0),
    defense       INTEGER NOT NULL DEFAULT 0 CHECK (defense >= 0),
    attack        INTEGER NOT NULL DEFAULT 5 CHECK (attack >= 0),   -- ดาเมจที่มอนสเตอร์ตอบโต้
    exp_reward    INTEGER NOT NULL CHECK (exp_reward >= 0),
    money_reward  INTEGER NOT NULL DEFAULT 0 CHECK (money_reward >= 0),
    respawn_at_ms INTEGER NOT NULL DEFAULT 0   -- 0 = พร้อมให้ตี; ค่าอื่น = เวลาที่จะเกิดใหม่
);

-- ---------------------------------------------------------------------------
-- NPC (คำพูด/บทสนทนาเขียนขึ้นเองสำหรับโปรเจกต์นี้ ไม่ได้คัดลอกจากเกมอื่น)
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS npcs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    npc_id      INTEGER NOT NULL UNIQUE,
    name        TEXT    NOT NULL,
    role        TEXT    NOT NULL,               -- 'guide' | 'merchant' | 'trainer'
    map_id      INTEGER NOT NULL DEFAULT 1,
    pos_x       REAL    NOT NULL DEFAULT 0.5 CHECK (pos_x BETWEEN 0 AND 1),
    pos_y       REAL    NOT NULL DEFAULT 0.5 CHECK (pos_y BETWEEN 0 AND 1),
    dialog_json TEXT    NOT NULL DEFAULT '[]'   -- ["บรรทัด 1", "บรรทัด 2"]
);

-- ---------------------------------------------------------------------------
-- เควสต์
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS quests (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    quest_id        INTEGER NOT NULL UNIQUE,
    name            TEXT    NOT NULL,
    target_monster  INTEGER NOT NULL REFERENCES monsters(monster_id),
    required_count  INTEGER NOT NULL CHECK (required_count >= 1),
    reward_exp      INTEGER NOT NULL DEFAULT 0 CHECK (reward_exp >= 0),
    reward_money    INTEGER NOT NULL DEFAULT 0 CHECK (reward_money >= 0)
);

CREATE TABLE IF NOT EXISTS character_quests (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    character_id  INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    quest_id      INTEGER NOT NULL REFERENCES quests(quest_id),
    progress      INTEGER NOT NULL DEFAULT 0 CHECK (progress >= 0),
    status        TEXT    NOT NULL DEFAULT 'accepted' CHECK (status IN ('accepted', 'completed', 'rewarded')),
    UNIQUE (character_id, quest_id)
);

-- อุปกรณ์ที่สวมอยู่: 1 ช่องต่อ 1 ชิ้น (ช่อง weapon / armor)
CREATE TABLE IF NOT EXISTS character_equipment (
    character_id  INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    equip_slot    TEXT    NOT NULL CHECK (equip_slot IN ('weapon', 'armor')),
    item_id       INTEGER NOT NULL REFERENCES items(item_id),
    PRIMARY KEY (character_id, equip_slot)
);

-- ตัวนับการล็อกอินผิดพลาด (กันการเดารหัสผ่าน)
CREATE TABLE IF NOT EXISTS login_failures (
    username        TEXT    PRIMARY KEY,
    failures        INTEGER NOT NULL,
    window_start_s  INTEGER NOT NULL,
    locked_until_s  INTEGER NOT NULL DEFAULT 0
);

-- ---------------------------------------------------------------------------
-- กันคำขอซ้ำ (Idempotency-Key): เก็บผลลัพธ์ของคำขอที่สำเร็จไว้ตอบซ้ำ
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS request_log (
    idem_key     TEXT    NOT NULL,
    account_id   INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    status_code  INTEGER NOT NULL,
    response     TEXT    NOT NULL,
    created_at   INTEGER NOT NULL,               -- unix seconds
    PRIMARY KEY (account_id, idem_key)
);
