"""Initialize the local BLE key without replacing existing firmware secrets."""
import os
from pathlib import Path
import re
import secrets


def provision():
    os.umask(0o077)
    here = Path(__file__).resolve().parent
    state = here / '.state'
    state.mkdir(mode=0o700, exist_ok=True)
    key_file = state / 'control-key'
    header = here.parent.parent / 'include/secrets.local.h'
    key = key_file.read_text().strip() if key_file.exists() else None
    if header.exists():
        matches = re.findall(r'^\s*#define\s+MYNAH_REMOTE_CONTROL_KEY\s+"([0-9a-f]{64})"\s*$', header.read_text(), re.M)
        if not matches or (key is not None and key != matches[-1]):
            raise RuntimeError('Existing secrets.local.h does not match the F101 control key; reconcile it before building.')
        key = matches[-1]
    else:
        key = key or secrets.token_hex(32)
        header.write_text('#pragma once\n#include "secrets.example.h"\n'
                          '#undef MYNAH_REMOTE_CONTROL_KEY\n'
                          f'#define MYNAH_REMOTE_CONTROL_KEY "{key}"\n')
    if not re.fullmatch('[0-9a-f]{64}', key):
        raise RuntimeError('Invalid local control key')
    if not key_file.exists():
        key_file.write_text(key + '\n')
    key_file.chmod(0o600)
    header.chmod(0o600)


if __name__ == '__main__':
    provision()
