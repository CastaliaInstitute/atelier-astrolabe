"""Run esptool with F101's userspace USB serial transport registered."""
import hashlib
import serial
import esptool
from esptool.loader import ESPLoader
from esptool.util import FatalError

serial.protocol_handler_packages.append('tools.f101')
_original_read_flash = ESPLoader.read_flash


def verified_read_flash(self, offset, length, progress_fn=None):
    if self.IS_STUB:
        return _original_read_flash(self, offset, length, progress_fn)
    data = bytearray()
    while len(data) < length:
        start = offset + len(data)
        size = min(65536, length - len(data))
        for attempt in range(3):
            try:
                block = _original_read_flash(self, start, size)
                if len(block) != size or self.flash_md5sum(start, size).lower() != hashlib.md5(block).hexdigest():
                    raise FatalError('ROM flash backup length/MD5 verification failed')
                break
            except FatalError:
                if attempt == 2:
                    raise
                print(f'Retrying verified ROM read at {start:#x}', flush=True)
                self.flush_input()
        data.extend(block)
        if progress_fn:
            progress_fn(len(data), length)
    if self.flash_md5sum(offset, length).lower() != hashlib.md5(data).hexdigest():
        raise FatalError('Whole ROM backup MD5 verification failed')
    return bytes(data)


ESPLoader.read_flash = verified_read_flash

if __name__ == '__main__':
    esptool._main()
