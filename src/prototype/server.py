#!/usr/bin/env python3
"""
The worker owns every C++ object, tool permission, chat session and API key.
"""
from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import queue
import secrets
import subprocess
import sys
import threading
import uuid

BASE = Path(__file__).resolve().parent
STATIC = BASE / "web"
TERMINAL = {"complete", "approval", "cancelled", "error"}
MAX_BODY = 48 * 1024
MAX_EVENTS = 1200


class WorkerBridge:
    def __init__(self, executable: Path, mode: str) -> None:
        self.process = subprocess.Popen(
            [str(executable.resolve()), "--" + mode],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=None,
            text=True, encoding="utf-8", bufsize=1,
        )
        self.pending: dict[str, queue.Queue[dict]] = {}
        self.lock = threading.Lock()
        self.write_lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True, name="worker-output")
        self.reader.start()

    def _read(self) -> None:
        assert self.process.stdout is not None
        try:
            for line in self.process.stdout:
                try:
                    event = json.loads(line)
                    if not isinstance(event, dict):
                        continue
                    request_id = event.get("id")
                    with self.lock:
                        receiver = self.pending.get(request_id)
                    if receiver is not None:
                        receiver.put(event)
                except (json.JSONDecodeError, TypeError):
                    print("[bridge] invalid JSON from worker", file=sys.stderr)
        finally:
            with self.lock:
                pending = list(self.pending.values())
            for receiver in pending:
                receiver.put({"type": "error", "message": "Worker-Verbindung beendet"})

    def send(self, action: dict):
        request_id = uuid.uuid4().hex
        receiver: queue.Queue[dict] = queue.Queue()
        with self.lock:
            self.pending[request_id] = receiver
        try:
            payload = {"id": request_id, **action}
            with self.write_lock:
                if self.process.poll() is not None:
                    raise RuntimeError("Worker läuft nicht")
                assert self.process.stdin is not None
                self.process.stdin.write(json.dumps(payload, ensure_ascii=False) + "\n")
                self.process.stdin.flush()
            for _ in range(MAX_EVENTS):
                try:
                    event = receiver.get(timeout=120)
                except queue.Empty:
                    yield {"type": "error", "message": "Worker antwortet nicht (Timeout)"}
                    return
                yield event
                if event.get("type") in TERMINAL:
                    return
            yield {"type": "error", "message": "Zu viele Worker-Ereignisse"}
        except (BrokenPipeError, OSError, RuntimeError) as exc:
            print("[bridge] worker failure:", type(exc).__name__, file=sys.stderr)
            yield {"type": "error", "message": "Worker nicht verfügbar"}
        finally:
            with self.lock:
                self.pending.pop(request_id, None)

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=2)


class Handler(BaseHTTPRequestHandler):
    # Populated once at server startup, before any request is accepted.
    bridge: WorkerBridge
    secret: str

    def _respond(self, code: int, body: bytes, content_type: str) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("X-Frame-Options", "DENY")
        self.send_header("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if not self._valid_host():
            return self._respond(403, b"Forbidden", "text/plain; charset=utf-8")
        if self.path in ("/", "/index.html"):
            source = (STATIC / "index.html").read_text(encoding="utf-8")
            source = source.replace("__CSRF_TOKEN__", self.secret)
            return self._respond(200, source.encode(), "text/html; charset=utf-8")
        files = {"/app.js": ("app.js", "text/javascript; charset=utf-8"),
                 "/style.css": ("style.css", "text/css; charset=utf-8")}
        if self.path in files:
            filename, mimetype = files[self.path]
            return self._respond(200, (STATIC / filename).read_bytes(), mimetype)
        self._respond(404, b"Not found", "text/plain")

    def _valid_host(self) -> bool:
        # Host header poisoning protection (server binds only loopback).
        hostname = self.headers.get("Host", "").split(":", 1)[0]
        return hostname in ("127.0.0.1", "localhost")

    def do_POST(self) -> None:
        if self.path != "/api/action":
            return self._respond(404, b"Not found", "text/plain")
        port = self.server.server_address[1]
        allowed_origins = {f"http://127.0.0.1:{port}", f"http://localhost:{port}"}
        origin = self.headers.get("Origin")
        if (not self._valid_host() or
            (origin is not None and origin not in allowed_origins) or
            self.headers.get("X-Local-Token") != self.secret):
            return self._respond(403, b"Forbidden", "text/plain")
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= MAX_BODY:
                raise ValueError("invalid length")
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("body must be object")
            action = body.get("action")
            if action not in ("send", "resume", "reset", "snapshot", "cancel"):
                raise ValueError("invalid action")
            if action == "send" and (not isinstance(body.get("text"), str) or
                                      not 0 < len(body["text"]) <= 16_000):
                raise ValueError("invalid text")
            if action == "resume" and (not isinstance(body.get("approval_id"), str) or
                                        not isinstance(body.get("approved"), bool)):
                raise ValueError("invalid approval")
            # Explicit allowlist: do not forward arbitrary attacker-controlled fields.
            payload = {"action": action}
            if action == "send":
                payload["text"] = body["text"]
            elif action == "resume":
                payload["approval_id"] = body["approval_id"]
                payload["approved"] = body["approved"]
        except (ValueError, json.JSONDecodeError):
            return self._respond(400, b"Invalid request", "text/plain")
        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson; charset=utf-8")
        self.send_header("Cache-Control", "no-store, no-transform")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Connection", "close")
        self.end_headers()
        self.close_connection = True
        try:
            for event in self.bridge.send(payload):
                self.wfile.write((json.dumps(event, ensure_ascii=False) + "\n").encode("utf-8"))
                self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass  # A disconnected tab never authorizes a pending operation.

    def log_message(self, format: str, *args: object) -> None:
        # Don't log full paths or prompt bodies; this is a local development server.
        if self.path not in ("/api/action",):
            super().log_message(format, *args)


def main() -> None:
    parser = argparse.ArgumentParser(description="Local-only Reflection chat prototype")
    parser.add_argument("--worker", type=Path, required=True)
    parser.add_argument("--mode", choices=("mock", "openai"), default="mock")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not args.worker.exists():
        parser.error(f"Worker executable missing: {args.worker}")
    bridge = WorkerBridge(args.worker, args.mode)
    Handler.bridge = bridge
    Handler.secret = secrets.token_urlsafe(32)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    try:
        port = server.server_address[1]
        print(f"Chat-Prototyp ({args.mode}): http://127.0.0.1:{port}/", flush=True)
        server.serve_forever(poll_interval=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        bridge.close()


if __name__ == "__main__":
    main()
