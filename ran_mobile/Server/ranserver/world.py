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
