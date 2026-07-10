#!/usr/bin/env python3
"""Local HTTP relay for Astrolabe voice requests.

The watch can POST plain HTTP to this process on the LAN. The relay forwards
the request body and auth headers to the HTTPS Supabase voice-pipeline endpoint.
"""

from __future__ import annotations

import argparse
import base64
import http.client
import http.server
import json
import os
import socketserver
import subprocess
import tempfile
import urllib.error
import urllib.parse
import urllib.request


HOST = os.environ.get("ASTROLABE_VOICE_RELAY_HOST", "0.0.0.0")
PORT = int(os.environ.get("ASTROLABE_VOICE_RELAY_PORT", "8787"))
TARGET = os.environ.get(
    "ASTROLABE_VOICE_RELAY_TARGET",
    "https://pilmscrodlitdrygabvo.supabase.co/functions/v1/voice-pipeline",
)
MOCK = os.environ.get("ASTROLABE_VOICE_RELAY_MOCK", "").lower() in ("1", "true", "yes")
MOCK_MP3: bytes | None = None


class RelayHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path.rstrip("/") not in ("", "/functions/v1/voice-pipeline"):
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length)
        if MOCK:
            self._serve_mock_response(body)
            return

        headers: dict[str, str] = {}
        for name in ("Authorization", "apikey", "Content-Type", "Accept"):
            value = self.headers.get(name)
            if value:
                headers[name] = value

        request = urllib.request.Request(TARGET, data=body, headers=headers, method="POST")
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                payload = response.read()
                self._log_upstream_summary(response.status, response.headers, payload)
                self.send_response(response.status)
                self._copy_response_headers(response.headers)
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
        except urllib.error.HTTPError as error:
            payload = error.read()
            self._log_upstream_summary(error.code, error.headers, payload)
            self.send_response(error.code)
            self._copy_response_headers(error.headers)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except Exception as exc:
            message = f"relay failed: {exc}\n".encode("utf-8")
            self.send_response(502)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", str(len(message)))
            self.end_headers()
            self.wfile.write(message)

    def do_GET(self) -> None:
        payload = b"astrolabe voice relay ok\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"voice-relay: {self.client_address[0]} {fmt % args}", flush=True)

    def _copy_response_headers(self, headers: http.client.HTTPMessage) -> None:
        for name in ("Content-Type", "X-Voice-Transcript", "X-Voice-Reply", "X-Voice-Tts-Source"):
            value = headers.get(name)
            if value:
                self.send_header(name, value)

    def _log_upstream_summary(self, status: int, headers: http.client.HTTPMessage, payload: bytes) -> None:
        transcript = headers.get("X-Voice-Transcript", "") or ""
        reply = headers.get("X-Voice-Reply", "") or ""
        route = headers.get("X-Voice-Route", "") or ""
        if transcript or reply or route:
            print(
                "voice-relay: upstream",
                status,
                f"route={route!r}",
                f"transcript={urllib.parse.unquote(transcript)!r}",
                f"reply={urllib.parse.unquote(reply)!r}",
                f"bytes={len(payload)}",
                flush=True,
            )

    def _serve_mock_response(self, body: bytes) -> None:
        meta = parse_voice_stream_meta(body)
        face = meta.get("face") or "unknown"
        faculty_slug = meta.get("facultySlug") or "mock-faculty"
        faculty_name = meta.get("facultyName") or "Mock Faculty"
        transcript = f"mock transcript from {face}"
        reply = f"mock duplex reply for {face}"
        payload = json.dumps(
            {
                "transcript": transcript,
                "reply": reply,
                "facultySlug": faculty_slug,
                "facultyName": faculty_name,
                "audioBase64": base64.b64encode(mock_mp3()).decode("ascii"),
            },
            separators=(",", ":"),
        ).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("X-Voice-Transcript", urllib.parse.quote(transcript))
        self.send_header("X-Voice-Reply", urllib.parse.quote(reply))
        self.send_header("X-Voice-Tts-Source", "mock")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)
        print(
            "voice-relay: mock",
            f"face={face!r}",
            f"faculty={faculty_slug!r}",
            f"pcm={max(len(body) - 4 - int.from_bytes(body[:4], 'little'), 0) if len(body) >= 4 else 0}B",
            f"mp3={len(MOCK_MP3 or b'')}B",
            flush=True,
        )


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


def parse_voice_stream_meta(body: bytes) -> dict[str, object]:
    if len(body) < 4:
        return {}
    meta_len = int.from_bytes(body[:4], "little")
    if meta_len <= 0 or 4 + meta_len > len(body):
        return {}
    try:
        meta = json.loads(body[4 : 4 + meta_len].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return {}
    return meta if isinstance(meta, dict) else {}


def mock_mp3() -> bytes:
    global MOCK_MP3
    if MOCK_MP3 is not None:
        return MOCK_MP3
    with tempfile.NamedTemporaryFile(suffix=".mp3") as tmp:
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-loglevel",
                "error",
                "-f",
                "lavfi",
                "-i",
                "sine=frequency=880:duration=0.35",
                "-ar",
                "24000",
                "-ac",
                "1",
                "-b:a",
                "48k",
                "-y",
                tmp.name,
            ],
            check=True,
        )
        tmp.seek(0)
        MOCK_MP3 = tmp.read()
    if len(MOCK_MP3) < 64:
        raise RuntimeError("generated mock MP3 is too small")
    return MOCK_MP3


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--target", default=TARGET)
    parser.add_argument("--mock", action="store_true", default=MOCK, help="return a local JSON+MP3 response")
    args = parser.parse_args()
    TARGET = args.target
    MOCK = args.mock
    if MOCK:
        mock_mp3()
    with ReusableTCPServer((args.host, args.port), RelayHandler) as server:
        mode = "mock" if MOCK else f"relay -> {TARGET}"
        print(f"voice-relay: listening on http://{args.host}:{args.port} ({mode})", flush=True)
        server.serve_forever()
