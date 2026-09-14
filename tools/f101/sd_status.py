"""Read-only discovery of block devices behind Astrolabe's USB composite."""
import os
from pathlib import Path
import stat
import subprocess


def inspect():
    root = Path('/proc/1/root')
    found = []
    for entry in sorted((root / 'sys/class/block').glob('*')):
        # Normalize relative sysfs links lexically: resolving /proc/1/root with
        # realpath would escape the Android mount namespace into the chroot.
        target = Path(os.path.normpath(str(entry.parent / os.readlink(entry))))
        usb = None
        for parent in target.parents:
            if (parent / 'idVendor').exists():
                if (parent / 'idVendor').read_text().strip() == '303a' and \
                   (parent / 'idProduct').read_text().strip() == '8003':
                    usb = parent.name
                break
        if usb is None:
            continue
        node = root / 'dev/block' / entry.name
        if not node.exists() or not stat.S_ISBLK(node.stat().st_mode):
            continue
        probe = subprocess.run(['blkid', '-p', '-o', 'export', str(node)],
                               capture_output=True, text=True, timeout=10)
        metadata = dict(line.split('=', 1) for line in probe.stdout.splitlines() if '=' in line)
        found.append({'device': str(node), 'usb_port': usb,
                      'bytes': int((entry / 'size').read_text()) * 512,
                      'partition': (entry / 'partition').exists(), 'metadata': metadata})
    return {'block_devices': found,
            'note': 'Read-only probes. Select linux over BLE to expose the SD card; no filesystem is mounted or formatted.'}
