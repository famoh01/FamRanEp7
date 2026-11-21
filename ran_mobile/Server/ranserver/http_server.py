# ran_mobile/Server/ranserver/http_server.py
# ชั้น HTTP บาง ๆ ครอบ Api ด้วย http.server ของ Python (ไม่มี dependency ภายนอก)
# สำหรับการพัฒนาเท่านั้น โปรดิวซ์ชันควรใช้ WSGI/ASGI เช่น gunicorn + nginx (ดู README)
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

MAX_BODY_BYTES = 64 * 1024


class _Handler(BaseHTTPRequestHandler):
    server_version = "RanMobileAPI/1.0"
    protocol_version = "HTTP/1.1"

    def _send(self, status, payload):
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _run(self):
        if self.headers.get("Transfer-Encoding"):
            self._send(411, {"error": {"code": "length_required", "message": "ต้องระบุ Content-Length"}})
            return
        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            self._send(400, {"error": {"code": "bad_length", "message": "Content-Length ไม่ถูกต้อง"}})
            return
        if length < 0 or length > MAX_BODY_BYTES:
            self._send(413, {"error": {"code": "payload_too_large", "message": "ข้อมูลใหญ่เกินไป"}})
            return
        body = self.rfile.read(length) if length else b""
        path = urlsplit(self.path).path
        status, payload = self.server.api.handle(self.command, path, dict(self.headers.items()), body)
        self._send(status, payload)

    do_GET = _run
    do_POST = _run
    do_DELETE = _run

    def log_message(self, fmt, *args):  # ไม่พิมพ์ทุกคำขอ (ป้องกัน token รั่วใน log)
        return


def make_server(api, host="127.0.0.1", port=8080):
    httpd = ThreadingHTTPServer((host, port), _Handler)
    httpd.api = api
    return httpd
