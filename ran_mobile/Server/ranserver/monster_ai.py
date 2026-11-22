# ran_mobile/Server/ranserver/monster_ai.py
# AI มอนสเตอร์แบบง่าย: เดินลาดตระเวนในรัศมีของจุดเกิด, ไล่ผู้เล่นที่เข้ามาในระยะตรวจจับ,
# โจมตีสวนกลับเมื่ออยู่ในระยะ, และกลับจุดเกิดเมื่อผู้เล่นหนีไปแล้ว
# เป็นฟังก์ชันบริสุทธิ์ (ไม่มีการอ่าน/เขียนฐานข้อมูล) จึงทดสอบแยกได้ และผลลัพธ์คงที่เสมอเมื่อให้ rng เดิม
import math

# ค่าเริ่มต้นของ AI (ค่าจริงมาจากไฟล์ข้อมูลเกม content.json ผ่าน ranserver/content.py)
DEFAULT_AI = {
    "aggro_range": 0.16,
    "attack_range": 0.10,
    "chase_speed": 0.10,
    "patrol_radius": 0.06,
}

# ใกล้จุดเกิดเท่านี้ถือว่า "กลับถึงบ้านแล้ว" จึงหยุดเดิน
HOME_EPS = 0.002


def _dist(ax, ay, bx, by):
    return math.hypot(bx - ax, by - ay)


def in_block(x, y, blocks):
    """จุดนี้อยู่ในสิ่งกีดขวางหรือไม่ (รูปแบบเดียวกับ world.in_block)"""
    for bx, by, bw, bh in blocks:
        if bx <= x <= bx + bw and by <= y <= by + bh:
            return True
    return False


def _clamp01(v):
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)


def _step_towards(x, y, tx, ty, max_step):
    """เดินจาก (x, y) ไปทาง (tx, ty) ไม่เกิน max_step คืนจุดใหม่"""
    d = _dist(x, y, tx, ty)
    if d <= 1e-9 or max_step <= 0:
        return x, y
    if d <= max_step:
        return tx, ty
    return x + (tx - x) * max_step / d, y + (ty - y) * max_step / d


def _move_ok(x, y, blocks):
    """เดินได้หรือไม่: อยู่ในแผนที่และไม่อยู่ในสิ่งกีดขวาง"""
    return 0.0 <= x <= 1.0 and 0.0 <= y <= 1.0 and not in_block(x, y, blocks)


def nearest_player(mx, my, players, max_range):
    """ผู้เล่นที่ใกล้ที่สุดภายในรัศมีตรวจจับ (players = [(id, x, y), ...]) หรือ None"""
    best = None
    best_d = None
    for pid, px, py in players:
        d = _dist(mx, my, px, py)
        if d <= max_range and (best_d is None or d < best_d):
            best, best_d = (pid, px, py), d
    return best


def step(monster, players, dt_ms, blocks, rng, ai=None, home=None):
    """ก้าวของมอนสเตอร์หนึ่งครั้ง

    monster: dict ที่มี pos_x, pos_y (และอาจมี home_x, home_y)
    players: [(id, x, y), ...] ผู้เล่นที่อยู่ในโลกและอยู่แผนที่เดียวกัน
    dt_ms:  เวลาที่ผ่านไปตั้งแต่การก้าวครั้งก่อน
    คืน dict: x, y (ตำแหน่งใหม่), state ("idle"|"patrol"|"chase"|"attack"|"return"),
              target_id (ผู้เล่นที่กำลังไล่/ตี หรือ None), attack (True เมื่อถึงจังหวะโจมตี)
    """
    cfg = dict(DEFAULT_AI)
    if ai:
        cfg.update(ai)
    hx = monster.get("home_x", monster["pos_x"])
    hy = monster.get("home_y", monster["pos_y"])
    x, y = monster["pos_x"], monster["pos_y"]
    dt_s = max(0.0, dt_ms) / 1000.0
    # กันการกระโดดไกลเมื่อ Server หยุดไปนาน (เช่น เครื่องหลับ): ก้าวได้ไม่เกิน 0.25 วินาทีต่อครั้ง
    dt_s = min(dt_s, 0.25)

    target = nearest_player(x, y, players, cfg["aggro_range"])
    if target is not None:
        _pid, px, py = target
        d = _dist(x, y, px, py)
        if d <= cfg["attack_range"]:
            return {"x": x, "y": y, "state": "attack", "target_id": _pid, "attack": True}
        nx, ny = _step_towards(x, y, px, py, cfg["chase_speed"] * dt_s)
        if _move_ok(nx, ny, blocks):
            return {"x": nx, "y": ny, "state": "chase", "target_id": _pid, "attack": False}
        # ติดสิ่งกีดขวาง: หยุดอยู่กับที่ แต่ยังจ้องเป้าหมายอยู่
        return {"x": x, "y": y, "state": "chase", "target_id": _pid, "attack": False}

    home_d = _dist(x, y, hx, hy)
    if home_d > HOME_EPS:
        # ไม่มีเป้าหมายแล้ว: กลับจุดเกิด
        nx, ny = _step_towards(x, y, hx, hy, cfg["chase_speed"] * dt_s)
        if not _move_ok(nx, ny, blocks):
            nx, ny = hx, hy          # ทางกลับถูกปิด: กลับจุดเกิดตรง ๆ (จุดเกิดถูกตรวจว่าเดินได้แล้ว)
        return {"x": nx, "y": ny, "state": "return", "target_id": None, "attack": False}

    radius = min(monster.get("patrol", cfg["patrol_radius"]), cfg["patrol_radius"])
    if radius <= 0.0 or dt_ms <= 0:
        return {"x": x, "y": y, "state": "idle", "target_id": None, "attack": False}
    # ลาดตระเวน: สุ่มทิศและเดินช้า ๆ ภายในรัศมีของจุดเกิด
    angle = rng.random() * 2.0 * math.pi
    reach = rng.random() * radius
    nx = _clamp01(hx + math.cos(angle) * reach)
    ny = _clamp01(hy + math.sin(angle) * reach)
    speed = cfg["chase_speed"] * 0.45 * dt_s
    nx, ny = _step_towards(x, y, nx, ny, speed)
    if not _move_ok(nx, ny, blocks):
        nx, ny = x, y
    moved = _dist(x, y, nx, ny) > 1e-6
    return {"x": nx, "y": ny, "state": "patrol" if moved else "idle", "target_id": None, "attack": False}


def hit_player(monster_attack, player_defense, rng, crit_chance=0.1, crit_mult=2, miss_chance=0.05):
    """มอนสเตอร์โจมตีผู้เล่น คืน (damage, crit, missed)"""
    if rng.random() < miss_chance:
        return 0, False, True
    damage = max(1, int(monster_attack) - int(player_defense))
    crit = rng.random() < crit_chance
    if crit:
        damage *= crit_mult
    return damage, crit, False


def roll_drops(drops, rng):
    """สุ่มของดรอปจากรายการ {item_id, chance, min, max} คืน [(item_id, quantity), ...]"""
    out = []
    for d in drops:
        if rng.random() < float(d["chance"]):
            lo, hi = int(d["min"]), int(d["max"])
            qty = lo if hi <= lo else lo + int(rng.random() * (hi - lo + 1))
            out.append((int(d["item_id"]), max(1, qty)))
    return out
