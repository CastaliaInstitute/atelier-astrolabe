"""Persistent build/flash worker. Logs and result survive the MCP process."""
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import struct
import re
from contextlib import nullcontext
from android_power import awake

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
STATE = HERE / '.state'
BUILD = ROOT / 'astrolabe185b/build'


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def run(args, timeout=3600):
    subprocess.run(args, cwd=ROOT, check=True, timeout=timeout)


def main(action, directory, device=None):
    os.umask(0o077)
    with (STATE / 'firmware.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        stamp = STATE / 'cyber-build.json'
        if action == 'build':
            from provision import provision
            provision()
            stamp.unlink(missing_ok=True)
            run(['bash', str(HERE / 'build.sh')])
            metadata = json.loads((BUILD / 'flasher_args.json').read_text())
            hashes = {name: sha256(BUILD / name) for name in metadata['flash_files'].values()}
            hashes['astrolabe185b.elf'] = sha256(BUILD / 'astrolabe185b.elf')
            stamp.write_text(json.dumps({'target': 'astrolabe185b', 'variant': 'cyber', 'sha256': hashes, 'time': time.time()}))
            return {'firmware': str(BUILD / 'astrolabe185b.bin'), 'sha256': hashes}
        if action not in ('flash', 'ota'):
            raise ValueError('Unknown action')
        receipt = json.loads(stamp.read_text())
        if receipt['variant'] != 'cyber' or receipt.get('target') != 'astrolabe185b':
            raise RuntimeError('Build receipt is not Cyber')
        for name, expected in receipt['sha256'].items():
            if sha256(BUILD / name) != expected:
                raise RuntimeError(f'Build changed since verification: {name}')
        if action == 'ota':
            from ota_update import install
            return install(device, BUILD / 'astrolabe185b.bin', receipt['sha256']['astrolabe185b.bin'],
                           receipt['sha256']['astrolabe185b.elf'], (STATE / 'control-key').read_text().strip())
        os.environ['PYTHONPATH'] = os.pathsep.join((str(ROOT), str(HERE)))
        base = [sys.executable, str(HERE / 'esptool_usbfs.py'), '--chip', 'esp32s3',
                '--port', 'usbfs://' + device, '--before', 'usb_reset', '--no-stub']
        # Stay in ROM between reads: the installed application may hide its USB port.
        read_base = base + ['--after', 'no_reset']
        partition_backup = directory / 'partitions.bin'
        identity = json.loads((STATE / 'board.json').read_text())
        if identity.get('board') != '185b':
            raise RuntimeError('Missing verified 1.85B identity')
        probe = subprocess.run(read_base + ['read_flash', '0x8000', '0x1000', str(partition_backup)],
                               cwd=ROOT, capture_output=True, text=True, timeout=120)
        print(probe.stdout, probe.stderr, flush=True)
        probe.check_returncode()
        mac = re.search(r'MAC: ([0-9a-f:]{17})', probe.stdout)
        if mac is None or mac[1] != identity.get('mac'):
            raise RuntimeError('ROM MAC does not match the previously identified 1.85B; refusing flash')
        partitions = {}
        data = partition_backup.read_bytes()
        for offset in range(0, len(data), 32):
            entry = data[offset:offset + 32]
            if entry[:2] != b'\xaa\x50':
                break
            _, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', entry)
            partitions[label.rstrip(b'\0').decode()] = (kind, subtype, start, size)
        existing = partitions.get('factory')
        if existing not in ((0, 0, 0x20000, 0x300000), (0, 0, 0x20000, 0x900000)):
            raise RuntimeError('Unexpected existing application layout; refusing flash')
        new_data = (BUILD / 'partition_table/partition-table.bin').read_bytes()
        new_partitions = {}
        for offset in range(0, len(new_data), 32):
            entry = new_data[offset:offset + 32]
            if entry[:2] != b'\xaa\x50': break
            _, kind, subtype, start, size, label, flags = struct.unpack('<HBBII16sI', entry)
            new_partitions[label.rstrip(b'\0').decode()] = (kind, subtype, start, size)
        if new_partitions.get('factory') != (0, 0, 0x20000, 0x900000):
            raise RuntimeError('Build is not the 9MB USB-only layout; rebuild before flashing')
        for name, entry in partitions.items():
            if entry[0] == 1 and new_partitions.get(name) != entry:
                raise RuntimeError(f'Data partition would change: {name}')
        backup = directory / 'application-backup.bin'
        run(read_base + ['read_flash', '0x20000', hex(existing[3]), str(backup)], 1800)
        if backup.stat().st_size != existing[3]:
            raise RuntimeError('Incomplete application backup; refusing flash')
        backup_hash = sha256(backup)
        (directory / 'backup.sha256').write_text(backup_hash + '  application-backup.bin\n')
        if (BUILD / 'astrolabe185b.bin').stat().st_size > 0x900000:
            raise RuntimeError('Cyber image does not fit 9MB application partition')
        run(base + ['write_flash', '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '16MB',
                    '0x8000', str(BUILD / 'partition_table/partition-table.bin'),
                    '0x20000', str(BUILD / 'astrolabe185b.bin')], 1800)
        return {'backup': str(backup), 'backup_sha256': backup_hash,
                'partition_backup': str(partition_backup),
                'note': 'USB-only Cyber written to 9MB factory partition; NVS and data offsets retained. Verify BLE after boot.'}



if __name__ == '__main__':
    directory = Path(sys.argv[2])
    try:
        with awake() if sys.argv[1] in ('flash', 'ota') else nullcontext():
            details = main(sys.argv[1], directory, sys.argv[3] if len(sys.argv) > 3 else None)
        result = {'status': 'complete', 'exit_code': 0, **details}
    except Exception as error:
        print(f'{type(error).__name__}: {error}', flush=True)
        result = {'status': 'failed', 'error': str(error), 'exit_code': 1}
    (directory / 'result.json').write_text(json.dumps(result, indent=2))
    sys.exit(result['exit_code'])
