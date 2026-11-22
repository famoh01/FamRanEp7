# ran_mobile/Server/ranserver/game.py
# กติกาเกมทั้งหมดอยู่ฝั่งเซิร์ฟเวอร์ (สูตรค่าสถานะ, ดาเมจ, EXP) ไม่มีการพึ่ง Client
# โมดูลนี้ไม่มี I/O จึงทดสอบแยกได้ง่าย

MAX_LEVEL = 99
INVENTORY_SLOTS = 30
MAX_CHARACTERS_PER_ACCOUNT = 3
STARTING_MONEY = 1000

BASIC_ATTACK_BASE = 20          # ดาเมจพื้นฐาน + STR
SKILL_CRIT_CHANCE = 0.10        # ใช้กับทั้งการโจมตีปกติและสกิล
CRIT_CHANCE = 0.10
MISS_CHANCE = 0.05
CRIT_MULTIPLIER = 2

MONSTER_RESPAWN_MS = 30_000
SELL_RATIO_NUMERATOR = 1        # ราคาขาย = ราคาซื้อ * 1 / 2
SELL_RATIO_DENOMINATOR = 2
REVIVE_HP_RATIO = 0.5
MAX_QUANTITY_PER_ACTION = 999

CLASS_START_STATS = {
    # class: (str, dex, int, con)
    "fighter": (14, 10, 10, 12),
    "mage": (10, 10, 14, 10),
    "archer": (10, 14, 10, 12),
}

STARTER_ITEMS = [
    # (item_id, quantity, slot)
    (1001, 3, 0),   # Small HP Potion
    (1002, 3, 1),   # Small MP Potion
    (1003, 2, 2),   # Stamina Biscuit
]

STARTER_SKILL_IDS = [1, 2]


def max_hp(level: int, con: int) -> int:
    return 100 + 20 * (level - 1) + 5 * con


def max_mp(level: int, intel: int) -> int:
    return 50 + 10 * (level - 1) + 2 * intel


def max_sp(level: int) -> int:
    return 100 + 10 * (level - 1)


def exp_required(level: int) -> int:
    return level * 100


def apply_exp(level: int, exp: int, gain: int):
    """คืนค่า (level, exp, levels_gained) หลังได้รับ EXP"""
    if gain < 0:
        raise ValueError("exp gain must not be negative")
    exp += gain
    gained = 0
    while level < MAX_LEVEL and exp >= exp_required(level):
        exp -= exp_required(level)
        level += 1
        gained += 1
    if level >= MAX_LEVEL:
        exp = 0
    return level, exp, gained


def roll_hit(raw_damage: int, defense: int, rng):
    """คืนค่า (damage, crit, missed). ลำดับการสุ่ม: miss -> crit"""
    if rng.random() < MISS_CHANCE:
        return 0, False, True
    damage = max(1, raw_damage - defense)
    crit = rng.random() < CRIT_CHANCE
    if crit:
        damage *= CRIT_MULTIPLIER
    return damage, crit, False


def sell_price(price: int, quantity: int) -> int:
    return (price * SELL_RATIO_NUMERATOR // SELL_RATIO_DENOMINATOR) * quantity
