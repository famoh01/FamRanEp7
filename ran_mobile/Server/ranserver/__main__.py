# ran_mobile/Server/ranserver/__main__.py
# วิธีรัน: python -m ranserver --db ran_mobile.db --host 127.0.0.1 --port 8080
import argparse
import random
import sys

from .api import Api
from .db import connect, init_db
from .http_server import make_server


def main(argv=None):
    ap = argparse.ArgumentParser(description="RAN MOBILE API server (dev)")
    ap.add_argument("--db", default="ran_mobile.db", help="ไฟล์ SQLite (สร้างให้ถ้ายังไม่มี)")
    ap.add_argument("--host", default="127.0.0.1", help="ใช้ 0.0.0.0 เมื่อต้องการให้มือถือในวงเดียวกันเชื่อมต่อ")
    ap.add_argument("--port", type=int, default=8080)
    args = ap.parse_args(argv)

    conn = connect(args.db)
    init_db(conn)
    api = Api(conn, rng=random.Random())
    httpd = make_server(api, args.host, args.port)
    print(f"RAN MOBILE API listening on http://{args.host}:{args.port}  (db: {args.db})", flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
        conn.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
