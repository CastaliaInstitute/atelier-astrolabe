"""Linux usbfs CDC transport for F101 kernels without cdc_acm."""
import ctypes
import errno
import fcntl
import os
from pathlib import Path
import struct
import time
from android_power import awake


def cdc_layout(desc):
    interfaces = {}
    unions = {}
    current = None
    offset = 18
    while offset + 2 <= len(desc):
        size, kind = desc[offset:offset + 2]
        if size < 2 or offset + size > len(desc):
            raise ValueError('Malformed USB descriptor')
        item = desc[offset:offset + size]
        if kind == 4 and size >= 9:
            current = item[2]
            interfaces[current] = {'class': item[5], 'subclass': item[6], 'endpoints': []}
        elif kind == 0x24 and size >= 5 and item[2] == 6:
            unions[item[3]] = item[4]
        elif kind == 5 and size >= 7 and current is not None and item[3] & 3 == 2:
            interfaces[current]['endpoints'].append(item[2])
        offset += size
    controls = [n for n, item in interfaces.items() if item['class'] == 2 and item['subclass'] == 2]
    data_numbers = [unions.get(n, n + 1) for n in controls]
    data = [(n, item['endpoints']) for n, item in interfaces.items()
            if item['class'] == 10 and n in data_numbers]
    if len(controls) != 1 or len(data) != 1:
        raise ValueError('Expected one CDC control/data pair')
    number, endpoints = data[0]
    incoming = [ep for ep in endpoints if ep & 0x80]
    outgoing = [ep for ep in endpoints if not ep & 0x80]
    if len(incoming) != 1 or len(outgoing) != 1:
        raise ValueError('Expected one CDC bulk endpoint in each direction')
    return controls[0], number, incoming[0], outgoing[0]


def devices():
    found = []
    for path in sorted(Path('/dev/bus/usb').glob('*/*')):
        try:
            with path.open('rb', buffering=0) as stream:
                desc = stream.read(4096)
            if len(desc) >= 18:
                vid, pid = struct.unpack_from('<HH', desc, 8)
                if vid == 0x303a and pid in (0x1001, 0x8003):
                    found.append({'path': str(path), 'vid': f'{vid:04x}',
                                  'pid': f'{pid:04x}', 'descriptors': desc.hex()})
        except OSError:
            continue
    return found


class Console:
    def __init__(self, path=None):
        matches = devices()
        if path:
            matches = [d for d in matches if d['path'] == path]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one supported Astrolabe USB device, found {len(matches)}; select a USB path explicitly.')
        self.path = matches[0]['path']
        self.control_interface, self.data_interface, self.ep_in, self.ep_out = cdc_layout(bytes.fromhex(matches[0]['descriptors']))
        self.lock = open('/tmp/astrolabe-f101-usb.lock', 'a')
        try:
            fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            self.lock.close()
            raise RuntimeError('Astrolabe USB is busy with another operation')
        self.fd = -1
        self.claimed = []
        self.usb = ctypes.CDLL('libusb-1.0.so.0')
        self.usb.libusb_wrap_sys_device.argtypes = [ctypes.c_void_p, ctypes.c_ssize_t, ctypes.POINTER(ctypes.c_void_p)]
        self.usb.libusb_bulk_transfer.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
        self.usb.libusb_control_transfer.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_ubyte, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint]
        self.context = ctypes.c_void_p()
        self.handle = ctypes.c_void_p()
        self.awake = awake()
        self.awake.__enter__()
        try:
            self.fd = os.open(self.path, os.O_RDWR)
            self.usb.libusb_set_option(None, 2)  # No sysfs/udev discovery in Kali chroot.
            self.check_usb(self.usb.libusb_init(ctypes.byref(self.context)))
            self.check_usb(self.usb.libusb_wrap_sys_device(self.context, self.fd, ctypes.byref(self.handle)))
            # Claim control and data interfaces, leaving the JTAG interface alone.
            for number in (self.control_interface, self.data_interface):
                self.check_usb(self.usb.libusb_claim_interface(self.handle, number))
                self.claimed.append(number)
            if matches[0]['pid'] == '8003':
                self.control(0x22, 1)  # TinyUSB CDC requires DTR to send console output.
        except Exception:
            self.close()
            raise

    def read(self, size=4096, timeout_ms=100):
        buffer = ctypes.create_string_buffer(size)
        count = ctypes.c_int()
        result = self.usb.libusb_bulk_transfer(self.handle, self.ep_in, buffer, size, ctypes.byref(count), max(1, timeout_ms))
        if result != -7:
            self.check_usb(result)
        return buffer.raw[:count.value]

    @staticmethod
    def check_usb(result):
        if result < 0:
            raise OSError({-3: errno.EACCES, -4: errno.ENODEV, -6: errno.EBUSY,
                           -7: errno.ETIMEDOUT, -9: errno.EPIPE}.get(result, errno.EIO), f'libusb error {result}')
        return result

    def write(self, data):
        total = 0
        while total < len(data):
            block = data[total:total + 4096]
            buffer = ctypes.create_string_buffer(block)
            written = ctypes.c_int()
            self.check_usb(self.usb.libusb_bulk_transfer(self.handle, self.ep_out, buffer,
                           len(block), ctypes.byref(written), 2000))
            count = written.value
            if count == 0:
                raise TimeoutError('USB write made no progress')
            total += count
        return total

    def control(self, request, value=0, data=b''):
        buffer = ctypes.create_string_buffer(data)
        return self.check_usb(self.usb.libusb_control_transfer(self.handle, 0x21, request,
                              value, self.control_interface, buffer, len(data), 1000))

    def collect(self, seconds, limit=65536):
        deadline = time.monotonic() + seconds
        data = bytearray()
        while time.monotonic() < deadline and len(data) < limit:
            try:
                chunk = self.read(min(4096, limit - len(data)))
            except OSError as error:
                if error.errno == errno.ENODEV and data:
                    return bytes(data), True  # Preserve acknowledgement before a reboot.
                raise
            data.extend(chunk)
            if not chunk:
                time.sleep(.01)
        return bytes(data), len(data) >= limit

    def command(self, command, seconds=2):
        if not command or len(command) > 256 or any(ord(c) < 32 for c in command):
            raise ValueError('Send one printable console command, up to 256 characters')
        self.collect(.2, 16384)
        self.write((command + '\n').encode())
        raw, truncated = self.collect(seconds)
        return {'device': self.path, 'command': command, 'output': raw.decode(errors='replace'),
                'truncated': truncated, 'received_bytes': len(raw),
                'note': 'Raw firmware output may include asynchronous logs; receipt is not proof of command success.'}

    def close(self):
        if self.fd >= 0:
            for number in reversed(self.claimed):
                self.usb.libusb_release_interface(self.handle, number)
            if self.handle.value:
                self.usb.libusb_close(self.handle)
                self.handle = ctypes.c_void_p()
            if self.context.value:
                self.usb.libusb_exit(self.context)
                self.context = ctypes.c_void_p()
            os.close(self.fd)
            self.fd = -1
        if not self.lock.closed:
            self.lock.close()
            self.awake.__exit__(None, None, None)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


if __name__ == '__main__':
    import argparse
    import json
    parser = argparse.ArgumentParser()
    parser.add_argument('command', nargs='?', default='qa status')
    parser.add_argument('--seconds', type=float, default=2)
    parser.add_argument('--device')
    args = parser.parse_args()
    with Console(args.device) as console:
        print(json.dumps(console.command(args.command, args.seconds), indent=2))
