# ran_mobile/Server/ranserver/log.py
# บันทึก log ของ Server แยกหมวด: server, http, auth, world, db
# - ทุกข้อความผ่านตัวกรอง redact ก่อนถูกเขียน: password, token, secret, Authorization ไม่มีทางหลุดลง log
# - ไม่บันทึก body และ header ของคำขอ (บันทึกเฉพาะ method, path, status, เวลา)
# - ชื่อผู้ใช้ใน log ถูกปิดบังบางส่วน (mask_name)
import logging
import logging.handlers
import re

CATEGORIES = ("server", "http", "auth", "world", "db")
ROOT = "ranserver"
LOG_FORMAT = "%(asctime)s %(levelname)s [%(name)s] %(message)s"
MAX_FILE_BYTES = 5 * 1024 * 1024
BACKUP_COUNT = 3

# รูปแบบที่ถือว่าเป็นข้อมูลลับ (JSON, key=value และ Bearer token)
_SECRET_KEYS = r"password|passwd|pass|token|secret|authorization|api[_-]?key|idempotency[_-]?key|cookie"
_REDACT_RULES = [
    (re.compile(r'("(?:' + _SECRET_KEYS + r')"\s*:\s*")[^"]*(")', re.IGNORECASE), r"\1***\2"),
    (re.compile(r"(?i)\b(bearer\s+)[A-Za-z0-9._~+/=-]+"), r"\1***"),
    (re.compile(r"(?i)\b((?:" + _SECRET_KEYS + r")\s*[=:]\s*)[^\s,;&\"']+"), r"\1***"),
]


def redact(text):
    """แทนค่าลับด้วย *** (คืนข้อความใหม่ ไม่แก้ค่าเดิม)"""
    out = str(text)
    for pattern, repl in _REDACT_RULES:
        out = pattern.sub(repl, out)
    return out


def mask_name(name):
    """ปิดบังชื่อผู้ใช้สำหรับ log: 'alice' -> 'a***(5)'"""
    if not isinstance(name, str) or not name:
        return "<empty>"
    return f"{name[0]}***({len(name)})"


class RedactFilter(logging.Filter):
    def filter(self, record):
        record.msg = redact(record.getMessage())
        record.args = ()
        if record.exc_info and not record.exc_text:
            record.exc_text = redact(logging.Formatter().formatException(record.exc_info))
            record.exc_info = None
        return True


def get_logger(category):
    if category not in CATEGORIES:
        raise ValueError(f"unknown log category: {category}")
    return logging.getLogger(f"{ROOT}.{category}")


def configure(level="INFO", log_file=None, stream=None):
    """ตั้งค่า log ของ Server (เรียกซ้ำได้ ไม่เพิ่ม handler ซ้ำ)"""
    root = logging.getLogger(ROOT)
    for h in list(root.handlers):
        if getattr(h, "_ranserver", False):
            root.removeHandler(h)
            h.close()
    root.setLevel(getattr(logging, str(level).upper(), logging.INFO))
    root.propagate = False
    formatter = logging.Formatter(LOG_FORMAT)
    handlers = [logging.StreamHandler(stream)]
    if log_file:
        handlers.append(logging.handlers.RotatingFileHandler(
            log_file, maxBytes=MAX_FILE_BYTES, backupCount=BACKUP_COUNT, encoding="utf-8"))
    for h in handlers:
        h.setFormatter(formatter)
        h.addFilter(RedactFilter())
        h._ranserver = True
        root.addHandler(h)
    return root
