"""Local face companion. Device observations never cause writes back to BLE."""
import argparse
import json
from pathlib import Path
import secrets
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from server import astrolabe_ble_status, astrolabe_ble_select_face, ble_address

FACES = ['pocketwatch', 'moon', 'astrology', 'synastry', 'transits', 'sky',
         'almanac', 'phenology', 'solar', 'magnetosphere', 'settings']

class Sync:
    def __init__(self, read, select):
        self.read, self.select = read, select
        self.lock = threading.Lock()
        self.state = dict(face=None, connected=False, pending=None, error=None, revision=0)
        self.seen = 0
        self.wake = threading.Event()

    def snapshot(self):
        with self.lock:
            self.seen = time.monotonic()
            return dict(self.state, faces=FACES)

    def request(self, face):
        if face not in FACES:
            raise ValueError('Face is not in the companion carousel')
        with self.lock:
            if not self.state['connected']:
                raise ValueError('Wait for Astrolabe to reconnect')
            if self.state['pending'] is not None:
                raise ValueError('A face change is already pending')
            self.state.update(pending=face, error=None)
        self.wake.set()

    def step(self):
        with self.lock:
            target = self.state['pending']
        try:
            status = self.select(target) if target is not None else self.read()
            if target is not None and status.get('face') != target:
                raise ValueError('Astrolabe did not confirm the requested face')
            with self.lock:
                self.state.update(face=status['face'], connected=True,
                                  pending=None if target is not None else self.state['pending'], error=None,
                                  revision=self.state['revision'] + 1)
        except Exception as error:
            # Discard failed intent: reconnect must never replay an old swipe.
            with self.lock:
                self.state.update(connected=False, pending=None, error=str(error))

    def run(self):
        while True:
            if time.monotonic() - self.seen < 20:
                self.step()
            self.wake.wait(1)
            self.wake.clear()


def serve(address, port):
    sync = Sync(lambda: astrolabe_ble_status(address),
                lambda face: astrolabe_ble_select_face(address, face))
    token = secrets.token_urlsafe(32)
    origin = f'http://127.0.0.1:{port}'
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass
        def send(self, code, body, content_type='application/json'):
            data = body.encode()
            self.send_response(code)
            self.send_header('Content-Type', content_type)
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Frame-Options', 'DENY')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            self.wfile.write(data)
        def authorized(self):
            return self.headers.get('Host') == f'127.0.0.1:{port}'
        def do_GET(self):
            if not self.authorized():
                return self.send(403, '{}')
            if self.path == '/':
                return self.send(200, (HERE/'index.html').read_text().replace('__TOKEN__', token), 'text/html; charset=utf-8')
            if self.path == '/state':
                return self.send(200, json.dumps(sync.snapshot()))
            self.send(404, '{}')
        def do_POST(self):
            if (not self.authorized() or self.headers.get('Origin') != origin or
                    self.headers.get('X-Sync-Token') != token):
                return self.send(403, '{}')
            if self.path != '/face':
                return self.send(404, '{}')
            try:
                size = int(self.headers.get('Content-Length', '0'))
                if not 0 < size <= 128:
                    raise ValueError('Invalid request length')
                sync.request(json.loads(self.rfile.read(size))['face'])
                self.send(202, json.dumps(sync.snapshot()))
            except (ValueError, KeyError) as error:
                self.send(409, json.dumps(dict(error=str(error))))
    threading.Thread(target=sync.run, daemon=True).start()
    print(origin, flush=True)
    ThreadingHTTPServer(('127.0.0.1', port), Handler).serve_forever()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--address', required=True, type=ble_address)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    serve(args.address, args.port)
