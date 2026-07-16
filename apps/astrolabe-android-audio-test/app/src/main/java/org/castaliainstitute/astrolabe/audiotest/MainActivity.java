package org.castaliainstitute.astrolabe.audiotest;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.media.AudioAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioRecord;
import android.media.AudioTrack;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.util.Arrays;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

public final class MainActivity extends Activity {
    private static final int SAMPLE_RATE = 48_000;
    private static final int CHANNELS = 2;
    private static final int TEST_SECONDS = 8;
    private static final int RECORD_AUDIO_REQUEST = 1001;

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean stopRequested = new AtomicBoolean(false);
    private AudioManager audioManager;
    private AudioTrack activeTrack;
    private AudioRecord activeRecord;
    private Thread audioThread;
    private TextView status;
    private Button leftButton;
    private Button rightButton;
    private Button stereoButton;
    private Button sweepButton;
    private Button bridgeButton;
    private Button stopButton;

    private final AudioDeviceCallback deviceCallback = new AudioDeviceCallback() {
        @Override public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) { refreshStatus(); }
        @Override public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
            stopTest();
            refreshStatus();
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        audioManager = (AudioManager) getSystemService(AUDIO_SERVICE);
        setContentView(buildUi());
        audioManager.registerAudioDeviceCallback(deviceCallback, mainHandler);
        refreshStatus();
    }

    @Override
    protected void onDestroy() {
        audioManager.unregisterAudioDeviceCallback(deviceCallback);
        stopTest();
        super.onDestroy();
    }

    private View buildUi() {
        int pad = dp(20);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(Color.rgb(23, 21, 28));

        TextView title = text("Astrolabe USB-C Audio", 26, Color.WHITE);
        title.setPadding(0, 0, 0, dp(8));
        root.addView(title);

        TextView help = text(
                "Connect Astrolabe by USB-C. Test Android → Astrolabe, or bridge Astrolabe → Bluetooth Classic.",
                15, Color.rgb(205, 196, 220));
        help.setPadding(0, 0, 0, dp(16));
        root.addView(help);

        status = text("Checking audio routes…", 14, Color.rgb(229, 222, 238));
        status.setBackgroundColor(Color.rgb(42, 38, 49));
        status.setPadding(dp(14), dp(14), dp(14), dp(14));
        root.addView(status, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        leftButton = testButton("Left · 1 kHz", () -> startTest(TestMode.LEFT));
        rightButton = testButton("Right · 1 kHz", () -> startTest(TestMode.RIGHT));
        stereoButton = testButton("Stereo · 1 kHz", () -> startTest(TestMode.STEREO));
        sweepButton = testButton("Stereo sweep · 100–10k", () -> startTest(TestMode.SWEEP));
        bridgeButton = testButton("Bridge Astrolabe → Bluetooth", this::startBridge);
        stopButton = testButton("Stop", this::stopTest);

        root.addView(leftButton);
        root.addView(rightButton);
        root.addView(stereoButton);
        root.addView(sweepButton);
        root.addView(bridgeButton);
        root.addView(stopButton);

        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(root);
        return scroll;
    }

    private Button testButton(String label, Runnable action) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextSize(16);
        button.setAllCaps(false);
        button.setGravity(Gravity.CENTER);
        button.setOnClickListener(v -> action.run());
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(54));
        params.topMargin = dp(10);
        button.setLayoutParams(params);
        return button;
    }

    private TextView text(String value, float size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private void refreshStatus() {
        mainHandler.post(() -> {
            AudioDeviceInfo usb = findUsbOutput();
            AudioDeviceInfo usbInput = findUsbInput();
            AudioDeviceInfo bluetooth = findBluetoothOutput();
            StringBuilder out = new StringBuilder();
            if (usb == null) {
                out.append("USB audio: NOT DETECTED\n\n")
                        .append("Astrolabe must enumerate as a UAC audio output. ")
                        .append("Check the cable/OTG connection and ensure “Disable USB audio routing” is off.");
            } else {
                out.append("USB audio: READY\n")
                        .append("Name: ").append(deviceName(usb)).append('\n')
                        .append("Type: ").append(typeName(usb.getType())).append('\n')
                        .append("Rates: ").append(formatArray(usb.getSampleRates(), "driver default")).append('\n')
                        .append("Channels: ").append(formatArray(usb.getChannelCounts(), "driver default"));
                AudioTrack track = activeTrack;
                if (track != null && track.getRoutedDevice() != null) {
                    out.append("\nActive route: ").append(deviceName(track.getRoutedDevice()));
                }
            }
            out.append("\n\nUSB capture: ")
                    .append(usbInput == null ? "NOT DETECTED" : deviceName(usbInput));
            out.append("\nBluetooth A2DP: ")
                    .append(bluetooth == null ? "NOT CONNECTED" : deviceName(bluetooth));
            AudioRecord record = activeRecord;
            AudioTrack track = activeTrack;
            if (record != null && record.getRoutedDevice() != null) {
                out.append("\nBridge input route: ").append(deviceName(record.getRoutedDevice()));
            }
            if (record != null && track != null && track.getRoutedDevice() != null) {
                out.append("\nBridge output route: ").append(deviceName(track.getRoutedDevice()));
            }
            status.setText(out.toString());
            boolean ready = usb != null && audioThread == null;
            leftButton.setEnabled(ready);
            rightButton.setEnabled(ready);
            stereoButton.setEnabled(ready);
            sweepButton.setEnabled(ready);
            bridgeButton.setEnabled(usbInput != null && bluetooth != null && audioThread == null);
            stopButton.setEnabled(audioThread != null);
        });
    }

    private AudioDeviceInfo findUsbOutput() {
        for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            if (device.isSink() && isUsb(device.getType())) return device;
        }
        return null;
    }

    private AudioDeviceInfo findUsbInput() {
        for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_INPUTS)) {
            if (device.isSource() && isUsb(device.getType())) return device;
        }
        return null;
    }

    private AudioDeviceInfo findBluetoothOutput() {
        for (AudioDeviceInfo device : audioManager.getDevices(AudioManager.GET_DEVICES_OUTPUTS)) {
            if (device.isSink() && device.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) return device;
        }
        return null;
    }

    private static boolean isUsb(int type) {
        return type == AudioDeviceInfo.TYPE_USB_DEVICE
                || type == AudioDeviceInfo.TYPE_USB_HEADSET
                || type == AudioDeviceInfo.TYPE_USB_ACCESSORY;
    }

    private synchronized void startTest(TestMode mode) {
        if (audioThread != null) return;
        AudioDeviceInfo usb = findUsbOutput();
        if (usb == null) {
            refreshStatus();
            return;
        }
        stopRequested.set(false);
        audioThread = new Thread(() -> streamTest(usb, mode), "astrolabe-audio-test");
        audioThread.start();
        refreshStatus();
    }

    private void startBridge() {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO}, RECORD_AUDIO_REQUEST);
            return;
        }
        startBridgeWithPermission();
    }

    private synchronized void startBridgeWithPermission() {
        if (audioThread != null) return;
        AudioDeviceInfo usbInput = findUsbInput();
        AudioDeviceInfo bluetooth = findBluetoothOutput();
        if (usbInput == null || bluetooth == null) {
            refreshStatus();
            return;
        }
        stopRequested.set(false);
        audioThread = new Thread(
                () -> streamBridge(usbInput, bluetooth), "astrolabe-bluetooth-bridge");
        audioThread.start();
        refreshStatus();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == RECORD_AUDIO_REQUEST
                && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            startBridgeWithPermission();
        }
    }

    private void streamBridge(AudioDeviceInfo usbInput, AudioDeviceInfo bluetooth) {
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            finishTest("Microphone permission was revoked before the bridge started.");
            return;
        }
        int inputMin = AudioRecord.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        int outputMin = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        if (inputMin <= 0 || outputMin <= 0) {
            finishTest("This phone rejected the 48 kHz PCM bridge format.");
            return;
        }
        int framesPerChunk = 960;
        int inputBytes = Math.max(inputMin, framesPerChunk * 2 * 4);
        int outputBytes = Math.max(outputMin, framesPerChunk * CHANNELS * 2 * 4);

        AudioRecord record;
        AudioTrack track;
        try {
            record = new AudioRecord.Builder()
                    .setAudioSource(MediaRecorder.AudioSource.DEFAULT)
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                            .build())
                    .setBufferSizeInBytes(inputBytes)
                    .build();
            track = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder()
                            .setUsage(AudioAttributes.USAGE_MEDIA)
                            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                            .build())
                    .setAudioFormat(new AudioFormat.Builder()
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setSampleRate(SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                            .build())
                    .setBufferSizeInBytes(outputBytes)
                    .setTransferMode(AudioTrack.MODE_STREAM)
                    .build();
        } catch (RuntimeException error) {
            finishTest("Could not create the Android audio bridge: " + error.getMessage());
            return;
        }

        activeRecord = record;
        activeTrack = track;
        if (!record.setPreferredDevice(usbInput) || !track.setPreferredDevice(bluetooth)) {
            record.release();
            track.release();
            activeRecord = null;
            activeTrack = null;
            finishTest("Android rejected the requested USB input or Bluetooth A2DP output route.");
            return;
        }

        short[] mono = new short[framesPerChunk];
        short[] stereo = new short[framesPerChunk * CHANNELS];
        String error = null;
        try {
            track.play();
            record.startRecording();
            mainHandler.post(this::refreshStatus);
            while (!stopRequested.get()) {
                int read = record.read(mono, 0, mono.length, AudioRecord.READ_BLOCKING);
                if (read <= 0) {
                    error = "USB audio capture stopped (read " + read + ").";
                    break;
                }
                for (int i = 0; i < read; i++) {
                    stereo[i * 2] = mono[i];
                    stereo[i * 2 + 1] = mono[i];
                }
                int written = track.write(stereo, 0, read * 2, AudioTrack.WRITE_BLOCKING);
                if (written < 0) {
                    error = "Bluetooth audio playback stopped (write " + written + ").";
                    break;
                }
            }
        } catch (IllegalStateException exception) {
            if (!stopRequested.get()) error = "Audio bridge stopped: " + exception.getMessage();
        } finally {
            try { record.stop(); } catch (IllegalStateException ignored) { }
            try { track.stop(); } catch (IllegalStateException ignored) { }
            record.release();
            track.release();
            activeRecord = null;
            activeTrack = null;
        }
        finishTest(error);
    }

    private void streamTest(AudioDeviceInfo usb, TestMode mode) {
        int minBytes = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        int bufferBytes = Math.max(minBytes, SAMPLE_RATE / 10 * CHANNELS * 2);
        AudioFormat format = new AudioFormat.Builder()
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                .setSampleRate(SAMPLE_RATE)
                .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                .build();
        AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                .build();
        AudioTrack track = new AudioTrack.Builder()
                .setAudioAttributes(attributes)
                .setAudioFormat(format)
                .setBufferSizeInBytes(bufferBytes)
                .setTransferMode(AudioTrack.MODE_STREAM)
                .build();
        activeTrack = track;
        boolean preferred = track.setPreferredDevice(usb);
        if (!preferred) {
            finishTest("Android rejected the USB output route.");
            track.release();
            activeTrack = null;
            return;
        }

        short[] pcm = new short[960 * CHANNELS];
        long framesWritten = 0;
        long totalFrames = (long) SAMPLE_RATE * TEST_SECONDS;
        double phase = 0.0;
        track.play();
        mainHandler.post(this::refreshStatus);
        while (!stopRequested.get() && framesWritten < totalFrames) {
            int frames = (int) Math.min(pcm.length / CHANNELS, totalFrames - framesWritten);
            for (int frame = 0; frame < frames; frame++) {
                double elapsed = (framesWritten + frame) / (double) SAMPLE_RATE;
                double frequency = mode == TestMode.SWEEP
                        ? 100.0 * Math.pow(100.0, elapsed / TEST_SECONDS)
                        : 1000.0;
                phase += 2.0 * Math.PI * frequency / SAMPLE_RATE;
                short sample = (short) (Math.sin(phase) * 8192.0);
                pcm[frame * 2] = mode == TestMode.RIGHT ? 0 : sample;
                pcm[frame * 2 + 1] = mode == TestMode.LEFT ? 0 : sample;
            }
            int written = track.write(pcm, 0, frames * CHANNELS, AudioTrack.WRITE_BLOCKING);
            if (written < 0) break;
            framesWritten += written / CHANNELS;
        }
        try { track.pause(); } catch (IllegalStateException ignored) { }
        try { track.flush(); } catch (IllegalStateException ignored) { }
        track.release();
        activeTrack = null;
        finishTest(null);
    }

    private synchronized void stopTest() {
        stopRequested.set(true);
        AudioRecord record = activeRecord;
        if (record != null) {
            try { record.stop(); } catch (IllegalStateException ignored) { }
        }
        AudioTrack track = activeTrack;
        if (track != null) {
            try { track.stop(); } catch (IllegalStateException ignored) { }
        }
    }

    private void finishTest(String error) {
        synchronized (this) { audioThread = null; }
        mainHandler.post(() -> {
            refreshStatus();
            if (error != null) status.setText(error + "\n\n" + status.getText());
        });
    }

    private static String deviceName(AudioDeviceInfo device) {
        CharSequence name = device.getProductName();
        return name == null || name.length() == 0 ? "Audio device" : name.toString();
    }

    private static String formatArray(int[] values, String empty) {
        return values.length == 0 ? empty : Arrays.toString(values);
    }

    private static String typeName(int type) {
        if (type == AudioDeviceInfo.TYPE_USB_HEADSET) return "USB headset";
        if (type == AudioDeviceInfo.TYPE_USB_DEVICE) return "USB device";
        if (type == AudioDeviceInfo.TYPE_USB_ACCESSORY) return "USB accessory";
        if (type == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) return "Bluetooth A2DP";
        return String.format(Locale.US, "USB (%d)", type);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private enum TestMode { LEFT, RIGHT, STEREO, SWEEP }
}
