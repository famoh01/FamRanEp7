# ran_mobile/Server/ranserver/social.py
# กติกาของระบบรอบ 4: แผนที่/ประตูวาร์ป, ร้านค้า, แชท และปาร์ตี้
# โมดูลนี้ไม่มี I/O (ทดสอบแยกได้) — api.py เป็นผู้เรียกและบันทึกลงฐานข้อมูล
import math

# ---- การย้ายแผนที่ (warp gate) ----
GATE_TRIGGER_RADIUS = 0.06      # ระยะที่ถือว่า "ยืนอยู่บนประตู" (สัดส่วนแผนที่)
GATE_COOLDOWN_MS = 1500         # กันการวาร์ปซ้ำรัว ๆ


def gate_in_range(px, py, gx, gy, radius=None):
    """ผู้เล่นยืนอยู่ในรัศมีของประตูหรือไม่ (ระยะยูคลิดบนสัดส่วนแผนที่)"""
    r = GATE_TRIGGER_RADIUS if radius is None else radius
    return math.hypot(gx - px, gy - py) <= r


def gates_near(gates, px, py):
    """ประตูทั้งหมดที่ผู้เล่นยืนอยู่ใกล้ (เรียงตามระยะ)"""
    out = []
    for g in gates:
        d = math.hypot(g["from_x"] - px, g["from_y"] - py)
        if d <= g["radius"]:
            out.append((d, g))
    out.sort(key=lambda t: t[0])
    return [g for _d, g in out]


def gate_block_reason(gate, level, money):
    """คืนเหตุผลที่ยังผ่านประตูไม่ได้ (None = ผ่านได้)"""
    if level < gate["required_level"]:
        return f"ต้องมีเลเวล {gate['required_level']} ขึ้นไป"
    if money < gate["required_money"]:
        return f"ต้องมีเงิน {gate['required_money']} บาท"
    return None


# ---- ร้านค้า ----
SELL_RATE_DEFAULT = 30          # เปอร์เซ็นต์ของราคาปกติที่ร้านรับซื้อ (ถ้า NPC ไม่ได้กำหนด)
SELL_RATE_MIN, SELL_RATE_MAX = 1, 100
MAX_BUY_QUANTITY = 99


def buy_cost(shop_price, quantity):
    """เงินที่ต้องจ่ายเมื่อซื้อ (ราคามาจากข้อมูลร้าน ไม่ใช่จาก Client)"""
    if quantity < 1 or quantity > MAX_BUY_QUANTITY:
        raise ValueError("จำนวนที่ซื้อต้องอยู่ระหว่าง 1 ถึง 99")
    return int(shop_price) * int(quantity)


def sell_price(base_price, quantity, sell_rate_percent=SELL_RATE_DEFAULT):
    """เงินที่ได้เมื่อขายคืนร้าน (ปัดลง, อย่างน้อย 1 ต่อชิ้นถ้าราคาตั้งต้น > 0)"""
    rate = clamp_sell_rate(sell_rate_percent)
    per_item = int(int(base_price) * rate // 100)
    if per_item == 0 and base_price > 0:
        per_item = 1
    return per_item * int(quantity)


def clamp_sell_rate(percent):
    p = int(percent)
    return max(SELL_RATE_MIN, min(SELL_RATE_MAX, p))


def can_afford(money, cost):
    return money >= cost


# ---- แชท ----
CHANNEL_MAP = "map"
CHANNEL_PARTY = "party"
CHANNEL_SYSTEM = "system"
VALID_CHANNELS = (CHANNEL_MAP, CHANNEL_PARTY, CHANNEL_SYSTEM)


def split_channel(text, party_prefix="#", whisper_prefix="@"):
    """แยกช่องทางจากคำนำหน้า (รูปแบบเดียวกับ EP7: '#' = ปาร์ตี้, '@' = กระซิบ)

    คืน (channel, body) โดย channel หนึ่งใน 'map' / 'party' / 'whisper'
    """
    t = (text or "").strip()
    if party_prefix and t.startswith(party_prefix):
        return "party", t[len(party_prefix):].strip()
    if whisper_prefix and t.startswith(whisper_prefix):
        return "whisper", t[len(whisper_prefix):].strip()
    return CHANNEL_MAP, t


def validate_chat(text, max_length=120):
    """ตรวจข้อความแชท คืนข้อความที่ตัดช่องว่างหัวท้ายแล้ว (โยน ValueError ถ้าไม่ผ่าน)"""
    if not isinstance(text, str):
        raise ValueError("ข้อความต้องเป็นตัวอักษร")
    body = text.strip()
    if not body:
        raise ValueError("ข้อความว่าง")
    if len(body) > max_length:
        raise ValueError(f"ข้อความยาวเกิน {max_length} ตัวอักษร")
    # กันอักขระควบคุม (ยกเว้นช่องว่างปกติ)
    if any(ord(c) < 32 for c in body):
        raise ValueError("ข้อความมีอักขระควบคุม")
    return body


# ---- ปาร์ตี้ ----
def can_invite(my_party_id, target_party_id, member_count, max_members):
    """ตรวจว่าเชิญคนนี้เข้าปาร์ตี้ได้หรือไม่ คืน (ok, reason)"""
    if my_party_id is None:
        return False, "คุณยังไม่มีปาร์ตี้"
    if target_party_id is not None:
        return False, "ผู้เล่นคนนั้นอยู่ในปาร์ตี้แล้ว"
    if member_count >= max_members:
        return False, "ปาร์ตี้เต็ม"
    return True, None


def exp_share_shares(exp, levels):
    """แบ่ง EXP ให้สมาชิกปาร์ตี้ตามสัดส่วนเลเวล (ปัดลงทุกคน ส่วนที่เหลือหายไป)

    ใช้กติกาเดียวกับเกมแนวเดียวกัน: คนเลเวลสูงได้มากกว่า แต่ทุกคนได้อย่างน้อย 1
    ถ้า EXP พอ
    """
    if not levels:
        return []
    total = sum(levels)
    if total <= 0:
        return [0 for _ in levels]
    shares = [int(exp * lv / total) for lv in levels]
    if exp > 0:
        shares = [max(1, s) for s in shares]
    return shares


def share_in_range(px, py, ox, oy, radius):
    """สมาชิกปาร์ตี้อยู่ในระยะรับ EXP ร่วมหรือไม่"""
    return math.hypot(ox - px, oy - py) <= radius
