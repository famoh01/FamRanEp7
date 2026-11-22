# ran_mobile/Server/ranserver/health.py
# ตรวจสุขภาพฐานข้อมูลโดยไม่ต้องเปิดเซิร์ฟเวอร์: python -m ranserver.health --db ran_mobile.db
# คืน exit 0 เมื่อปกติ, 1 เมื่อมีปัญหา (พิมพ์รายงานเป็น JSON ไม่มีข้อมูลลับ)
import argparse
import json
import os
import sqlite3
import sys
import time

from .migrations import LATEST, current_version
from .world import session_expired

REQUIRED_TABLES = ("accounts", "sessions", "characters", "maps", "world_sessions", "monsters", "items")


def check(path, world_idle_ms=5 * 60 * 1000, now_ms=None):
    """คืน dict รายงาน: ok (bool), problems (รายการ), counts (จำนวนแถวหลัก)"""
    report = {"ok": False, "problems": [], "warnings": [], "schema_version": None,
              "expected_version": LATEST, "counts": {}}
    if not os.path.exists(path):
        report["problems"].append("ไม่พบไฟล์ฐานข้อมูล")
        return report
    now_ms = int(time.time() * 1000) if now_ms is None else now_ms
    conn = sqlite3.connect(f"file:{path}?mode=ro", uri=True)
    try:
        version = current_version(conn)
        report["schema_version"] = version
        if version != LATEST:
            report["problems"].append(f"schema version {version} ไม่ตรงกับ {LATEST} (รัน migration ก่อน)")
        integrity = conn.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            report["problems"].append(f"integrity_check: {integrity}")
        tables = {r[0] for r in conn.execute("SELECT name FROM sqlite_master WHERE type = 'table'")}
        for t in REQUIRED_TABLES:
            if t not in tables:
                report["problems"].append(f"ไม่มีตาราง {t}")
        if not report["problems"]:
            now_s = now_ms // 1000
            report["counts"] = {
                "accounts": conn.execute("SELECT COUNT(*) FROM accounts").fetchone()[0],
                "characters": conn.execute("SELECT COUNT(*) FROM characters").fetchone()[0],
                "active_tokens": conn.execute(
                    "SELECT COUNT(*) FROM sessions WHERE expires_at > ?", (now_s,)).fetchone()[0],
                "maps": conn.execute("SELECT COUNT(*) FROM maps").fetchone()[0],
                "world_sessions": conn.execute("SELECT COUNT(*) FROM world_sessions").fetchone()[0],
            }
            stale = [r[0] for r in conn.execute("SELECT last_seen_ms FROM world_sessions")
                     if session_expired(r[0], now_ms, world_idle_ms)]
            report["counts"]["stale_world_sessions"] = len(stale)
            if stale:
                report["warnings"].append(f"มี world session ที่ควรหมดอายุ {len(stale)} รายการ (จะถูกปิดเมื่อมีคำขอถัดไป)")
            if report["counts"]["maps"] == 0:
                report["problems"].append("ไม่มีข้อมูลแผนที่ (seed.sql ยังไม่ได้รัน)")
    finally:
        conn.close()
    report["ok"] = not report["problems"]
    return report


def main(argv=None):
    ap = argparse.ArgumentParser(description="ตรวจสุขภาพฐานข้อมูล RAN MOBILE")
    ap.add_argument("--db", required=True)
    ap.add_argument("--world-idle-s", type=float, default=300.0)
    args = ap.parse_args(argv)
    report = check(args.db, int(args.world_idle_s * 1000))
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
