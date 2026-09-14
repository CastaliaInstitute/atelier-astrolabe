"""Prevent reporting success for an unchanged slot or incomplete install."""
import unittest
from ota_update import verified_update


class VerificationTests(unittest.TestCase):
    def test_reboot_into_expected_image(self):
        before = {'partition': 'ota_0'}
        after = {'partition': 'ota_1', 'elfSha256': 'expected', 'otaActive': False}
        self.assertTrue(verified_update(before, after, True, 'expected'))
        for change in ({'partition': 'ota_0'}, {'partition': ''},
                       {'elfSha256': 'old'}, {'otaActive': True}, {'otaActive': None}):
            with self.subTest(change=change):
                self.assertFalse(verified_update(before, dict(after, **change), True, 'expected'))
        self.assertFalse(verified_update(before, after, False, 'expected'))
        self.assertFalse(verified_update({}, after, True, 'expected'))


if __name__ == '__main__':
    unittest.main()
