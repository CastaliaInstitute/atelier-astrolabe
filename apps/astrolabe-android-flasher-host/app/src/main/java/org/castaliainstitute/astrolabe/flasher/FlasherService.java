package org.castaliainstitute.astrolabe.flasher;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.IBinder;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;

public final class FlasherService extends Service {
    private static final int PORT = 8765;
    private static final int NOTIFICATION_ID = 175;
    private static final String CHANNEL_ID = "astrolabe-flasher";
    private static final String USB_PERMISSION_ACTION =
            "org.castaliainstitute.astrolabe.flasher.USB_PERMISSION";
    private static final int ASTROLABE_VENDOR_ID = 0x303a;
    private static final int RECONNECT_TIMEOUT_MS = 8000;

    private static volatile FlasherService instance;
    private static volatile boolean ready;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private ServerSocket serverSocket;
    private UsbManager usbManager;
    private UsbDeviceConnection usbConnection;
    private UsbInterface usbControlInterface;
    private UsbInterface usbDataInterface;
    private UsbEndpoint usbInputEndpoint;
    private UsbEndpoint usbOutputEndpoint;
    private int baudRate = 115200;
    private boolean reconnecting;

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                UsbDevice detached = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                if (detached != null && detached.getVendorId() == ASTROLABE_VENDOR_ID) {
                    synchronized (FlasherService.this) {
                        reconnecting = true;
                        closeUsbLocked();
                    }
                }
            } else if (UsbManager.ACTION_USB_DEVICE_ATTACHED.equals(action)) {
                requestUsbPermission(FlasherService.this);
            }
        }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
        usbManager = (UsbManager) getSystemService(Context.USB_SERVICE);
        IntentFilter filter = new IntentFilter();
        filter.addAction(USB_PERMISSION_ACTION);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        registerReceiver(usbReceiver, filter);
        startForeground(NOTIFICATION_ID, buildNotification());
        startServer();
        requestUsbPermission(this);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        requestUsbPermission(this);
        return START_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        ready = false;
        running.set(false);
        unregisterReceiver(usbReceiver);
        synchronized (this) {
            closeUsbLocked();
        }
        if (serverSocket != null) {
            try {
                serverSocket.close();
            } catch (IOException ignored) {
                // The accept loop exits when the socket closes.
            }
        }
        instance = null;
        super.onDestroy();
    }

    public static boolean isReady() {
        return ready;
    }

    public static boolean hasAstrolabe() {
        FlasherService service = instance;
        return service != null && service.findAstrolabe() != null;
    }

    public static boolean hasUsbPermission() {
        FlasherService service = instance;
        if (service == null) return false;
        UsbDevice device = service.findAstrolabe();
        return device != null && service.usbManager.hasPermission(device);
    }

    public static void requestUsbPermission(Context context) {
        FlasherService service = instance;
        if (service == null || service.usbManager == null) return;
        UsbDevice device = service.findAstrolabe();
        if (device == null || service.usbManager.hasPermission(device)) return;
        Intent permission = new Intent(USB_PERMISSION_ACTION).setPackage(context.getPackageName());
        PendingIntent permissionIntent = PendingIntent.getBroadcast(context, 0, permission, 0);
        service.usbManager.requestPermission(device, permissionIntent);
    }

    private Notification buildNotification() {
        NotificationManager manager = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            manager.createNotificationChannel(new NotificationChannel(
                    CHANNEL_ID, "Astrolabe flashing", NotificationManager.IMPORTANCE_LOW));
        }
        Intent launcher = new Intent(this, MainActivity.class);
        PendingIntent contentIntent = PendingIntent.getActivity(this, 0, launcher, 0);
        Notification.Builder builder = Build.VERSION.SDK_INT >= Build.VERSION_CODES.O
                ? new Notification.Builder(this, CHANNEL_ID)
                : new Notification.Builder(this);
        return builder
                .setSmallIcon(android.R.drawable.stat_sys_upload)
                .setContentTitle("Astrolabe Flasher ready")
                .setContentText("Android USB bridge is active")
                .setOngoing(true)
                .setContentIntent(contentIntent)
                .build();
    }

    private void startServer() {
        if (!running.compareAndSet(false, true)) return;
        new Thread(() -> {
            try {
                serverSocket = new ServerSocket(PORT, 8, InetAddress.getByName("127.0.0.1"));
                ready = true;
                while (running.get()) {
                    Socket socket = serverSocket.accept();
                    new Thread(() -> serve(socket), "flasher-http-client").start();
                }
            } catch (IOException ignored) {
                ready = false;
            }
        }, "flasher-http-server").start();
    }

    private void serve(Socket socket) {
        try (Socket client = socket;
             BufferedInputStream input = new BufferedInputStream(client.getInputStream());
             BufferedOutputStream output = new BufferedOutputStream(client.getOutputStream())) {
            String requestLine = readLine(input);
            if (requestLine == null) return;
            String[] parts = requestLine.split(" ");
            if (parts.length < 2 || !("GET".equals(parts[0]) || "HEAD".equals(parts[0])
                    || "POST".equals(parts[0]))) {
                writeError(output, 405, "Method Not Allowed");
                return;
            }
            int contentLength = 0;
            while (true) {
                String header = readLine(input);
                if (header == null || header.isEmpty()) break;
                int colon = header.indexOf(':');
                if (colon > 0 && "content-length".equalsIgnoreCase(header.substring(0, colon).trim())) {
                    contentLength = Integer.parseInt(header.substring(colon + 1).trim());
                }
            }

            String[] target = parts[1].split("\\?", 2);
            String path = URLDecoder.decode(target[0], "UTF-8");
            String query = target.length > 1 ? target[1] : "";
            if ("/".equals(path)) path = "/flasher/";
            if (path.endsWith("/")) path += "index.html";
            if (path.contains("..") || !path.startsWith("/")) {
                writeError(output, 403, "Forbidden");
                return;
            }

            if (path.startsWith("/bridge/native-serial/")) {
                byte[] requestBody = contentLength > 0 ? readExactly(input, contentLength) : new byte[0];
                serveNativeSerial(path, query, requestBody, output);
                return;
            }

            byte[] body;
            try (InputStream asset = getAssets().open("www" + path)) {
                body = readAll(asset);
            } catch (IOException missing) {
                writeError(output, 404, "Not Found");
                return;
            }
            String headers = "HTTP/1.1 200 OK\r\n"
                    + "Content-Type: " + contentType(path) + "\r\n"
                    + "Content-Length: " + body.length + "\r\n"
                    + "Cache-Control: no-store\r\n"
                    + "X-Content-Type-Options: nosniff\r\n"
                    + "Connection: close\r\n\r\n";
            output.write(headers.getBytes(StandardCharsets.US_ASCII));
            if ("GET".equals(parts[0])) output.write(body);
            output.flush();
        } catch (IOException ignored) {
            // Chrome may cancel speculative requests.
        }
    }

    private void serveNativeSerial(
            String path, String query, byte[] requestBody, BufferedOutputStream output) throws IOException {
        try {
            if (path.endsWith("/open")) {
                openUsb(queryInt(query, "baud", 115200), 0);
                writeResponse(output, "application/json", "{\"ok\":true}".getBytes(StandardCharsets.UTF_8));
            } else if (path.endsWith("/read")) {
                byte[] data = readUsb(
                        Math.min(queryInt(query, "length", 65536), 65536),
                        queryInt(query, "timeout", 1000));
                writeResponse(output, "application/octet-stream", data);
            } else if (path.endsWith("/write")) {
                writeUsb(requestBody, queryInt(query, "timeout", 5000));
                writeResponse(output, "application/json", "{\"ok\":true}".getBytes(StandardCharsets.UTF_8));
            } else if (path.endsWith("/signals")) {
                setUsbSignals(queryInt(query, "dtr", 0) != 0, queryInt(query, "rts", 0) != 0);
                writeResponse(output, "application/json", "{\"ok\":true}".getBytes(StandardCharsets.UTF_8));
            } else if (path.endsWith("/close")) {
                synchronized (this) {
                    closeUsbLocked();
                    reconnecting = false;
                }
                writeResponse(output, "application/json", "{\"ok\":true}".getBytes(StandardCharsets.UTF_8));
            } else {
                writeError(output, 404, "Not Found");
            }
        } catch (Exception error) {
            writeError(output, 500, error.getMessage() == null ? error.toString() : error.getMessage());
        }
    }

    private synchronized void openUsb(int requestedBaudRate, int waitMs) throws IOException {
        baudRate = requestedBaudRate;
        long deadline = System.currentTimeMillis() + Math.max(0, waitMs);
        IOException lastError = null;
        do {
            closeUsbLocked();
            UsbDevice device = findAstrolabe();
            if (device == null) {
                lastError = new IOException("Astrolabe USB device is not attached");
            } else if (!usbManager.hasPermission(device)) {
                requestUsbPermission(this);
                lastError = new IOException("Grant Astrolabe Flasher USB permission, then connect again");
            } else {
                try {
                    openDeviceLocked(device);
                    reconnecting = false;
                    return;
                } catch (IOException error) {
                    lastError = error;
                    closeUsbLocked();
                }
            }
            if (System.currentTimeMillis() >= deadline) break;
            try {
                wait(Math.min(100, Math.max(1, deadline - System.currentTimeMillis())));
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new IOException("Interrupted while waiting for Astrolabe USB", interrupted);
            }
        } while (true);
        throw lastError == null ? new IOException("Could not open Astrolabe USB") : lastError;
    }

    private void openDeviceLocked(UsbDevice device) throws IOException {
        for (int index = 0; index < device.getInterfaceCount(); index++) {
            UsbInterface candidate = device.getInterface(index);
            if (candidate.getInterfaceClass() == UsbConstants.USB_CLASS_COMM) {
                usbControlInterface = candidate;
            } else if (candidate.getInterfaceClass() == UsbConstants.USB_CLASS_CDC_DATA) {
                usbDataInterface = candidate;
            }
        }
        if (usbControlInterface == null || usbDataInterface == null) {
            throw new IOException("Astrolabe CDC interfaces were not found");
        }
        for (int index = 0; index < usbDataInterface.getEndpointCount(); index++) {
            UsbEndpoint endpoint = usbDataInterface.getEndpoint(index);
            if (endpoint.getDirection() == UsbConstants.USB_DIR_IN) usbInputEndpoint = endpoint;
            else usbOutputEndpoint = endpoint;
        }
        if (usbInputEndpoint == null || usbOutputEndpoint == null) {
            throw new IOException("Astrolabe CDC endpoints were not found");
        }
        usbConnection = usbManager.openDevice(device);
        if (usbConnection == null) throw new IOException("Could not open Astrolabe USB device");
        if (!usbConnection.claimInterface(usbControlInterface, true)
                || !usbConnection.claimInterface(usbDataInterface, true)) {
            throw new IOException("Could not claim Astrolabe CDC interface");
        }
        byte[] lineCoding = new byte[] {
                (byte) baudRate, (byte) (baudRate >> 8), (byte) (baudRate >> 16),
                (byte) (baudRate >> 24), 0, 0, 8
        };
        int result = usbConnection.controlTransfer(
                0x21, 0x20, 0, usbControlInterface.getId(), lineCoding, lineCoding.length, 1000);
        if (result < 0) throw new IOException("Could not configure Astrolabe baud rate");
    }

    private synchronized void ensureUsbAfterReset() throws IOException {
        if (usbConnection == null) openUsb(baudRate, RECONNECT_TIMEOUT_MS);
    }

    private synchronized byte[] readUsb(int length, int timeout) throws IOException {
        ensureUsbAfterReset();
        byte[] buffer = new byte[Math.max(1, length)];
        int count = usbConnection.bulkTransfer(usbInputEndpoint, buffer, buffer.length, timeout);
        if (count < 0 && reconnecting) {
            closeUsbLocked();
            openUsb(baudRate, RECONNECT_TIMEOUT_MS);
            return new byte[0];
        }
        if (count <= 0) return new byte[0];
        byte[] result = new byte[count];
        System.arraycopy(buffer, 0, result, 0, count);
        return result;
    }

    private synchronized void writeUsb(byte[] data, int timeout) throws IOException {
        ensureUsbAfterReset();
        int offset = 0;
        while (offset < data.length) {
            int block = Math.min(16 * 1024, data.length - offset);
            byte[] chunk = new byte[block];
            System.arraycopy(data, offset, chunk, 0, block);
            int count = usbConnection.bulkTransfer(usbOutputEndpoint, chunk, chunk.length, timeout);
            if (count <= 0) {
                closeUsbLocked();
                throw new IOException("Astrolabe USB write failed");
            }
            offset += count;
        }
    }

    private synchronized void setUsbSignals(boolean dtr, boolean rts) throws IOException {
        if (usbConnection == null && reconnecting) return;
        ensureUsbAfterReset();
        int value = (dtr ? 1 : 0) | (rts ? 2 : 0);
        int result = usbConnection.controlTransfer(
                0x21, 0x22, value, usbControlInterface.getId(), null, 0, 1000);
        if (result < 0) {
            reconnecting = true;
            closeUsbLocked();
        }
    }

    private void closeUsbLocked() {
        if (usbConnection != null) {
            if (usbDataInterface != null) usbConnection.releaseInterface(usbDataInterface);
            if (usbControlInterface != null) usbConnection.releaseInterface(usbControlInterface);
            usbConnection.close();
        }
        usbConnection = null;
        usbControlInterface = null;
        usbDataInterface = null;
        usbInputEndpoint = null;
        usbOutputEndpoint = null;
    }

    private UsbDevice findAstrolabe() {
        if (usbManager == null) return null;
        for (Map.Entry<String, UsbDevice> entry : usbManager.getDeviceList().entrySet()) {
            UsbDevice device = entry.getValue();
            if (device.getVendorId() == ASTROLABE_VENDOR_ID) return device;
        }
        return null;
    }

    private static int queryInt(String query, String name, int fallback) {
        for (String entry : query.split("&")) {
            String[] pair = entry.split("=", 2);
            if (pair.length == 2 && name.equals(pair[0])) {
                try {
                    return Integer.parseInt(pair[1]);
                } catch (NumberFormatException ignored) {
                    return fallback;
                }
            }
        }
        return fallback;
    }

    private static String readLine(InputStream input) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        int current;
        while ((current = input.read()) != -1) {
            if (current == '\n') break;
            if (current != '\r') bytes.write(current);
            if (bytes.size() > 8192) throw new IOException("HTTP header too large");
        }
        if (current == -1 && bytes.size() == 0) return null;
        return bytes.toString("US-ASCII");
    }

    private static byte[] readAll(InputStream input) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        byte[] buffer = new byte[32 * 1024];
        int count;
        while ((count = input.read(buffer)) != -1) bytes.write(buffer, 0, count);
        return bytes.toByteArray();
    }

    private static byte[] readExactly(InputStream input, int length) throws IOException {
        if (length < 0 || length > 1024 * 1024) throw new IOException("Invalid request size");
        byte[] data = new byte[length];
        int offset = 0;
        while (offset < length) {
            int count = input.read(data, offset, length - offset);
            if (count < 0) throw new IOException("Incomplete request body");
            offset += count;
        }
        return data;
    }

    private static void writeResponse(
            BufferedOutputStream output, String contentType, byte[] body) throws IOException {
        String headers = "HTTP/1.1 200 OK\r\n"
                + "Content-Type: " + contentType + "\r\n"
                + "Content-Length: " + body.length + "\r\n"
                + "Cache-Control: no-store\r\n"
                + "Connection: close\r\n\r\n";
        output.write(headers.getBytes(StandardCharsets.US_ASCII));
        output.write(body);
        output.flush();
    }

    private static void writeError(BufferedOutputStream output, int code, String message)
            throws IOException {
        byte[] body = message.getBytes(StandardCharsets.UTF_8);
        String headers = String.format(
                Locale.US,
                "HTTP/1.1 %d Error\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                code,
                body.length);
        output.write(headers.getBytes(StandardCharsets.US_ASCII));
        output.write(body);
        output.flush();
    }

    private static String contentType(String path) {
        String lower = path.toLowerCase(Locale.US);
        if (lower.endsWith(".html")) return "text/html; charset=utf-8";
        if (lower.endsWith(".js") || lower.endsWith(".mjs")) return "text/javascript; charset=utf-8";
        if (lower.endsWith(".css")) return "text/css; charset=utf-8";
        if (lower.endsWith(".json") || lower.endsWith(".webmanifest")) return "application/json; charset=utf-8";
        if (lower.endsWith(".png")) return "image/png";
        return "application/octet-stream";
    }
}
