"""Prepare an x86-64 UEFI live image locally; never opens a physical disk."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tarfile

STATE = Path(__file__).resolve().parents[1] / '.state' / 'boot-image'
ISO = STATE / 'iso'
PACKAGES = STATE / 'fetch-root/packages'
IMAGE = STATE / 'astrolabe-x86_64.img'
TREE = STATE / 'image-tree'
OVERLAY = STATE / 'overlay'


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def put(name, text, mode=0o644):
    p = OVERLAY / name
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text)
    p.chmod(mode)


def main():
    os.umask(0o077)
    if IMAGE.exists():
        raise SystemExit(f'Image already exists: {IMAGE}; preserve or remove it explicitly before rebuilding')
    for name in ('boot/vmlinuz-lts', 'boot/initramfs-lts', 'efi/boot/bootx64.efi'):
        if not (ISO / name).is_file():
            raise SystemExit('Extract the checksum-verified Alpine standard ISO first')
    TREE.mkdir(exist_ok=True)
    for name in ('boot', 'efi'):
        shutil.copytree(ISO / name, TREE / name, dirs_exist_ok=True)
    # Keep the publishers' signed indexes; do not generate an unsigned local index.
    for repo in ('main', 'community'):
        directory = TREE / ('apks-' + repo)
        (directory / 'x86_64').mkdir(parents=True, exist_ok=True)
        (directory / '.boot_repository').touch()
        shutil.copyfile(STATE / (repo + '-index.tar.gz'), directory / 'x86_64/APKINDEX.tar.gz')
        for package in PACKAGES.glob('*.apk'):
            shutil.copyfile(package, directory / 'x86_64' / package.name)
    key = STATE / 'control_ed25519'
    if not key.exists():
        run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-C', 'astrolabe-f101-control', '-f', key)
    host_key = STATE / 'guest_host_ed25519'
    if not host_key.exists():
        run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-C', 'astrolabe-live-host', '-f', host_key)
    put('etc/.default_boot_services', '')
    put('etc/hostname', 'astrolabe-live\n')
    put('etc/conf.d/sshd', 'sshd_disable_keygen="yes"\n')
    put('etc/apk/world', '\n'.join(('alpine-base', 'openssl', 'openssh', 'openssh-server-common-openrc', 'xorg-server', 'xf86-video-fbdev', 'xf86-input-libinput', 'xinit', 'xvfb', 'fluxbox', 'xterm', 'x11vnc')) + '\n')
    put('root/.ssh/authorized_keys', key.with_suffix('.pub').read_text(), 0o600)
    put('etc/ssh/ssh_host_ed25519_key', host_key.read_text(), 0o600)
    put('etc/ssh/ssh_host_ed25519_key.pub', host_key.with_suffix('.pub').read_text())
    put('etc/ssh/sshd_config', 'HostKey /etc/ssh/ssh_host_ed25519_key\nPermitRootLogin prohibit-password\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nPubkeyAuthentication yes\nAllowTcpForwarding yes\nSubsystem sftp internal-sftp\n')
    put('etc/shadow', 'root:*:20000:0:99999:7:::\n', 0o600)
    put('etc/network/interfaces', 'auto lo\niface lo inet loopback\nauto eth0\niface eth0 inet dhcp\n')
    put('etc/modules', 'cdc_ncm\n')
    put('etc/X11/xorg.conf.d/20-framebuffer.conf', 'Section "Device"\n Identifier "Astrolabe framebuffer"\n Driver "fbdev"\n Option "fbdev" "/dev/fb0"\nEndSection\n')
    put('etc/local.d/desktop.start', '''#!/bin/sh
# USB NCM normally appears as eth0; retry when the cable is attached after boot.
(while :; do
  for p in /sys/class/net/*; do
    [ -e "$p/device/driver" ] || continue
    [ "$(basename "$(readlink "$p/device/driver")")" = cdc_ncm ] || continue
    n=${p##*/}
    ip -4 addr show dev "$n" | grep -q 'inet ' || udhcpc -i "$n" -n -q -t 3
  done
  sleep 10
done) >/var/log/astrolabe-network.log 2>&1 &
export DISPLAY=:0
if [ -e /dev/fb0 ]; then
  Xorg :0 -nolisten tcp >/var/log/astrolabe-x.log 2>&1 &
else
  Xvfb :0 -screen 0 1024x768x24 -nolisten tcp >/var/log/astrolabe-x.log 2>&1 &
fi
(while [ ! -S /tmp/.X11-unix/X0 ]; do sleep 1; done
 fluxbox &
 xterm -geometry 100x30+20+20 &
 # VNC is reachable only through authenticated SSH forwarding.
 exec x11vnc -display :0 -localhost -forever -shared -nopw -rfbport 5900
) >/var/log/astrolabe-desktop.log 2>&1 &
''', 0o755)
    for service in ('sshd', 'networking', 'local'):
        p = OVERLAY / 'etc/runlevels/default' / service
        p.parent.mkdir(parents=True, exist_ok=True)
        if not p.is_symlink():
            p.symlink_to('/etc/init.d/' + service)
    with tarfile.open(TREE / 'astrolabe.apkovl.tar.gz', 'w:gz') as archive:
        for name in ('etc', 'root'):
            archive.add(OVERLAY / name, arcname=name)
    (TREE / 'boot/grub/grub.cfg').write_text('set timeout=2\nmenuentry "Astrolabe Linux" {\n linux /boot/vmlinuz-lts modules=loop,squashfs,sd-mod,usb-storage,cdc_ncm console=tty0 console=ttyS0\n initrd /boot/initramfs-lts\n}\n')
    # MBR with a removable-media EFI FAT32 partition starting at 1 MiB.
    size = 2 * 1024**3
    with IMAGE.open('xb') as stream:
        stream.truncate(size)
        mbr = bytearray(512)
        mbr[446:462] = struct.pack('<B3sB3sII', 0x80, b'\xfe\xff\xff', 0xef, b'\xfe\xff\xff', 2048, size // 512 - 2048)
        mbr[510:512] = b'\x55\xaa'
        stream.write(mbr)
    run('mkfs.vfat', '-F', '32', '-n', 'ASTROLABE', '--offset', '2048', IMAGE)
    for item in TREE.iterdir():
        run('mcopy', '-s', '-i', str(IMAGE) + '@@1048576', item, '::/')
    digest = hashlib.file_digest(IMAGE.open('rb'), 'sha256').hexdigest()
    (STATE / 'image.json').write_text(json.dumps({'image': str(IMAGE), 'sha256': digest, 'boot': 'x86_64 UEFI', 'sd_written': False}, indent=2))
    print(IMAGE)


if __name__ == '__main__':
    main()
