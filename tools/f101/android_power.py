"""Keep F101 awake during a bounded hardware operation."""
from contextlib import contextmanager
import os
from pathlib import Path
import uuid


@contextmanager
def awake():
    power = Path('/proc/1/root/sys/power')
    name = f'astrolabe-codex-{os.getpid()}-{uuid.uuid4().hex}'
    held = False
    try:
        if (power / 'wake_lock').exists():
            (power / 'wake_lock').write_text(name + ' 1800000000000')
            held = True
        yield
    finally:
        if held:
            (power / 'wake_unlock').write_text(name)
