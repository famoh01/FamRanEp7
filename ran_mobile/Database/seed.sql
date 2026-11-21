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

INSERT OR IGNORE INTO skills (skill_id, name, damage, mana_cost, cooldown_ms, range) VALUES
    (1, 'Power Strike', 35, 10, 3000, 3.0),
    (2, 'Fire Bolt', 50, 20, 5000, 8.0);

INSERT OR IGNORE INTO monsters (monster_id, name, max_hp, hp, defense, attack, exp_reward, money_reward, respawn_at_ms) VALUES
    (1, 'Little Vulgarian', 120, 120, 2, 6, 50, 30, 0),
    (2, 'Big Vulgarian', 300, 300, 6, 14, 120, 80, 0);

INSERT OR IGNORE INTO quests (quest_id, name, target_monster, required_count, reward_exp, reward_money) VALUES
    (1, 'Hunt 5 Little Vulgarians', 1, 5, 200, 100);
