"""BLE via Android's Bluetooth stack, invoked locally from the Kali chroot."""
import json
import os
from pathlib import Path
import subprocess
from android_power import awake


def call(request):
    env = {key: value for key, value in os.environ.items() if not key.startswith('LD_')}
    pid = subprocess.check_output(['/usr/sbin/chroot', '/proc/1/root', '/system/bin/pidof', 'zygote64'], text=True).split()[0]
    allowed = {'ANDROID_ROOT', 'ANDROID_DATA', 'ANDROID_ART_ROOT', 'ANDROID_I18N_ROOT',
               'ANDROID_TZDATA_ROOT', 'ANDROID_STORAGE', 'BOOTCLASSPATH', 'DEX2OATBOOTCLASSPATH',
               'SYSTEMSERVERCLASSPATH'}
    for entry in Path(f'/proc/{pid}/environ').read_bytes().split(b'\0'):
        key, _, value = entry.partition(b'=')
        if key.decode(errors='replace') in allowed:
            env[key.decode()] = value.decode()
    env['CLASSPATH'] = '/data/local/tmp/astrolabe-ble/bridge.dex'
    command = ['/usr/sbin/chroot', '--userspec=2000:2000', '/proc/1/root',
               '/system/bin/app_process', '/system/bin', 'org.castalia.astrolabe.BleBridge']
    with awake():
        result = subprocess.run(command, env=env, input=json.dumps(request) + '\n',
                                text=True, capture_output=True, timeout=50)
    try:
        response = json.loads(result.stdout)
    except json.JSONDecodeError:
        raise RuntimeError(f'Android BLE bridge failed ({result.returncode}): {result.stderr[-2000:]}')
    if not response.get('ok'):
        raise RuntimeError(response.get('error', 'BLE failed'))
    return response


if __name__ == '__main__':
    import sys
    print(json.dumps(call(json.loads(sys.stdin.readline())), indent=2))
