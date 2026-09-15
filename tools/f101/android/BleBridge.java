package org.castalia.astrolabe;

import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.AttributionSource;
import android.app.Application;
import android.os.Looper;
import android.os.ParcelUuid;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** One bounded Android BLE operation, launched as Android shell by local MCP. */
public final class BleBridge {
    static final UUID SERVICE = UUID.fromString("01000000-5017-0065-6261-6c6f72747341");
    static final UUID SETTINGS = UUID.fromString("03000000-5017-0065-6261-6c6f72747341");
    static Context context;
    static BluetoothAdapter adapter;
    static BluetoothDevice controlDevice;
    static boolean confirmControlPairing;
    static final BlockingQueue<JSONObject> events = new LinkedBlockingQueue<>();

    static void event(String type, int status, String value) {
        try { events.offer(new JSONObject().put("event", type).put("status", status).put("value", value)); }
        catch (JSONException e) { throw new RuntimeException(e); }
    }

    static JSONObject await(String expected) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(12);
        while (System.nanoTime() < end) {
            if (confirmControlPairing && controlDevice != null && controlDevice.getBondState() == BluetoothDevice.BOND_BONDING)
                BluetoothDevice.class.getMethod("setPairingConfirmation", boolean.class).invoke(controlDevice, true);
            JSONObject e = events.poll(100, TimeUnit.MILLISECONDS);
            if (e == null) continue;
            if (e.getInt("status") != BluetoothGatt.GATT_SUCCESS) throw new IOException(e.toString());
            if (e.getString("event").equals(expected)) return e;
            if (e.getString("event").equals("disconnected")) throw new IOException("BLE disconnected");
        }
        throw new IOException("Timed out waiting for " + expected);
    }

    static final BluetoothGattCallback CALLBACK = new BluetoothGattCallback() {
        public void onConnectionStateChange(BluetoothGatt g, int status, int state) {
            event(state == BluetoothProfile.STATE_CONNECTED ? "connected" : "disconnected", status, "");
        }
        public void onServicesDiscovered(BluetoothGatt g, int status) { event("services", status, ""); }
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            byte[] value = c.getValue();
            event("read", status, value == null ? "" : new String(value, StandardCharsets.UTF_8));
        }
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            event("write", status, "");
        }
    };

    static void write(BluetoothGatt gatt, BluetoothGattCharacteristic c, byte[] value) throws Exception {
        c.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        c.setValue(value);
        if (!gatt.writeCharacteristic(c)) throw new IOException("BLE write not started");
        await("write");
    }

    static JSONObject run(JSONObject request) throws Exception {
        if (adapter == null || !adapter.isEnabled()) throw new IOException("Android Bluetooth is disabled");
        String op = request.getString("op");
        if (op.equals("scan")) {
            Map<String, JSONObject> found = new ConcurrentHashMap<>();
            BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
            if (scanner == null) throw new IOException("BLE scanner unavailable");
            ScanCallback callback = new ScanCallback() {
                public void onScanResult(int type, ScanResult result) {
                    try {
                        String name = result.getScanRecord() == null ? "" : result.getScanRecord().getDeviceName();
                        byte[] mfg = result.getScanRecord() == null ? null : result.getScanRecord().getManufacturerSpecificData(0xffff);
                        boolean astrolabe = mfg != null && mfg.length > 0 && (mfg[0] & 0xff) == 0xa7;
                        if (result.getScanRecord() != null && result.getScanRecord().getServiceUuids() != null)
                            astrolabe |= result.getScanRecord().getServiceUuids().contains(new ParcelUuid(SERVICE));
                        if (!astrolabe && (name == null || !(name.toLowerCase(Locale.ROOT).contains("astrolabe") || name.toLowerCase(Locale.ROOT).contains("lunasay")))) return;
                        found.put(result.getDevice().getAddress(), new JSONObject()
                            .put("address", result.getDevice().getAddress()).put("name", name)
                            .put("rssi", result.getRssi()).put("connectable", result.isConnectable()));
                    } catch (JSONException e) { event("scan-error", -1, e.toString()); }
                }
                public void onScanFailed(int code) { event("scan-error", code, "Scan failed"); }
            };
            try {
                // Existing firmware advertises manufacturer data and its name,
                // but omits the service UUID. Filter returned names in callback.
                scanner.startScan(Arrays.asList(new ScanFilter.Builder().setManufacturerData(0xffff,
                        new byte[] {(byte) 0xa7}, new byte[] {(byte) 0xff}).build(),
                        new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICE)).build()),
                    new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback);
                Thread.sleep(Math.max(1, Math.min(10, request.optInt("seconds", 6))) * 1000L);
                if (!events.isEmpty()) throw new IOException(events.poll().toString());
                return new JSONObject().put("ok", true).put("devices", new JSONArray(found.values()));
            } finally { scanner.stopScan(callback); }
        }
        BluetoothDevice device = adapter.getRemoteDevice(request.getString("address"));
        if (op.equals("confirm-pairing")) {
            boolean confirmed = (Boolean) BluetoothDevice.class.getMethod("setPairingConfirmation", boolean.class).invoke(device, true);
            return new JSONObject().put("ok", confirmed);
        }
        if (op.equals("bond")) {
            if (device.getBondState() == BluetoothDevice.BOND_NONE && !device.createBond())
                throw new IOException("Pairing not started");
            long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(35);
            while (device.getBondState() != BluetoothDevice.BOND_BONDED && System.nanoTime() < deadline) {
                Thread.sleep(500);
            }
            if (device.getBondState() != BluetoothDevice.BOND_BONDED) throw new IOException("Confirm Android pairing for Astrolabe");
            return new JSONObject().put("ok", true).put("bonded", true);
        }
        controlDevice = device;
        confirmControlPairing = op.equals("write") && request.optBoolean("confirmPairing", false);
        BluetoothGatt gatt = device.connectGatt(context, false, CALLBACK, BluetoothDevice.TRANSPORT_LE);
        if (gatt == null) throw new IOException("BLE connect not started");
        try {
            await("connected");
            if (!gatt.discoverServices()) throw new IOException("Service discovery not started");
            await("services");
            BluetoothGattService service = gatt.getService(SERVICE);
            if (service == null) throw new IOException("Astrolabe service absent");
            if (op.equals("inspect")) {
                JSONArray characteristics = new JSONArray();
                for (BluetoothGattCharacteristic c : service.getCharacteristics())
                    characteristics.put(c.getUuid().toString());
                return new JSONObject().put("ok", true).put("address", device.getAddress())
                    .put("characteristics", characteristics);
            }
            if (op.equals("read")) {
                UUID id = UUID.fromString(request.optString("characteristic", SETTINGS.toString()));
                BluetoothGattCharacteristic c = service.getCharacteristic(id);
                if (c == null || !gatt.readCharacteristic(c)) throw new IOException("Read unavailable");
                return new JSONObject().put("ok", true).put("address", device.getAddress())
                    .put("value", await("read").getString("value"));
            }
            if (!op.equals("write")) throw new IOException("Unknown BLE operation");
            BluetoothGattCharacteristic c = service.getCharacteristic(UUID.fromString(request.optString("characteristic", SETTINGS.toString())));
            if (c == null) throw new IOException("Settings characteristic absent");
            byte[] payload = request.getJSONObject("payload").toString().getBytes(StandardCharsets.UTF_8);
            if (payload.length > 1024) throw new IOException("Settings payload too large");
            write(gatt, c, "BEGIN".getBytes(StandardCharsets.UTF_8));
            // 20 bytes works at the default ATT MTU and the firmware's 95-byte limit.
            for (int offset = 0; offset < payload.length; offset += 20)
                write(gatt, c, Arrays.copyOfRange(payload, offset, Math.min(offset + 20, payload.length)));
            write(gatt, c, "END".getBytes(StandardCharsets.UTF_8));
            return new JSONObject().put("ok", true).put("address", device.getAddress()).put("writtenBytes", payload.length);
        } finally { gatt.disconnect(); gatt.close(); }
    }

    public static void main(String[] args) throws Exception {
        System.setOut(new PrintStream(new FileOutputStream(FileDescriptor.out), true, "UTF-8"));
        System.setErr(new PrintStream(new FileOutputStream(FileDescriptor.err), true, "UTF-8"));
        Looper.prepareMainLooper();
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object thread = activityThread.getMethod("systemMain").invoke(null);
        Context system = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
        Context shell = system.createPackageContext("com.android.shell", 0);
        context = new ContextWrapper(shell) {
            public String getPackageName() { return "com.android.shell"; }
            public String getOpPackageName() { return "com.android.shell"; }
            public AttributionSource getAttributionSource() {
                return new AttributionSource.Builder(android.os.Process.myUid()).setPackageName("com.android.shell").build();
            }
            public Context getApplicationContext() { return this; }
        };
        Application application = new Application();
        java.lang.reflect.Method attach = Application.class.getDeclaredMethod("attach", Context.class);
        attach.setAccessible(true);
        attach.invoke(application, context);
        java.lang.reflect.Field initial = activityThread.getDeclaredField("mInitialApplication");
        initial.setAccessible(true);
        initial.set(thread, application);
        Class<?> initializer = Class.forName("android.bluetooth.BluetoothFrameworkInitializer");
        if (initializer.getMethod("getBluetoothServiceManager").invoke(null) == null) {
            Class<?> manager = Class.forName("android.os.BluetoothServiceManager");
            initializer.getMethod("setBluetoothServiceManager", manager).invoke(null, manager.getConstructor().newInstance());
        }
        adapter = (BluetoothAdapter) BluetoothAdapter.class.getDeclaredMethod("createAdapter", AttributionSource.class)
            .invoke(null, context.getAttributionSource());
        new Thread(() -> {
            try {
                String line = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8)).readLine();
                JSONObject result = run(new JSONObject(line));
                System.out.println(result.toString());
                System.exit(0);
            } catch (Exception error) {
                try { System.out.println(new JSONObject().put("ok", false).put("error", error.toString())); }
                catch (JSONException ignored) { }
                System.exit(1);
            }
        }, "astrolabe-ble").start();
        Looper.loop();
    }
}
