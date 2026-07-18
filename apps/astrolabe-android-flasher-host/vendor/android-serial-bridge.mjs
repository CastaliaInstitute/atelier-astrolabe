class AndroidSerialPort {
  constructor() {
    this.readable = null;
    this.writable = null;
    this.closed = true;
    this.dtr = false;
    this.rts = false;
  }

  getInfo() {
    return { usbVendorId: 0x303a, usbProductId: 0x1001 };
  }

  async open(options = {}) {
    const baudRate = options.baudRate || 115200;
    const response = await fetch(`/bridge/native-serial/open?baud=${baudRate}`, {
      method: "POST",
    });
    if (!response.ok) throw new Error(await response.text());
    this.closed = false;
    this.readable = new ReadableStream({
      pull: async (controller) => {
        if (this.closed) {
          controller.close();
          return;
        }
        const read = await fetch("/bridge/native-serial/read?length=65536&timeout=20");
        if (!read.ok) throw new Error(await read.text());
        const data = new Uint8Array(await read.arrayBuffer());
        if (data.length) controller.enqueue(data);
      },
      cancel: () => {
        this.closed = true;
      },
    });
    this.writable = new WritableStream({
      write: async (chunk) => {
        const response = await fetch("/bridge/native-serial/write?timeout=5000", {
          method: "POST",
          headers: { "Content-Type": "application/octet-stream" },
          body: chunk,
        });
        if (!response.ok) throw new Error(await response.text());
      },
    });
  }

  async setSignals(signals = {}) {
    if (Object.prototype.hasOwnProperty.call(signals, "dataTerminalReady")) {
      this.dtr = Boolean(signals.dataTerminalReady);
    }
    if (Object.prototype.hasOwnProperty.call(signals, "requestToSend")) {
      this.rts = Boolean(signals.requestToSend);
    }
    const response = await fetch(
      `/bridge/native-serial/signals?dtr=${this.dtr ? 1 : 0}&rts=${this.rts ? 1 : 0}`,
      {
      method: "POST",
      },
    );
    if (!response.ok) throw new Error(await response.text());
  }

  async close() {
    this.closed = true;
    this.readable = null;
    this.writable = null;
    await fetch("/bridge/native-serial/close", { method: "POST" });
  }
}

export const serial = {
  async requestPort() {
    return new AndroidSerialPort();
  },
  async getPorts() {
    return [new AndroidSerialPort()];
  },
};
