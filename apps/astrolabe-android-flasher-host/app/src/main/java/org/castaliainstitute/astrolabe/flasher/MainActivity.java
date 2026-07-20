package org.castaliainstitute.astrolabe.flasher;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    private static final String FLASHER_URL = "http://localhost:8765/flasher/";

    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private TextView status;
    private Button launchButton;
    private boolean openedAutomatically;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(buildUi());
        startService(new Intent(this, FlasherService.class));
        waitForBridge();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        openedAutomatically = false;
        startService(new Intent(this, FlasherService.class));
        FlasherService.requestUsbPermission(this);
        waitForBridge();
    }

    @Override
    public void onBackPressed() {
        moveTaskToBack(true);
    }

    private void waitForBridge() {
        if (!FlasherService.isReady()) {
            status.setText("Starting persistent Android USB bridge…");
            mainHandler.postDelayed(this::waitForBridge, 100);
            return;
        }
        launchButton.setEnabled(true);
        if (FlasherService.hasAstrolabe() && !FlasherService.hasUsbPermission()) {
            status.setText("Grant USB access to Astrolabe to continue");
            FlasherService.requestUsbPermission(this);
            mainHandler.postDelayed(this::waitForBridge, 250);
            return;
        }
        status.setText(FlasherService.hasAstrolabe()
                ? "Ready · Astrolabe connected and firmware bundled"
                : "Ready · connect Astrolabe, then return to Chrome");
        if (!openedAutomatically) {
            openedAutomatically = true;
            openChrome();
        }
    }

    private LinearLayout buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(dp(24), dp(36), dp(24), dp(24));
        root.setBackgroundColor(Color.rgb(7, 9, 15));

        TextView title = text("Astrolabe Flasher", 28, Color.WHITE);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView explanation = text(
                "The bundled local firmware is flashed through Android USB. The bridge stays active while Chrome is open and reconnects after ESP32 bootloader resets.",
                16,
                Color.rgb(205, 202, 218));
        explanation.setGravity(Gravity.CENTER);
        explanation.setPadding(0, dp(18), 0, dp(24));
        root.addView(explanation);

        status = text("Starting local flasher…", 16, Color.rgb(230, 225, 240));
        status.setGravity(Gravity.CENTER);
        status.setPadding(dp(14), dp(14), dp(14), dp(14));
        status.setBackgroundColor(Color.rgb(34, 36, 48));
        root.addView(status, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        launchButton = new Button(this);
        launchButton.setText("Open flasher in Chrome");
        launchButton.setTextSize(17);
        launchButton.setAllCaps(false);
        launchButton.setEnabled(false);
        launchButton.setOnClickListener(view -> openChrome());
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(56));
        buttonParams.topMargin = dp(24);
        root.addView(launchButton, buttonParams);
        return root;
    }

    private TextView text(String value, float size, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void openChrome() {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(FLASHER_URL));
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        intent.setPackage("com.android.chrome");
        try {
            startActivity(intent);
        } catch (ActivityNotFoundException missingChrome) {
            intent.setPackage(null);
            startActivity(intent);
        }
    }
}
