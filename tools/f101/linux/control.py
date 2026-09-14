"""Bounded RFB control over an SSH tunnel to the generated live image."""
import json
import re
import select
import struct
import subprocess
import time
import zlib
from pathlib import Path

STATE = Path(__file__).resolve().parents[1] / '.state/boot-image'


def ssh_args(host, port):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.:-]{0,252}', host) or not 1 <= port <= 65535:
        raise ValueError('Invalid target host or SSH port')
    known = STATE / 'known_hosts'
    known.write_text('astrolabe-live ' + (STATE / 'guest_host_ed25519.pub').read_text())
    return ['ssh', '-o', 'BatchMode=yes', '-o', 'IdentitiesOnly=yes', '-o', 'ConnectTimeout=8',
            '-o', 'StrictHostKeyChecking=yes', '-o', 'HostKeyAlias=astrolabe-live',
            '-o', 'UserKnownHostsFile=' + str(known), '-i', str(STATE / 'control_ed25519'),
            '-p', str(port)]


class Desktop:
    def __init__(self, host, port=22):
        self.process = subprocess.Popen(ssh_args(host, port) + ['-W', 'localhost:5900', 'root@' + host],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, bufsize=0)
        self.deadline = time.monotonic() + 30
        try:
            if self.read(12) != b'RFB 003.008\n':
                raise RuntimeError('Expected RFB 3.8 from the live desktop')
            self.write(b'RFB 003.008\n')
            methods = self.read(self.read(1)[0])
            if 1 not in methods:
                raise RuntimeError('Expected the local RFB endpoint inside authenticated SSH')
            self.write(b'\x01')
            if self.read(4) != b'\0\0\0\0':
                raise RuntimeError('RFB security handshake failed')
            self.write(b'\x01')
            header = self.read(24)
            self.width, self.height = struct.unpack('>HH', header[:4])
            if not (0 < self.width <= 4096 and 0 < self.height <= 4096):
                raise RuntimeError('Unsupported desktop dimensions')
            name_size = struct.unpack('>I', header[20:])[0]
            if name_size > 4096:
                raise RuntimeError('Oversized desktop name')
            self.name = self.read(name_size).decode(errors='replace')
            self.write(b'\0\0\0\0' + struct.pack('>BBBBHHHBBBxxx', 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
            self.write(struct.pack('>BBHi', 2, 0, 1, 0))  # Raw rectangles only.
        except Exception:
            self.close()
            raise

    def read(self, size):
        result = bytearray()
        while len(result) < size:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.process.stdout], [], [], remaining)[0]:
                raise TimeoutError('Desktop response timeout')
            chunk = self.process.stdout.read(size - len(result))
            if not chunk:
                detail = ''
                if select.select([self.process.stderr], [], [], 1)[0]:
                    detail = self.process.stderr.read(4096).decode(errors='replace').strip()
                raise RuntimeError('Desktop SSH tunnel closed: ' + detail)
            result.extend(chunk)
        return bytes(result)

    def write(self, data):
        self.process.stdin.write(data)
        self.process.stdin.flush()

    def key(self, keysym):
        if not 0 <= keysym <= 0xffffffff:
            raise ValueError('Invalid X11 keysym')
        for down in (1, 0):
            self.write(struct.pack('>BBHI', 4, down, 0, keysym))

    def pointer(self, x, y, buttons=0):
        if not (0 <= x < self.width and 0 <= y < self.height and 0 <= buttons <= 255):
            raise ValueError('Pointer outside the desktop or invalid button mask')
        self.write(struct.pack('>BBHH', 5, buttons, x, y))

    def screenshot(self):
        self.write(struct.pack('>BBHHHH', 3, 0, 0, 0, self.width, self.height))
        pixels = bytearray(self.width * self.height * 3)
        while True:
            message = self.read(1)[0]
            if message == 2:  # Bell.
                continue
            if message == 3:  # Clipboard text; ignore with a strict bound.
                header = self.read(7)
                length = struct.unpack('>I', header[3:])[0]
                if length > 1024 * 1024:
                    raise RuntimeError('Oversized clipboard message')
                self.read(length)
                continue
            if message != 0:
                raise RuntimeError('Unexpected RFB message')
            count = struct.unpack('>H', self.read(3)[1:])[0]
            if count > 4096:
                raise RuntimeError('Too many desktop rectangles')
            for _ in range(count):
                x, y, w, h, encoding = struct.unpack('>HHHHi', self.read(12))
                if encoding != 0 or x + w > self.width or y + h > self.height:
                    raise RuntimeError('Invalid raw desktop rectangle')
                raw = self.read(w * h * 4)
                rgb = bytearray(w * h * 3)
                rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
                for row in range(h):
                    start = ((y + row) * self.width + x) * 3
                    pixels[start:start + w * 3] = rgb[row * w * 3:(row + 1) * w * 3]
            break
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        scan = b''.join(b'\0' + pixels[y*self.width*3:(y+1)*self.width*3] for y in range(self.height))
        return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', self.width, self.height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b'')

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host')
    parser.add_argument('--port', type=int, default=22)
    parser.add_argument('--key', type=lambda value: int(value, 0))
    parser.add_argument('--click', type=int, nargs=2, metavar=('X', 'Y'))
    parser.add_argument('--output', type=Path, default=STATE / 'target-screen.png')
    args = parser.parse_args()
    with Desktop(args.host, args.port) as desktop:
        if args.key is not None:
            desktop.key(args.key)
        if args.click is not None:
            desktop.pointer(*args.click, buttons=1)
            desktop.pointer(*args.click, buttons=0)
        args.output.write_bytes(desktop.screenshot())
        print(json.dumps({'screen': str(args.output.resolve()), 'width': desktop.width,
                          'height': desktop.height, 'name': desktop.name}))
