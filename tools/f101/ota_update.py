"""Serve one verified image to Astrolabe and check the rebooted image over BLE."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
import secrets
import socket
import threading
import time
from urllib.parse import urlparse

from ble_android import call

CONTROL = '06000000-5017-0065-6261-6c6f72747341'


def status(address):
    return json.loads(call({'op': 'read', 'address': address, 'characteristic': CONTROL})['value'])


def verified_update(before, after, transferred, elf_sha):
    return (transferred and bool(before.get('partition')) and bool(after.get('partition'))
            and after['partition'] != before['partition']
            and after.get('elfSha256') == elf_sha and after.get('otaActive') is False)


def install(address, firmware, expected_sha, elf_sha, key):
    before = status(address)
    if before.get('board') != '185b':
        raise RuntimeError('Expected 1.85B firmware control service')
    if before.get('face') != 'ota' or not before.get('otaReady'):
        raise RuntimeError('OTA face is not ready on Wi-Fi')
    host = urlparse(before.get('wifiUrl', '')).hostname
    if host is None or not ipaddress.ip_address(host).is_private:
        raise RuntimeError('No private Wi-Fi address reported by Astrolabe')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((host, 80))
        local_ip = route.getsockname()[0]
    token_path = '/' + secrets.token_urlsafe(24) + '/firmware.bin'
    transferred = threading.Event()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            if self.path != token_path or self.client_address[0] != host:
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Content-Length', str(firmware.stat().st_size))
            self.end_headers()
            try:
                with firmware.open('rb') as source:
                    for block in iter(lambda: source.read(65536), b''):
                        self.wfile.write(block)
                transferred.set()
            except (BrokenPipeError, ConnectionResetError):
                pass

        def log_message(self, *args):
            pass

    server = ThreadingHTTPServer((local_ip, 0), Handler)
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    try:
        url = f'http://{local_ip}:{server.server_port}{token_path}'
        print(f'Serving verified Cyber image to Astrolabe at {host}', flush=True)
        call({'op': 'write', 'address': address, 'characteristic': CONTROL,
              'payload': {'key': key, 'ota': {'url': url, 'sha256': expected_sha}}})
        deadline = time.monotonic() + 600
        last = {}
        while time.monotonic() < deadline:
            time.sleep(5)
            try:
                last = status(address)
            except Exception:
                continue  # Reboot interrupts BLE; reconnect on the next poll.
            if verified_update(before, last, transferred.is_set(), elf_sha):
                return {'ota_verified': True, 'sha256': expected_sha, 'device': last}
            print(json.dumps({k: last.get(k) for k in ('face', 'otaActive', 'otaLast')}), flush=True)
        raise RuntimeError(f'OTA not verified within 10 minutes; transferred={transferred.is_set()}, last={last}')
    finally:
        server.shutdown()
        server.server_close()
        serving.join(timeout=2)
