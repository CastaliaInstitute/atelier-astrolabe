#!/usr/bin/env python3
"""Local HTTP relay for Astrolabe voice requests.

The watch can POST plain HTTP to this process on the LAN. The relay forwards
the request body and auth headers to the HTTPS Supabase voice-pipeline endpoint.
"""

from __future__ import annotations

import http.server
import http.client
import argparse
import os
import socketserver
import urllib.error
import urllib.request


HOST = os.environ.get("ASTROLABE_VOICE_RELAY_HOST", "0.0.0.0")
PORT = int(os.environ.get("ASTROLABE_VOICE_RELAY_PORT", "8787"))
TARGET = os.environ.get(
    "ASTROLABE_VOICE_RELAY_TARGET",
    "https://pilmscrodlitdrygabvo.supabase.co/functions/v1/voice-pipeline",
)


class RelayHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path.rstrip("/") not in ("", "/functions/v1/voice-pipeline"):
            self.send_error(404)
            return

        length = int(self.headers.get("Content-Length", "0") or "0")
        body = self.rfile.read(length)
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


class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=HOST)
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--target", default=TARGET)
    args = parser.parse_args()
    TARGET = args.target
    with ReusableTCPServer((args.host, args.port), RelayHandler) as server:
        print(f"voice-relay: listening on http://{args.host}:{args.port} -> {TARGET}", flush=True)
        server.serve_forever()
