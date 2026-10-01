package org.castalia.astrolabe;

import android.bluetooth.*;
import android.bluetooth.le.*;
import android.content.Context;
import android.content.ContextWrapper;
import android.os.Looper;
import android.os.ParcelUuid;
import org.json.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;

/** One bounded Android BLE operation against the Astrolabe audio bridge (docs/design/ble-audio-bridge.md).
 *  Android 8.1 compatible (R12mini): no AttributionSource/BluetoothFrameworkInitializer. */
public final class BleBridgeAudio {
    static final UUID AUDIO_SERVICE = UUID.fromString("10000000-5017-0065-6261-6c6f72747341");
    static final UUID MIC = UUID.fromString("11000000-5017-0065-6261-6c6f72747341");
    static final UUID TTS = UUID.fromString("12000000-5017-0065-6261-6c6f72747341");
    static final UUID CODEC = UUID.fromString("13000000-5017-0065-6261-6c6f72747341");
    static final UUID CONTROL = UUID.fromString("14000000-5017-0065-6261-6c6f72747341");
    static final UUID CREDIT = UUID.fromString("15000000-5017-0065-6261-6c6f72747341");
    static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");
    static Context context;
    static BluetoothAdapter adapter;
    static final BlockingQueue<JSONObject> events = new LinkedBlockingQueue<>();
    static final Object lock = new Object();
    static int mtu = 23;
    static volatile int micNotifications, micBytes, creditNotifications, seqGaps, seqWraps;
    static volatile int lastSeq = -1, codecBytes, ttsWrites, ttsWriteErrors;
    static volatile long firstNotifyNs, lastNotifyNs;
    static volatile int lastCredit = -1;
    static final Map<Integer, Integer> codecCounts = new ConcurrentHashMap<>();

    static void event(String type, int status, String value) {
        try { events.offer(new JSONObject().put("event", type).put("status", status).put("value", value)); }
        catch (JSONException e) { throw new RuntimeException(e); }
    }

    static JSONObject await(String expected, int timeoutS) throws Exception {
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(timeoutS);
        System.err.println("astrolabe: await " + expected);
        while (System.nanoTime() < end) {
            JSONObject e = events.poll(100, TimeUnit.MILLISECONDS);
            if (e == null) continue;
            System.err.println("astrolabe:   got " + e);
            if (e.getInt("status") != BluetoothGatt.GATT_SUCCESS && !e.getString("event").equals("disconnected"))
                throw new IOException(e.toString());
            if (e.getString("event").equals(expected)) return e;
            if (e.getString("event").equals("disconnected")) throw new IOException("BLE disconnected");
        }
        throw new IOException("Timed out waiting for " + expected);
    }

    static final BluetoothGattCallback CALLBACK = new BluetoothGattCallback() {
        public void onConnectionStateChange(BluetoothGatt g, int status, int state) {
            event(state == BluetoothProfile.STATE_CONNECTED ? "connected" : "disconnected", status,
                  state == BluetoothProfile.STATE_CONNECTED ? "" : "state=" + state + " status=" + status);
        }
        public void onServicesDiscovered(BluetoothGatt g, int status) { event("services", status, ""); }
        public void onMtuChanged(BluetoothGatt g, int mtu, int status) {
            synchronized (lock) { BleBridgeAudio.mtu = mtu; }
            event("mtu", status, String.valueOf(mtu));
        }
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            event("cccd", status, d.getCharacteristic().getUuid().toString());
        }
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            byte[] v = c.getValue();
            event("read", status, v == null ? "" : android.util.Base64.encodeToString(v, android.util.Base64.NO_WRAP));
        }
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            event("write", status, "");
        }
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            UUID u = c.getUuid();
            byte[] v = c.getValue();
            if (v == null) return;
            long now = System.nanoTime();
            if (u.equals(MIC)) {
                if (micNotifications == 0) firstNotifyNs = now;
                lastNotifyNs = now;
                micNotifications++;
                micBytes += v.length;
                if (v.length >= 4) {
                    int seq = (v[0] & 0xff) | ((v[1] & 0xff) << 8);
                    int codec = v[2] & 0xff;
                    Integer k = codec; codecCounts.put(k, codecCounts.get(k) == null ? 1 : codecCounts.get(k) + 1);
                    if (lastSeq >= 0) {
                        int next = (lastSeq + 1) & 0xfffe;
                        int span = (seq - lastSeq) & 0xffff;
                        if (span != 1 && span != 0xfffe) seqGaps++;
                        if (span > 0xff00) seqWraps++;
                    }
                    lastSeq = seq;
                }
            } else if (u.equals(CREDIT) && v.length >= 4) {
                creditNotifications++;
                lastCredit = (v[0] & 0xff) | ((v[1] & 0xff) << 8) | ((v[2] & 0xff) << 16) | ((v[3] & 0xff) << 24);
            }
        }
    };

    static void connect(BluetoothGatt gatt) throws Exception {
        await("connected", 15);
        try {
            gatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
        } catch (Throwable ignored) { }
        if (!gatt.requestMtu(512)) throw new IOException("MTU request not started");
        JSONObject e = await("mtu", 8);
        if (!gatt.discoverServices()) throw new IOException("Service discovery not started");
        await("services", 15);
    }

    static BluetoothGattCharacteristic need(BluetoothGatt gatt, UUID id) throws Exception {
        BluetoothGattService s = gatt.getService(AUDIO_SERVICE);
        if (s == null) throw new IOException("Audio service absent");
        BluetoothGattCharacteristic c = s.getCharacteristic(id);
        if (c == null) throw new IOException("Characteristic absent: " + id);
        return c;
    }

    static byte[] read(BluetoothGatt gatt, UUID id) throws Exception {
        BluetoothGattCharacteristic c = need(gatt, id);
        IOException last = new IOException("Read unavailable: " + id);
        for (int attempt = 0; attempt < 4; attempt++) {
            try {
                if (gatt.readCharacteristic(c)) {
                    await("read", 3);
                    return c.getValue();
                }
                last = new IOException("Read unavailable: " + id);
            } catch (IOException e) {
                last = e;
            }
            Thread.sleep(200);
        }
        throw last;
    }

    static void write(BluetoothGatt gatt, UUID id, byte[] value, int type) throws Exception {
        BluetoothGattCharacteristic c = need(gatt, id);
        c.setWriteType(type);
        c.setValue(value);
        for (int attempt = 0; attempt < 20; attempt++) {
            if (gatt.writeCharacteristic(c)) {
                if (type == BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT) await("write", 8);
                return;
            }
            Thread.sleep(100);
        }
        throw new IOException("Write not started: " + id);
    }

    static void subscribe(BluetoothGatt gatt, UUID id, boolean on) throws Exception {
        BluetoothGattCharacteristic c = need(gatt, id);
        if (!gatt.setCharacteristicNotification(c, on)) throw new IOException("Local notify toggle failed");
        BluetoothGattDescriptor d = c.getDescriptor(CCCD);
        if (d == null) throw new IOException("CCCD absent for " + id);
        d.setValue(on ? BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                      : BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
        boolean started = false;
        for (int attempt = 0; attempt < 10 && !started; attempt++) {
            started = gatt.writeDescriptor(d);
            if (!started) {
                Thread.sleep(100);
                d.setValue(on ? BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                              : BluetoothGattDescriptor.DISABLE_NOTIFICATION_VALUE);
            }
        }
        if (!started) throw new IOException("CCCD write not started");
        long end = System.nanoTime() + TimeUnit.SECONDS.toNanos(8);
        while (System.nanoTime() < end) {
            JSONObject e = events.poll(100, TimeUnit.MILLISECONDS);
            if (e == null) continue;
            if (e.getString("event").equals("cccd")
                && e.getString("value").contains(id.toString())
                && e.getInt("status") == BluetoothGatt.GATT_SUCCESS) {
                return;
            }
            if (e.getString("event").equals("disconnected")) throw new IOException("BLE disconnected");
        }
        throw new IOException("Timed out waiting for cccd " + id);
    }

    static JSONObject scan(JSONObject request) throws Exception {
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
                        astrolabe |= result.getScanRecord().getServiceUuids().contains(new ParcelUuid(AUDIO_SERVICE));
                    if (!astrolabe && (name == null || !(name.toLowerCase(Locale.ROOT).contains("astrolabe") || name.toLowerCase(Locale.ROOT).contains("lunasay")))) return;
                    found.put(result.getDevice().getAddress(), new JSONObject()
                        .put("address", result.getDevice().getAddress()).put("name", name)
                        .put("rssi", result.getRssi()).put("connectable", result.isConnectable()));
                } catch (JSONException e) { event("scan-error", -1, e.toString()); }
            }
            public void onScanFailed(int code) { event("scan-error", code, "Scan failed"); }
        };
        try {
            scanner.startScan(null, new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback);
            Thread.sleep(Math.max(1, Math.min(15, request.optInt("seconds", 6))) * 1000L);
            if (!events.isEmpty()) throw new IOException(events.poll().toString());
            return new JSONObject().put("ok", true).put("devices", new JSONArray(found.values()));
        } finally { scanner.stopScan(callback); }
    }

    static void startMic(JSONObject request, BluetoothGatt gatt) throws Exception {
        subscribe(gatt, MIC, true);
        byte[] body = "{\"mic\":1}".getBytes(StandardCharsets.UTF_8);
        byte[] msg = new byte[1 + body.length];
        msg[0] = 0x02;
        System.arraycopy(body, 0, msg, 1, body.length);
        write(gatt, CONTROL, msg, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
    }

    static void stopMic(BluetoothGatt gatt) throws Exception {
        try { write(gatt, CONTROL, "{\"mic\":0}".getBytes(StandardCharsets.UTF_8), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT); }
        catch (Exception ignored) { }
        try { subscribe(gatt, MIC, false); } catch (Exception ignored) { }
    }

    static JSONObject micStream(JSONObject request, BluetoothGatt gatt) throws Exception {
        startMic(request, gatt);
        int seconds = Math.max(1, Math.min(30, request.optInt("seconds", 6)));
        Thread.sleep(seconds * 1000L);
        stopMic(gatt);
        long spanNs = lastNotifyNs - firstNotifyNs;
        double rate = spanNs > 0 ? (micBytes * 8.0) / (spanNs / 1e9) / 1000.0 : 0;
        JSONObject codecs = new JSONObject();
        for (Map.Entry<Integer, Integer> e : codecCounts.entrySet()) {
            codecs.put(String.valueOf(e.getKey()), e.getValue());
        }
        JSONObject out = new JSONObject().put("ok", true)
            .put("mtu", mtu).put("notifications", micNotifications).put("bytes", micBytes)
            .put("seqGaps", seqGaps).put("seqWraps", seqWraps).put("kbits", Math.round(rate))
            .put("codecCounts", codecs);
        try {
            out.put("codecChar", read(gatt, CODEC)[0] & 0xff);
        } catch (Exception ignored) { }
        return out;
    }

    /** Contract §8: mic notify and TTS writes ride the same link concurrently. */
    static JSONObject duplex(JSONObject request, BluetoothGatt gatt) throws Exception {
        final int ttsSeconds = Math.max(1, Math.min(6, request.optInt("ttsSeconds", 3)));
        final int micSeconds = Math.max(ttsSeconds + 2, Math.min(30, request.optInt("seconds", ttsSeconds + 2)));
        byte[] creditRaw = read(gatt, CREDIT);
        int credit = (creditRaw[0] & 0xff) | ((creditRaw[1] & 0xff) << 8) | ((creditRaw[2] & 0xff) << 16) | ((creditRaw[3] & 0xff) << 24);
        subscribe(gatt, MIC, true);
        subscribe(gatt, CREDIT, true);
        byte[] body = "{\"mic\":1}".getBytes(StandardCharsets.UTF_8);
        byte[] msg = new byte[1 + body.length];
        msg[0] = 0x02;
        System.arraycopy(body, 0, msg, 1, body.length);
        write(gatt, CONTROL, msg, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
        Thread.sleep(1000);
        // mic streaming in background for micSeconds
        Thread micWindow = new Thread(() -> {
            try { Thread.sleep(micSeconds * 1000L); } catch (InterruptedException ignored) { }
        });
        micWindow.start();
        // TTS tone concurrent
        JSONObject tts = ttsTone(new JSONObject().put("seconds", ttsSeconds).put("hz", request.optInt("hz", 440)), gatt);
        int micNotifsAtTtsEnd = micNotifications;
        int micBytesAtTtsEnd = micBytes;
        // wait for mic window to finish
        micWindow.join();
        stopMic(gatt);
        long spanNs = lastNotifyNs - firstNotifyNs;
        double micRate = spanNs > 0 ? (micBytes * 8.0) / (spanNs / 1e9) / 1000.0 : 0;
        return new JSONObject().put("ok", true)
            .put("mtu", mtu)
            .put("mic", new JSONObject()
                .put("notifications", micNotifications).put("bytes", micBytes)
                .put("duringTts", micNotifsAtTtsEnd).put("bytesDuringTts", micBytesAtTtsEnd)
                .put("seqGaps", seqGaps).put("kbits", Math.round(micRate)))
            .put("tts", tts);
    }

    static JSONObject ttsTone(JSONObject request, BluetoothGatt gatt) throws Exception {
        byte[] creditRaw = read(gatt, CREDIT);
        int credit = (creditRaw[0] & 0xff) | ((creditRaw[1] & 0xff) << 8) | ((creditRaw[2] & 0xff) << 16) | ((creditRaw[3] & 0xff) << 24);
        subscribe(gatt, CREDIT, true);
        Thread.sleep(1500);
        int creditNotifiesBefore = creditNotifications;
        int seconds = Math.max(1, Math.min(6, request.optInt("seconds", 3)));
        int hz = request.optInt("hz", 440);
        int codec = read(gatt, CODEC)[0] & 0xff;
        if (codec != 0 && codec != 10) codec = 0;
        // PCM16 16k sine, 10 ms chunks (160 samples = 320 B), µ-law if negotiated
        int chunkSamples = codec == 0 ? 160 : 160;
        byte[] payload = new byte[codec == 0 ? 320 : 160];
        byte[] chunk = new byte[4 + payload.length];
        int seq = 0;
        long startNs = System.nanoTime();
        int totalChunks = seconds * 100;
        for (int i = 0; i < totalChunks; i++) {
            // wait for credit: each chunk consumes payload.length ring bytes; ring drains ~32 kB/s
            int need = payload.length;
            while (credit < need) {
                if (System.nanoTime() - startNs > TimeUnit.SECONDS.toNanos(seconds + 10)) {
                    throw new IOException("credit stall lastCredit=" + lastCredit);
                }
                // §6: local credit is authoritative; only a notification that
                // arrived AFTER the writes refreshes it. Adopting a stale
                // lastCredit would resurrect spent credit (ring overflow).
                int before = creditNotifications;
                long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
                while (creditNotifications == before && System.nanoTime() < deadline) {
                    Thread.sleep(10);
                }
                if (creditNotifications != before) {
                    credit = lastCredit;
                } else if (System.nanoTime() - startNs > TimeUnit.SECONDS.toNanos(seconds + 10)) {
                    throw new IOException("credit stall rxNotifies=" + creditNotifications
                        + " lastCredit=" + lastCredit + " writes=" + ttsWrites);
                }
            }
            for (int j = 0; j < chunkSamples; j++) {
                int idx = (int) (((startNs / 1000000L) + i * 10 + j * 1000L / 16) % 1000000);
                double ph = 2.0 * Math.PI * hz * ((i * chunkSamples + j) % 16000) / 16000.0;
                int s = (int) (Math.sin(ph) * 12000);
                if (codec == 0) {
                    payload[j * 2] = (byte) s;
                    payload[j * 2 + 1] = (byte) (s >> 8);
                } else {
                    payload[j] = ulaw(s);
                }
            }
            chunk[0] = (byte) (seq & 0xff);
            chunk[1] = (byte) ((seq >> 8) & 0xff);
            chunk[2] = (byte) codec;
            chunk[3] = 0;
            System.arraycopy(payload, 0, chunk, 4, payload.length);
            write(gatt, TTS, chunk, BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
            ttsWrites++;
            credit -= need;
            // Light pacing: flooding write-no-response backpressures the
            // Android GATT pipe and stalls reverse-direction notifications.
            // 5 ms/chunk still feeds 2x real-time; credit bounds the burst.
            Thread.sleep(5);
        }
        long endNs = System.nanoTime();
        double rate = (ttsWrites * payload.length * 8.0) / ((endNs - startNs) / 1e9) / 1000.0;
        Thread.sleep(1200); // drain playout
        JSONObject out = new JSONObject().put("ok", true)
            .put("mtu", mtu).put("codec", codec).put("ttsWrites", ttsWrites)
            .put("payloadBytes", ttsWrites * payload.length).put("kbits", Math.round(rate))
            .put("creditEnd", lastCredit).put("creditNotifies", creditNotifications)
            .put("creditNotifiesBefore", creditNotifiesBefore);
        return out;
    }

    static byte ulaw(int pcm) {
        int mag = pcm < 0 ? -pcm : pcm;
        int sign = pcm < 0 ? 0x7f : 0x00;
        if (mag > 32767) mag = 32767;
        mag += 0x84;
        int seg = 0;
        while (seg < 8 && mag > (0xff << (seg + 1)) + 0x7f) seg++;
        return (byte) (sign | (seg << 4) | ((mag >> (seg + 3)) & 0x0f));
    }

    static BluetoothAdapter adapter() {
        if (context != null) {
            Object mgr = context.getSystemService(Context.BLUETOOTH_SERVICE);
            if (mgr instanceof BluetoothManager) {
                BluetoothAdapter a = ((BluetoothManager) mgr).getAdapter();
                if (a != null) {
                    return a;
                }
            }
        }
        return BluetoothAdapter.getDefaultAdapter();
    }

    static BluetoothGatt open(JSONObject request) throws Exception {
        BluetoothDevice device = adapter.getRemoteDevice(request.getString("address"));
        Exception last = null;
        for (int attempt = 0; attempt < 3; attempt++) {
            events.clear();
            BluetoothGatt gatt = device.connectGatt(context, false, CALLBACK, BluetoothDevice.TRANSPORT_LE);
            if (gatt == null) throw new IOException("BLE connect not started");
            try {
                connect(gatt);
                System.err.println("astrolabe: connect() ok, attempt " + attempt);
                return gatt;
            } catch (Exception e) {
                System.err.println("astrolabe: attempt " + attempt + " failed: " + e);
                try { gatt.disconnect(); gatt.close(); } catch (Throwable ignored) { }
                last = e;
                if (attempt == 2) {
                    adapter = null;
                    throw new IOException("connect failed: " + e + " (after " + (attempt + 1) + " attempts)", e);
                }
                Thread.sleep(800);
            }
        }
        throw new IOException("unreachable");
    }

    static JSONObject run(JSONObject request) throws Exception {
        if (adapter == null) adapter = adapter();
        if (adapter == null || !adapter.isEnabled()) throw new IOException("Android Bluetooth is disabled");
        String op = request.getString("op");
        if (op.equals("quit")) {
            if (s_gatt != null) {
                try { s_gatt.disconnect(); s_gatt.close(); } catch (Throwable ignored) { }
                s_gatt = null;
            }
            return new JSONObject().put("ok", true).put("bye", true);
        }
        if (op.equals("scan")) return scan(request);
        if (op.equals("scan-all")) {
            Map<String, JSONObject> found = new ConcurrentHashMap<>();
            BluetoothLeScanner scanner = adapter.getBluetoothLeScanner();
            ScanCallback callback = new ScanCallback() {
                public void onScanResult(int type, ScanResult result) {
                    try {
                        String name = result.getScanRecord() == null ? "" : result.getScanRecord().getDeviceName();
                        found.put(result.getDevice().getAddress(), new JSONObject()
                            .put("address", result.getDevice().getAddress()).put("name", name)
                            .put("rssi", result.getRssi()));
                    } catch (JSONException ignored) { }
                }
                public void onScanFailed(int code) { event("scan-error", code, "Scan failed"); }
            };
            try {
                scanner.startScan(null, new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build(), callback);
                Thread.sleep(Math.max(1, Math.min(15, request.optInt("seconds", 6))) * 1000L);
            } finally { scanner.stopScan(callback); }
            return new JSONObject().put("ok", true).put("devices", new JSONArray(found.values()));
        }
        BluetoothGatt gatt = s_gatt != null ? s_gatt : open(request);
        s_gatt = gatt;
        try {
            // open() already performed connected/MTU/discovery.
            if (op.equals("probe")) {
                if (request.optBoolean("mtu", false)) {
                    if (!gatt.requestMtu(request.optInt("mtuValue", 512))) throw new IOException("MTU request not started");
                    await("mtu", 8);
                }
                if (request.optBoolean("discover", false)) {
                    if (!gatt.discoverServices()) throw new IOException("Service discovery not started");
                    await("services", 15);
                }
                if (request.optBoolean("read", false)) {
                    byte[] ctrl = read(gatt, CONTROL);
                    return new JSONObject().put("ok", true).put("mtu", mtu)
                        .put("control", new String(ctrl, StandardCharsets.UTF_8));
                }
                int seconds = Math.max(1, Math.min(20, request.optInt("seconds", 3)));
                Thread.sleep(seconds * 1000L);
                return new JSONObject().put("ok", true).put("mtu", mtu);
            }
            if (op.equals("audio-state")) {
                JSONArray chars = new JSONArray();
                BluetoothGattService s = gatt.getService(AUDIO_SERVICE);
                if (s != null) {
                    for (BluetoothGattCharacteristic c : s.getCharacteristics()) {
                        JSONObject j = new JSONObject();
                        try {
                            j.put("uuid", c.getUuid().toString());
                            j.put("id", c.getInstanceId());
                            j.put("props", c.getProperties());
                        } catch (JSONException ignored) { }
                        chars.put(j);
                    }
                }
                byte[] ctrl = read(gatt, CONTROL);
                byte[] codec = read(gatt, CODEC);
                byte[] credit = read(gatt, CREDIT);
                int cr = (credit[0] & 0xff) | ((credit[1] & 0xff) << 8) | ((credit[2] & 0xff) << 16) | ((credit[3] & 0xff) << 24);
                return new JSONObject().put("ok", true).put("mtu", mtu)
                    .put("chars", chars)
                    .put("control", new String(ctrl, StandardCharsets.UTF_8))
                    .put("codec", codec[0] & 0xff).put("credit", cr);
            }
            if (op.equals("mic-stream")) return micStream(request, gatt);
            if (op.equals("duplex")) return duplex(request, gatt);
            if (op.equals("tts-tone")) return ttsTone(request, gatt);
            if (op.equals("control")) {
                byte[] body = request.getString("body").getBytes(StandardCharsets.UTF_8);
                byte[] msg = new byte[1 + body.length];
                msg[0] = (byte) request.optInt("msg", 0x02);
                System.arraycopy(body, 0, msg, 1, body.length);
                write(gatt, CONTROL, msg, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                return new JSONObject().put("ok", true).put("state", new String(read(gatt, CONTROL), StandardCharsets.UTF_8));
            }
            throw new IOException("Unknown operation: " + op);
        } catch (Exception e) {
            // Drop the shared connection on failure; the next op reconnects.
            try { if (s_gatt != null) { s_gatt.disconnect(); s_gatt.close(); } } catch (Throwable ignored) { }
            s_gatt = null;
            throw e;
        }
    }

    static BluetoothGatt s_gatt;

    public static void main(String[] args) throws Exception {
        System.setOut(new PrintStream(new FileOutputStream(FileDescriptor.out), true, "UTF-8"));
        System.setErr(new PrintStream(new FileOutputStream(FileDescriptor.err), true, "UTF-8"));
        Looper.prepareMainLooper();
        Class<?> activityThread = Class.forName("android.app.ActivityThread");
        Object thread = activityThread.getMethod("systemMain").invoke(null);
        Context system = (Context) activityThread.getMethod("getSystemContext").invoke(thread);
        String pkg = System.getProperty("astrolabe.pkg", "institute.castalia.reliquary");
        Context host;
        try {
            host = system.createPackageContext(pkg, 0);
        } catch (Exception e) {
            host = system.createPackageContext("com.android.shell", 0);
        }
        final String hostPkg = host.getPackageName();
        System.err.println("astrolabe: hostPkg=" + hostPkg + " sdk=" + android.os.Build.VERSION.SDK_INT);
        context = new ContextWrapper(host) {
            public String getPackageName() { return hostPkg; }
            public String getOpPackageName() { return hostPkg; }
            public android.content.AttributionSource getAttributionSource() {
                return new android.content.AttributionSource.Builder(android.os.Process.myUid())
                    .setPackageName(hostPkg)
                    .build();
            }
            public Context getApplicationContext() { return this; }
        };
        if (android.os.Build.VERSION.SDK_INT >= 31) {
            try {
                adapter = (BluetoothAdapter) BluetoothAdapter.class
                    .getDeclaredMethod("createAdapter", android.content.AttributionSource.class)
                    .invoke(null, context.getAttributionSource());
            } catch (Throwable t) {
                System.err.println("astrolabe: createAdapter reflection failed: " + t);
            }
        }
        new Thread(() -> {
            try {
                BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.UTF_8));
                boolean daemon = args != null && args.length > 0 && "--daemon".equals(args[0]);
                if (daemon) {
                    // File-based RPC: poll req.json (last line wins), write resp.json.
                    // adb cannot keep a fifo open across shell sessions here.
                    java.io.File req = new java.io.File("/data/local/tmp/astrolabe-ble/req.json");
                    java.io.File resp = new java.io.File("/data/local/tmp/astrolabe-ble/resp.json");
                    long lastMtime = 0;
                    while (true) {
                        long m = req.lastModified();
                        if (m != 0 && m != lastMtime) {
                            lastMtime = m;
                            String line = null;
                            try (java.io.RandomAccessFile raf = new java.io.RandomAccessFile(req, "r")) {
                                long pos = raf.length();
                                // last non-empty line
                                while (pos > 0) {
                                    raf.seek(--pos);
                                    if (pos > 0 && raf.read() == '\n') {
                                        line = raf.readLine();
                                        if (line != null && !line.trim().isEmpty()) break;
                                        line = null;
                                    }
                                }
                                if (line == null) {
                                    raf.seek(0);
                                    line = raf.readLine();
                                }
                            }
                            if (line == null || line.trim().isEmpty()) continue;
                            JSONObject result;
                            try {
                                result = run(new JSONObject(line.trim()));
                            } catch (Exception e) {
                                result = new JSONObject().put("ok", false).put("error", e.toString());
                            }
                            try (java.io.FileWriter w = new java.io.FileWriter(resp)) {
                                w.write(result.toString());
                            }
                            if (result.has("bye")) break;
                        } else {
                            Thread.sleep(150);
                        }
                    }
                } else {
                    String line = in.readLine();
                    JSONObject result = run(new JSONObject(line));
                    System.out.println(result.toString());
                }
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
