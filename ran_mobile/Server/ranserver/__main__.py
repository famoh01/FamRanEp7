# ran_mobile/Server/ranserver/__main__.py
# วิธีรัน: python -m ranserver --db ran_mobile.db --host 127.0.0.1 --port 8080
# ตัวเลือกอื่น: --log-file ไฟล์ log (หมุนอัตโนมัติ), --log-level, --world-idle-s (session โลกเกมหมดอายุเมื่อไม่มีการติดต่อ)
import argparse
import random
import sys

from .api import Api, WORLD_IDLE_MS_DEFAULT
from .content import ContentError
from .db import connect, init_db, load_content
from .http_server import make_server
from .log import configure, get_logger

log_server = get_logger("server")


def main(argv=None):
    ap = argparse.ArgumentParser(description="RAN MOBILE API server (dev)")
    ap.add_argument("--db", default="ran_mobile.db", help="ไฟล์ SQLite (สร้างให้ถ้ายังไม่มี)")
    ap.add_argument("--host", default="127.0.0.1", help="ใช้ 0.0.0.0 เมื่อต้องการให้มือถือในวงเดียวกันเชื่อมต่อ")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    ap.add_argument("--log-file", default=None, help="เขียน log ลงไฟล์ด้วย (นอกเหนือจาก stderr)")
    ap.add_argument("--world-idle-s", type=float, default=WORLD_IDLE_MS_DEFAULT / 1000.0,
                    help="session โลกเกมหมดอายุเมื่อไม่มีการติดต่อเกินกี่วินาที")
    args = ap.parse_args(argv)
    if args.world_idle_s <= 0:
        ap.error("--world-idle-s ต้องมากกว่า 0")

    configure(level=args.log_level, log_file=args.log_file)
    conn = connect(args.db)
    try:
        init_db(conn)
        # ชุดข้อมูลเกม (content.json) ถูกตรวจแล้วครั้งเดียว: Api ใช้ค่าเดียวกับที่เขียนลงฐานข้อมูล
        content = load_content(conn)
    except ContentError as e:
        print(f"ไฟล์ข้อมูลเกมไม่ถูกต้อง: {e}", file=sys.stderr, flush=True)
        conn.close()
        return 2
    # ค่าเส้นโค้งเลเวลของเกมมาจากไฟล์ข้อมูลเดียวกัน
    from . import game
    game.configure(content.level_up)
    api = Api(conn, rng=random.Random(), world_idle_ms=int(args.world_idle_s * 1000),
              content=content)
    httpd = make_server(api, args.host, args.port)
    log_server.info("listening on %s:%s db=%s world_idle_s=%g", args.host, args.port, args.db, args.world_idle_s)
    print(f"RAN MOBILE API listening on http://{args.host}:{args.port}  (db: {args.db})", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        log_server.info("shutting down")
        httpd.server_close()
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
