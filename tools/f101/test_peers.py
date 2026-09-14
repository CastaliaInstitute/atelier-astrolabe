"""Unit coverage for F101's multiple-Astrolabe BLE registry."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import server


class PeerRegistryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.registry = Path(self.temp.name) / 'paired-astrolabes.json'
        self.registry_patch = patch.object(server, 'PAIRED_DEVICES', self.registry)
        self.registry_patch.start()
        self.addCleanup(self.registry_patch.stop)
        self.addCleanup(self.temp.cleanup)

    def test_pair_preserves_existing_device(self):
        with patch.object(server, 'ble_call', return_value={'ok': True, 'bonded': True}):
            server.astrolabe_ble_pair('A0:F2:62:E4:3F:12', 'desk')
            result = server.astrolabe_ble_pair('A4:CB:8F:D6:42:62', 'lab')
        self.assertEqual([peer['name'] for peer in result['peers']], ['desk', 'lab'])
        self.assertEqual(json.loads(self.registry.read_text())[1]['address'], 'A4:CB:8F:D6:42:62')

    def test_pair_replaces_only_matching_label(self):
        self.registry.write_text('[{"address":"A0:F2:62:E4:3F:12","name":"old"}]')
        with patch.object(server, 'ble_call', return_value={'ok': True, 'bonded': True}):
            result = server.astrolabe_ble_pair('a0:f2:62:e4:3f:12', 'new')
        self.assertEqual(result['peers'], [{'address': 'A0:F2:62:E4:3F:12', 'name': 'new'}])

    def test_fanout_returns_individual_failures(self):
        self.registry.write_text('[{"address":"A0:F2:62:E4:3F:12"},{"address":"A4:CB:8F:D6:42:62"}]')
        def select(address, face):
            if address.startswith('A4'):
                raise OSError('out of range')
            return {'face': face}
        with patch.object(server, 'astrolabe_ble_select_face', side_effect=select):
            result = server.astrolabe_ble_select_face_all('notes')
        self.assertEqual(result['results'][0]['status']['face'], 'notes')
        self.assertEqual(result['results'][1]['error'], 'out of range')


if __name__ == '__main__':
    unittest.main()
