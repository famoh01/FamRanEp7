# ran_mobile/Server/ranserver/security.py
# รหัสผ่านใช้ scrypt (stdlib) และเก็บเป็น hash เท่านั้น
# Token เป็นค่าสุ่ม และฐานข้อมูลเก็บเฉพาะ SHA-256 ของ token
import hashlib
import hmac
import re
import secrets

SCRYPT_N = 2 ** 14
SCRYPT_R = 8
SCRYPT_P = 1
SCRYPT_DKLEN = 32

USERNAME_RE = re.compile(r"^[A-Za-z0-9_]{3,20}$")
# รองรับตัวอักษรไทย (U+0E00-U+0E7F) ในชื่อตัวละคร
CHARACTER_NAME_RE = re.compile(r"^[A-Za-z0-9_\u0E00-\u0E7F]{2,16}$")
EMAIL_RE = re.compile(r"^[^@\s]{1,64}@[^@\s]{1,189}\.[^@\s]{2,24}$")

PASSWORD_MIN = 8
PASSWORD_MAX = 128


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(16)
    dk = hashlib.scrypt(
        password.encode("utf-8"),
        salt=salt,
        n=SCRYPT_N,
        r=SCRYPT_R,
        p=SCRYPT_P,
        dklen=SCRYPT_DKLEN,
    )
    return f"scrypt${SCRYPT_N}${SCRYPT_R}${SCRYPT_P}${salt.hex()}${dk.hex()}"


def verify_password(password: str, stored: str) -> bool:
    try:
        scheme, n, r, p, salt_hex, hash_hex = stored.split("$")
        if scheme != "scrypt":
            return False
        dk = hashlib.scrypt(
            password.encode("utf-8"),
            salt=bytes.fromhex(salt_hex),
            n=int(n),
            r=int(r),
            p=int(p),
            dklen=len(bytes.fromhex(hash_hex)),
        )
    except (ValueError, TypeError):
        return False
    return hmac.compare_digest(dk.hex(), hash_hex)


# ใช้ตอนไม่พบชื่อผู้ใช้ เพื่อให้เวลาตอบสนองใกล้เคียงกันและเดาชื่อผู้ใช้ยากขึ้น
DUMMY_HASH = hash_password("dummy-password-for-timing-only")


def new_token() -> str:
    return secrets.token_urlsafe(32)


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()
