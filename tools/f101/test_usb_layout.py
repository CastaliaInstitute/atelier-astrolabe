"""CDC console discovery must ignore the Cyber NCM data/control pair."""
import unittest
from usb_console import cdc_layout


def interface(number, kind, subclass=0):
    return bytes([9, 4, number, 0, 2, kind, subclass, 0, 0])


def endpoint(address):
    return bytes([7, 5, address, 2, 64, 0, 0])


class LayoutTests(unittest.TestCase):
    def test_cdc_and_ncm_composite(self):
        desc = bytes(18) + interface(0, 2, 2) + bytes([5, 0x24, 6, 0, 1])
        desc += interface(1, 10) + endpoint(0x81) + endpoint(1)
        desc += interface(2, 8, 6) + endpoint(0x82) + endpoint(2)
        desc += interface(3, 2, 13) + bytes([5, 0x24, 6, 3, 4])
        desc += interface(4, 10) + endpoint(0x83) + endpoint(3)
        self.assertEqual(cdc_layout(desc), (0, 1, 0x81, 1))
        with self.assertRaises(ValueError):
            cdc_layout(desc[:-2])


if __name__ == '__main__':
    unittest.main()
