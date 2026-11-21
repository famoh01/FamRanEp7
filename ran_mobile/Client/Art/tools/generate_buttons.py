# ran_mobile/Client/Art/tools/generate_buttons.py
# สร้างปุ่ม HUD แบบเวกเตอร์ทั้งหมดด้วยโค้ด (ออกแบบขึ้นใหม่ทั้งหมด ไม่ได้คัดลอกภาพจากเกมหรือ APK ใด ๆ)
#
# การใช้งาน (ต้องมี Pillow):
#   pip install pillow
#   python generate_buttons.py            # เขียนไฟล์ PNG ไปที่ ../Buttons/
#
# วาดที่ความละเอียด 4 เท่าแล้วย่อลงเพื่อให้ขอบเรียบ
import os
from PIL import Image, ImageDraw, ImageFont

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Buttons")
SS = 4                   # supersampling factor
SIZE = 256               # ขนาดปุ่ม (px) หลังย่อ
FONT_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

# พาเล็ตต์ของโปรเจกต์ (เลือกเอง)
NAVY = (22, 30, 46)
WHITE = (245, 247, 250)
GOLD = (232, 179, 58)
RED = (214, 69, 69)
BLUE = (63, 140, 255)
TEAL = (43, 179, 163)
GREY = (84, 98, 122)
PURPLE = (150, 96, 220)
GREEN = (120, 196, 72)


def darker(c, f=0.7):
    return tuple(int(v * f) for v in c)


def lighter(c, f=0.35):
    return tuple(int(v + (255 - v) * f) for v in c)


def new_canvas(size=SIZE):
    return Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))


def finish(img, path):
    out = img.resize((SIZE, SIZE), Image.LANCZOS)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    out.save(path)


def round_button(base, icon_fn, pressed=False, label=None):
    img = new_canvas()
    d = ImageDraw.Draw(img)
    c = SIZE * SS // 2
    r_outer = c - 4 * SS
    fill = darker(base, 0.75) if pressed else base
    ring = darker(base, 0.45)
    # วงนอก (ขอบเข้ม)
    d.ellipse([c - r_outer, c - r_outer, c + r_outer, c + r_outer], fill=ring + (255,))
    # วงใน (สีหลัก)
    r_in = r_outer - 10 * SS
    d.ellipse([c - r_in, c - r_in, c + r_in, c + r_in], fill=fill + (255,))
    # แสงด้านบน (ไม่ทำเมื่อกด)
    if not pressed:
        hl = Image.new("RGBA", img.size, (0, 0, 0, 0))
        hd = ImageDraw.Draw(hl)
        hd.ellipse([c - r_in + 8 * SS, c - r_in + 6 * SS, c + r_in - 8 * SS, c + r_in * 0 + 2 * SS], fill=(255, 255, 255, 70))
        img = Image.alpha_composite(img, hl)
        d = ImageDraw.Draw(img)
    icon_fn(d, c, lighter(WHITE, 0) if not pressed else (210, 214, 222))
    if label:
        font = ImageFont.truetype(FONT_BOLD, 40 * SS)
        w = d.textlength(label, font=font)
        d.text((c - w / 2, c - 26 * SS), label, font=font, fill=WHITE)
    return img


# ---------------------------------------------------------------- icons
def icon_attack(d, c, col):
    # ดาวแปดแฉก (คำนวณจุดเอง)
    import math
    pts = []
    for i in range(16):
        ang = math.pi * i / 8 - math.pi / 2
        rad = 78 * SS if i % 2 == 0 else 38 * SS
        pts.append((c + rad * math.cos(ang), c + rad * math.sin(ang)))
    d.polygon(pts, fill=col)


def icon_dash(d, c, col):
    w = 16 * SS
    for dx in (-30, 10):
        x = c + dx * SS
        d.polygon([(x, c - 46 * SS), (x + 40 * SS, c), (x, c + 46 * SS),
                   (x, c + 46 * SS - w), (x + 40 * SS - w * 0.6, c), (x, c - 46 * SS + w)], fill=col)


def icon_skill(d, c, col):
    # สัญลักษณ์ฟ้าผ่า
    pts = [(c + 10 * SS, c - 80 * SS), (c - 50 * SS, c + 8 * SS), (c - 4 * SS, c + 8 * SS),
           (c - 14 * SS, c + 80 * SS), (c + 50 * SS, c - 12 * SS), (c + 4 * SS, c - 12 * SS),
           (c + 22 * SS, c - 80 * SS)]
    d.polygon(pts, fill=col)


def icon_target(d, c, col):
    w = 12 * SS
    d.ellipse([c - 60 * SS, c - 60 * SS, c + 60 * SS, c + 60 * SS], outline=col, width=w)
    d.ellipse([c - 14 * SS, c - 14 * SS, c + 14 * SS, c + 14 * SS], fill=col)
    for (x1, y1, x2, y2) in [(c, c - 84 * SS, c, c - 46 * SS), (c, c + 46 * SS, c, c + 84 * SS),
                             (c - 84 * SS, c, c - 46 * SS, c), (c + 46 * SS, c, c + 84 * SS, c)]:
        d.line([x1, y1, x2, y2], fill=col, width=w)


def icon_camera(d, c, col):
    d.rounded_rectangle([c - 70 * SS, c - 42 * SS, c + 70 * SS, c + 52 * SS], radius=14 * SS, fill=col)
    d.rectangle([c - 26 * SS, c - 62 * SS, c + 4 * SS, c - 40 * SS], fill=col)
    d.ellipse([c - 30 * SS, c - 26 * SS, c + 30 * SS, c + 34 * SS], fill=(40, 48, 66))
    d.ellipse([c - 14 * SS, c - 10 * SS, c + 14 * SS, c + 18 * SS], fill=col)


def icon_inventory(d, c, col):
    # กระเป๋า
    d.arc([c - 34 * SS, c - 78 * SS, c + 34 * SS, c - 6 * SS], start=180, end=360, fill=col, width=12 * SS)
    d.rounded_rectangle([c - 66 * SS, c - 30 * SS, c + 66 * SS, c + 70 * SS], radius=14 * SS, fill=col)


def icon_character(d, c, col):
    d.ellipse([c - 30 * SS, c - 74 * SS, c + 30 * SS, c - 14 * SS], fill=col)
    d.rounded_rectangle([c - 58 * SS, c - 6 * SS, c + 58 * SS, c + 74 * SS], radius=40 * SS, fill=col)


def icon_quest(d, c, col):
    # ม้วนกระดาษ
    d.rounded_rectangle([c - 50 * SS, c - 68 * SS, c + 50 * SS, c + 68 * SS], radius=10 * SS, fill=col)
    for i, y in enumerate((-36, -10, 16)):
        w = 70 if i < 2 else 46
        d.rounded_rectangle([c - 34 * SS, c + y * SS - 6 * SS, c - 34 * SS + w * SS * 0.8, c + y * SS + 6 * SS],
                            radius=6 * SS, fill=(40, 48, 66))


def icon_map(d, c, col):
    pts = [(c - 72 * SS, c - 44 * SS), (c - 24 * SS, c - 62 * SS), (c + 24 * SS, c - 44 * SS),
           (c + 72 * SS, c - 62 * SS), (c + 72 * SS, c + 56 * SS), (c + 24 * SS, c + 74 * SS),
           (c - 24 * SS, c + 56 * SS), (c - 72 * SS, c + 74 * SS)]
    d.polygon(pts, fill=col)


def icon_settings(d, c, col):
    import math
    for i in range(8):
        ang = math.pi * 2 * i / 8
        x = c + 66 * SS * math.cos(ang)
        y = c + 66 * SS * math.sin(ang)
        d.ellipse([x - 16 * SS, y - 16 * SS, x + 16 * SS, y + 16 * SS], fill=col)
    d.ellipse([c - 58 * SS, c - 58 * SS, c + 58 * SS, c + 58 * SS], fill=col)
    d.ellipse([c - 24 * SS, c - 24 * SS, c + 24 * SS, c + 24 * SS], fill=(40, 48, 66))


def icon_menu(d, c, col):
    for dy in (-52, 0, 52):
        d.rounded_rectangle([c - 76 * SS, c + (dy - 12) * SS, c + 76 * SS, c + (dy + 12) * SS], radius=12 * SS, fill=col)


def icon_none(d, c, col):
    pass


# ---------------------------------------------------------------- joystick
def joystick_base():
    img = new_canvas(320)
    d = ImageDraw.Draw(img)
    c = 160 * SS
    r = 150 * SS
    d.ellipse([c - r, c - r, c + r, c + r], fill=(22, 30, 46, 110), outline=(245, 247, 250, 150), width=8 * SS)
    d.ellipse([c - r + 26 * SS, c - r + 26 * SS, c + r - 26 * SS, c + r - 26 * SS], outline=(245, 247, 250, 60), width=4 * SS)
    return img.resize((320, 320), Image.LANCZOS)


def joystick_handle():
    img = new_canvas(160)
    d = ImageDraw.Draw(img)
    c = 80 * SS
    r = 62 * SS
    d.ellipse([c - r, c - r, c + r, c + r], fill=(63, 140, 255, 235), outline=(245, 247, 250, 255), width=6 * SS)
    d.ellipse([c - 30 * SS, c - 40 * SS, c + 14 * SS, c - 6 * SS], fill=(255, 255, 255, 90))
    return img.resize((160, 160), Image.LANCZOS)


# ---------------------------------------------------------------- slots / bars / frames
def quick_slot(pressed=False):
    img = new_canvas()
    d = ImageDraw.Draw(img)
    fill = (22, 30, 46, 230) if not pressed else (14, 20, 32, 240)
    d.rounded_rectangle([8 * SS, 8 * SS, 248 * SS, 248 * SS], radius=36 * SS, fill=fill,
                        outline=(232, 179, 58, 255), width=8 * SS)
    return img


def bar(fill_color, name_suffix):
    # แถบแนวนอน 512x40 ใช้แบบ 9-slice ใน Unity
    w, h = 512, 40
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, w * SS - 1, h * SS - 1], radius=h * SS // 2, fill=(22, 30, 46, 200))
    return img.resize((w, h), Image.LANCZOS)


def bar_fill(color):
    w, h = 512, 40
    img = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([4 * SS, 4 * SS, w * SS - 4 * SS, h * SS - 4 * SS], radius=(h // 2 - 4) * SS,
                        fill=color + (255,))
    d.rounded_rectangle([8 * SS, 6 * SS, w * SS - 8 * SS, 14 * SS], radius=4 * SS, fill=(255, 255, 255, 70))
    return img.resize((w, h), Image.LANCZOS)


def portrait_frame():
    img = new_canvas()
    d = ImageDraw.Draw(img)
    c = SIZE * SS // 2
    r = c - 6 * SS
    d.ellipse([c - r, c - r, c + r, c + r], outline=(232, 179, 58, 255), width=12 * SS)
    return img


def build_all():
    out = lambda name: os.path.join(OUT_DIR, name)
    specs = [
        ("btn_attack.png", RED, icon_attack, None),
        ("btn_attack_pressed.png", RED, icon_attack, "P"),
        ("btn_dash.png", TEAL, icon_dash, None),
        ("btn_dash_pressed.png", TEAL, icon_dash, "P"),
        ("btn_skill.png", PURPLE, icon_skill, None),
        ("btn_skill_pressed.png", PURPLE, icon_skill, "P"),
        ("btn_target.png", GREY, icon_target, None),
        ("btn_camera.png", GREY, icon_camera, None),
        ("btn_inventory.png", BLUE, icon_inventory, None),
        ("btn_character.png", BLUE, icon_character, None),
        ("btn_quest.png", GOLD, icon_quest, None),
        ("btn_map.png", GREEN, icon_map, None),
        ("btn_settings.png", GREY, icon_settings, None),
        ("btn_menu.png", GREY, icon_menu, None),
    ]
    for name, color, fn, p in specs:
        finish(round_button(color, fn, pressed=(p == "P")), out(name))

    for name, pressed in (("slot_frame.png", False), ("slot_frame_pressed.png", True)):
        finish(quick_slot(pressed), out(name))

    for k in ("F1", "F2", "F3", "F4"):
        finish(round_button(GREY, icon_none, label=k).resize((SIZE, SIZE)), out(f"fkey_{k.lower()}.png"))

    joystick_base().save(out("joystick_base.png"))
    joystick_handle().save(out("joystick_handle.png"))

    bar(None, "").save(out("bar_bg.png"))
    for name, col in (("hp", RED), ("mp", BLUE), ("sp", GREEN), ("exp", PURPLE)):
        bar_fill(col).save(out(f"bar_fill_{name}.png"))

    finish(portrait_frame(), out("portrait_frame.png"))


if __name__ == "__main__":
    build_all()
    print("generated into", os.path.normpath(OUT_DIR))
