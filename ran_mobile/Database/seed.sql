-- ran_mobile/Database/seed.sql
-- ข้อมูลเริ่มต้นสำหรับทดสอบ (ไอเทม สกิล มอนสเตอร์ เควสต์)
-- ชื่อและค่าทั้งหมดเป็นของโปรเจกต์นี้เอง ไม่ได้คัดลอกมาจากเกมอื่น
-- รันซ้ำได้: ใช้ INSERT OR IGNORE

INSERT OR IGNORE INTO items (item_id, name, type, price, stackable, max_stack, hp_recovery, mp_recovery, sp_recovery) VALUES
    (1001, 'Small HP Potion', 'Consumable', 20, 1, 99, 100, 0, 0),
    (1002, 'Small MP Potion', 'Consumable', 25, 1, 99, 0, 60, 0),
    (1003, 'Stamina Biscuit', 'Consumable', 15, 1, 99, 0, 0, 50),
    (2001, 'Vulgarian Fang', 'Quest', 0, 1, 99, 0, 0, 0),
    (3001, 'Iron Ore', 'Material', 5, 1, 999, 0, 0, 0);

-- อุปกรณ์ (แอตทริบิวต์ attack_bonus / defense_bonus ใช้จริงในการคำนวณดาเมจ)
INSERT OR IGNORE INTO items (item_id, name, type, price, stackable, max_stack, hp_recovery, mp_recovery, sp_recovery, attack_bonus, defense_bonus, equip_slot) VALUES
    (4001, 'Iron Sword', 'Equipment', 120, 0, 1, 0, 0, 0, 6, 0, 'weapon'),
    (4002, 'Leather Armor', 'Equipment', 90, 0, 1, 0, 0, 0, 0, 3, 'armor');

INSERT OR IGNORE INTO skills (skill_id, name, damage, mana_cost, cooldown_ms, range) VALUES
    (1, 'Power Strike', 35, 10, 3000, 3.0),
    (2, 'Fire Bolt', 50, 20, 5000, 8.0),
    (3, 'Quick Step', 20, 5, 1500, 2.0),
    (4, 'Frost Nova', 60, 35, 9000, 5.0);

INSERT OR IGNORE INTO monsters (monster_id, name, max_hp, hp, defense, attack, exp_reward, money_reward, respawn_at_ms) VALUES
    (1, 'Little Vulgarian', 120, 120, 2, 6, 50, 30, 0),
    (2, 'Big Vulgarian', 300, 300, 6, 14, 120, 80, 0),
    (3, 'Training Dummy', 80, 80, 0, 0, 20, 10, 0),
    (4, 'Dorm Guard', 400, 400, 10, 18, 200, 150, 0),
    (5, 'Rooftop Crow', 60, 60, 1, 4, 30, 15, 0);

INSERT OR IGNORE INTO quests (quest_id, name, target_monster, required_count, reward_exp, reward_money) VALUES
    (1, 'Hunt 5 Little Vulgarians', 1, 5, 200, 100),
    (2, 'Hit the Training Dummy 3 times', 3, 3, 50, 20),
    (3, 'Hunt 2 Rooftop Crows', 5, 2, 90, 40),
    (4, 'Hunt 2 Dorm Guards', 4, 2, 600, 400);

-- แผนที่ทดสอบ (สร้างเองสำหรับทดสอบการเดินและการตรวจของ Server ไม่ใช่ข้อมูลจากเกม EP7)
-- blocks: [x, y, w, h] เป็นสัดส่วนของแผนที่
INSERT OR IGNORE INTO maps (map_id, name, spawn_x, spawn_y, blocks_json) VALUES
    (1, 'แผนทดสอบ (สร้างเอง)', 0.5, 0.5, '[[0.20,0.22,0.10,0.18],[0.70,0.55,0.12,0.16],[0.42,0.12,0.16,0.06]]');

-- ตำแหน่งมอนสเตอร์บนแผนทดสอบ (ตั้งค่าซ้ำได้ทุกครั้งที่เริ่มเซิร์ฟเวอร์)
UPDATE monsters SET map_id = 1, pos_x = 0.15, pos_y = 0.75 WHERE monster_id = 1;
UPDATE monsters SET map_id = 1, pos_x = 0.85, pos_y = 0.25 WHERE monster_id = 2;
UPDATE monsters SET map_id = 1, pos_x = 0.35, pos_y = 0.60 WHERE monster_id = 3;
UPDATE monsters SET map_id = 1, pos_x = 0.60, pos_y = 0.85 WHERE monster_id = 4;
UPDATE monsters SET map_id = 1, pos_x = 0.90, pos_y = 0.85 WHERE monster_id = 5;
