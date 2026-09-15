"""A damaged ROM response must never become an accepted firmware backup."""
import hashlib
import unittest
from unittest.mock import Mock, patch
from esptool.util import FatalError
import esptool_usbfs


class BackupTests(unittest.TestCase):
    def test_bad_chunk_is_retried_and_complete_backup_verified(self):
        good = b'firmware block'
        rom = Mock(IS_STUB=False)
        rom.flash_md5sum.return_value = hashlib.md5(good).hexdigest()
        with patch.object(esptool_usbfs, '_original_read_flash', side_effect=[b'corrupt', good]) as read:
            self.assertEqual(esptool_usbfs.verified_read_flash(rom, 0x20000, len(good)), good)
            self.assertEqual(read.call_count, 2)
        rom.flush_input.assert_called_once()
        self.assertEqual(rom.flash_md5sum.call_count, 2)

    def test_repeated_corruption_is_rejected(self):
        rom = Mock(IS_STUB=False)
        rom.flash_md5sum.return_value = hashlib.md5(b'good').hexdigest()
        with patch.object(esptool_usbfs, '_original_read_flash', return_value=b'bad!') as read:
            with self.assertRaises(FatalError):
                esptool_usbfs.verified_read_flash(rom, 0x20000, 4)
            self.assertEqual(read.call_count, 3)


if __name__ == '__main__':
    unittest.main()
