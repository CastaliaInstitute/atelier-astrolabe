"""Firmware installation must reject a different board or build target."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch
import server
import worker


class TargetTests(unittest.TestCase):
    def test_pinned_board_survives_failed_touch_probe_but_other_boards_are_rejected(self):
        for mac, flash, allowed in [('a0:f2:62:e4:3f:10', 16, True),
                                    ('a4:cb:8f:d6:42:60', 16, False),
                                    ('a0:f2:62:e4:3f:10', 32, False)]:
            with self.subTest(mac=mac, flash=flash), tempfile.TemporaryDirectory() as directory:
                state = Path(directory)
                (state / 'board.json').write_text(json.dumps({'board': '185b', 'mac': 'a0:f2:62:e4:3f:10'}))
                console = Mock(path='/dev/bus/usb/001/002')
                console.command.side_effect = [
                    {'output': f'guess=unknown flash={flash}MB mac={mac}'},
                    {'output': 'running=factory'}]
                with patch.object(server, 'STATE', state), patch.object(server, 'Console') as opening, \
                     patch.object(server, 'devices', return_value=[]), patch.object(server, 'start_job') as start:
                    opening.return_value.__enter__.return_value = console
                    if allowed:
                        server.astrolabe_flash_cyber()
                        start.assert_called_once_with('flash', console.path)
                    else:
                        with self.assertRaises(RuntimeError):
                            server.astrolabe_flash_cyber()
                        start.assert_not_called()

    def test_old_175c_receipt_cannot_flash(self):
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory)
            (state / 'cyber-build.json').write_text(json.dumps({'variant': 'cyber', 'target': 'astrolabe175c'}))
            with patch.object(worker, 'STATE', state), patch.object(worker, 'run') as run:
                with self.assertRaises(RuntimeError):
                    worker.main('flash', state, '/dev/bus/usb/001/002')
                run.assert_not_called()



if __name__ == '__main__':
    unittest.main()
