"""pySerial URL handler: usbfs:///dev/bus/usb/001/002."""
import struct
import time
from serial.serialutil import SerialBase, SerialException
from usb_console import Console


class Serial(SerialBase):
    def open(self):
        if self.is_open:
            raise SerialException('Already open')
        self.transport = Console(self.port.removeprefix('usbfs://'))
        self.is_open = True
        self._buffer = bytearray()
        self._reconfigure_port()

    def close(self):
        if self.is_open:
            self.transport.close()
            self.is_open = False

    def _reconfigure_port(self):
        if self.is_open:
            self.transport.control(0x20, data=struct.pack('<IBBB', self.baudrate, 0, 0, 8))

    def _update_dtr_state(self):
        self._update_lines()

    def _update_rts_state(self):
        self._update_lines()

    def _update_lines(self):
        if self.is_open:
            self.transport.control(0x22, int(self._dtr_state) | (int(self._rts_state) << 1))

    def read(self, size=1):
        deadline = None if self.timeout is None else time.monotonic() + self.timeout
        while len(self._buffer) < size:
            remaining = 100 if deadline is None else max(1, min(100, int((deadline - time.monotonic()) * 1000)))
            self._buffer.extend(self.transport.read(64, remaining))
            if deadline is not None and time.monotonic() >= deadline:
                break
        data = bytes(self._buffer[:size])
        del self._buffer[:size]
        return data

    def write(self, data):
        return self.transport.write(bytes(data))

    @property
    def in_waiting(self):
        self._buffer.extend(self.transport.read(64, 20))
        return len(self._buffer)

    def reset_input_buffer(self):
        self._buffer.clear()
        self.transport.collect(.05, 65536)

    def reset_output_buffer(self):
        pass

    def flush(self):
        pass

    def _update_break_state(self):
        raise SerialException('USB break is not implemented')
