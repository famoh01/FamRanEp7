# ran_mobile/Server/ranserver/world.py
# กติกาการเดินในโลกเกม ตรวจที่ Server ทั้งหมด (ไม่มี I/O จึงทดสอบแยกได้)
# พิกัดทุกค่าเป็นสัดส่วนของพื้นที่แผนที่ 0.0 ถึง 1.0 (ซ้ายบน = 0,0) ทำให้ Client ทุกขนาดจอใช้ค่าเดียวกันได้
import math

# ความเร็วสูงสุดที่ยอมรับ หน่วย: สัดส่วนของแผนที่ต่อวินาที
# (ฝั่ง Client เดินประมาณ 0.18 และวิ่งประมาณ 0.31 บนจอมือถือแนวนอน จึงตั้งเผื่อไว้ที่ 0.45)
MAX_SPEED = 0.45
# ช่วงเวลาที่นำมาคิดระยะสูงสุดต่อคำขอ 1 ครั้ง (วินาที) กันการหยุดส่งแล้วกระโดดไกลในครั้งเดียว
MAX_DT_S = 1.0
# ค่าผ่อนผันของระยะ (สัดส่วนของแผนที่) สำหรับความคลาดเคลื่อนของเวลาและการปัดเศษ
SLACK = 0.03
# ปริมาณการเดินสะสมสูงสุดที่เก็บไว้ในถัง (token bucket) ของแต่ละ session
BUDGET_CAP = MAX_SPEED * MAX_DT_S + SLACK


def is_coord(v):
    """ตัวเลขจำนวนจริงที่จำกัดอยู่ในช่วง 0..1 (ไม่รับ bool และ NaN/Infinity)"""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        return False
    if math.isnan(v) or math.isinf(v):
        return False
    return 0.0 <= v <= 1.0


def in_block(x, y, blocks):
    """blocks: รายการ [x, y, w, h] ตามสัดส่วนของแผนที่"""
    for bx, by, bw, bh in blocks:
        if bx <= x <= bx + bw and by <= y <= by + bh:
            return True
    return False


def validate_blocks(blocks):
    """คืนรายการข้อผิดพลาดของข้อมูลสิ่งกีดขวาง (ว่าง = ถูกต้อง)

    ใช้ตรวจข้อมูลแผนที่ตอนโหลด เพื่อไม่ให้แผนที่ผิดรูปแบบเข้าไปถึงการเดิน
    """
    errors = []
    if not isinstance(blocks, list):
        return ["blocks ต้องเป็นรายการ"]
    for i, b in enumerate(blocks):
        if not isinstance(b, list) or len(b) != 4 or not all(is_coord(v) for v in b):
            errors.append(f"blocks[{i}] ต้องเป็น [x, y, w, h] ที่อยู่ในช่วง 0..1")
            continue
        x, y, w, h = b
        if w <= 0 or h <= 0:
            errors.append(f"blocks[{i}] ต้องมีความกว้างและความสูงมากกว่า 0")
        elif x + w > 1 or y + h > 1:
            errors.append(f"blocks[{i}] ล้นออกนอกแผนที่")
    return errors


def is_walkable(x, y, blocks):
    """จุดนี้อยู่ในแผนที่และไม่อยู่ในสิ่งกีดขวาง (ใช้ตรวจจุดเกิด)"""
    return is_coord(x) and is_coord(y) and not in_block(x, y, blocks)


def refill(budget, elapsed_ms):
    """เติมถังตามเวลาที่ผ่านไป (เติมได้ไม่เกิน BUDGET_CAP)"""
    dt = max(0.0, elapsed_ms) / 1000.0
    return min(BUDGET_CAP, budget + MAX_SPEED * dt)


def check_move(cur_x, cur_y, new_x, new_y, elapsed_ms, budget, blocks):
    """ตรวจการเดินแบบถังสะสม (token bucket)

    คืน (ok, code, budget_ที่เหลือ) โดย budget_ที่เหลือใช้เฉพาะเมื่อ ok
    ต่างจาก validate_move: คำขอที่ส่งถี่ ๆ จะใช้ระยะสะสมร่วมกัน ไม่สามารถวาร์ปข้ามแผนที่ด้วยคำขอหลายครั้ง
    """
    available = refill(budget, elapsed_ms)
    dist = math.hypot(new_x - cur_x, new_y - cur_y)
    if dist > available + 1e-9:
        return False, "position_rejected", budget
    if in_block(new_x, new_y, blocks):
        return False, "blocked", budget
    return True, None, max(0.0, available - dist)


def validate_move(cur_x, cur_y, new_x, new_y, elapsed_ms, blocks):
    """คืน (True, None) ถ้าย้ายได้, หรือ (False, รหัสข้อผิดพลาด)

    - ระยะที่เดินได้ = ความเร็วสูงสุด * เวลา (จำกัดเวลาไม่เกิน MAX_DT_S) + ค่าผ่อนผัน
    - ห้ามเดินเข้าช่องที่เป็นสิ่งกีดขวาง
    """
    dt = max(0.0, min(elapsed_ms, MAX_DT_S * 1000.0)) / 1000.0
    allowed = MAX_SPEED * dt + SLACK
    dist = math.hypot(new_x - cur_x, new_y - cur_y)
    if dist > allowed:
        return False, "position_rejected"
    if in_block(new_x, new_y, blocks):
        return False, "blocked"
    return True, None


def session_expired(last_seen_ms, now_ms, idle_ms):
    """session ที่ไม่มีการติดต่อเกิน idle_ms หมดอายุ"""
    return now_ms - last_seen_ms > idle_ms
