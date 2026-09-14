# Astrolabe x86-64 live image

The local image is `../.state/boot-image/astrolabe-x86_64.img`: 2GB, one FAT32
EFI partition, Alpine 3.24.1 x86-64 kernel and signed offline package repositories.
It is a regular file; no removable SD card has been written.

QEMU on F101 booted through UEFI to the Linux login prompt, installed all 142
offline packages, and verified the kernel modules. Pinned-key SSH login and the
MCP screenshot, click, type, and key-chord tools passed. A typed terminal command
and its output were visually verified in `mcp-screen.png`. Final startup
adjustments were checked in the running guest and saved into the image. Physical
SD boot and access through the Astrolabe USB bridge remain unverified. The ISO's GRUB binary prints a missing-original-
volume-label warning before loading the custom menu and kernel.

The overlay enables DHCP, SSH public-key authentication, and an Xorg framebuffer desktop
with Fluxbox/xterm. Xvfb provides a virtual desktop when no framebuffer exists.
x11vnc listens only on localhost:5900, for access through an SSH tunnel. The
private controller key and the pinned guest host key are local to `.state`.
Root password login is disabled. This controls the live Linux session after boot;
it does not capture BIOS video or another operating system's display.

`build_image.py` assembles the image from these prepared inputs under
`../.state/boot-image/`:

- `iso/`, extracted from the checksum-verified Alpine standard x86-64 ISO.
- `fetch-root/packages/`, fetched with Alpine APK for x86-64 and signature-verified.
- `main-index.tar.gz` and `community-index.tar.gz`, original signed Alpine indexes.

The downloader used Alpine's official v3.24 repositories. The fetch helper is an
ARM64 Alpine 3.24.1 minirootfs so APK can run natively on F101. Its key directory
also contains the x86-64 keys from the verified ISO initramfs. Requested packages
are alpine-base, openssl, openssh, openssh-server-common-openrc, xorg-server,
xf86-video-fbdev, xf86-input-libinput, xinit, xvfb, fluxbox, xterm, and x11vnc,
including their recursive dependencies. Host build tools are Python, ssh-keygen,
mtools, and dosfstools. QEMU x86 and OVMF provide the emulator test.

`python3 tools/f101/linux/build_image.py` creates an image if none exists. It
never selects or opens a physical disk. Inspect the attached SD card and preserve
its contents before choosing how to install the image.

To connect after the target has an address reachable from F101, use the generated
controller key with SSH and forward local port 5901 to guest localhost:5900.
Pin the host key from `guest_host_ed25519.pub`; do not disable host-key checking.
The Astrolabe network path still needs validation/configuration before a physical
target is reachable through it.

The Codex MCP tools are `astrolabe_target_screen`, `astrolabe_target_click`,
`astrolabe_target_type`, and `astrolabe_target_keys`. Pass the authorized target's
reachable SSH host and port. They authenticate with the image's controller key
and verify its pinned host key; each returns a PNG screenshot. The target's VNC
port stays bound to loopback. A CLI equivalent is:

```sh
python3 tools/f101/linux/control.py TARGET --output /absolute/path/screen.png
```

`--click X Y` clicks before capture; `--key 0xff0d` presses Return. The isolated
QEMU validation used SSH forwarding on 127.0.0.1:22222 and `test_kvm.py`. The VM is
shut down after validation. x86 emulation on F101's ARM processor boots slowly.
