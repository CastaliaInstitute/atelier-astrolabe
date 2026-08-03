# Astrolabe Widget

A native iOS/macOS companion app and WidgetKit extension for monitoring and controlling one or more Astrolabes.

## Architecture

- The iOS or macOS app owns CoreBluetooth discovery, explicit device selection, GATT discovery, authenticated commands, and local-network screen capture.
- The widget reads cached device state and BMP captures from the shared App Group. Tap, swipe, and refresh controls deep-link into the companion app because WidgetKit is not a reliable long-running BLE central.
- BLE commands use a fresh, one-use challenge and HMAC-SHA256 signature. A device is identified by its CoreBluetooth identifier plus the MAC returned by the challenge; the app never auto-selects a device by name alone.
- Current firmware exposes control via BLE but serves `/screen.bmp` over its local Wi-Fi HTTP endpoint. Screenshot transport is isolated behind `refreshScreenshot(for:)` so a future chunked BLE characteristic can replace it.

## Build

Requirements: Xcode 15+, iOS 17+/macOS 14+, and [XcodeGen](https://github.com/yonaskolb/XcodeGen).

```sh
cd astrolabe-widget
xcodegen generate
open AstrolabeWidget.xcodeproj
```

The project contains `AstrolabeWidgetiOS` and `AstrolabeWidgetApp` schemes plus a WidgetKit extension for each platform. Select a development team for the app and extension you build. If your signing setup cannot use the checked-in App Group identifier, change `group.institute.castalia.astrolabe-widget` consistently in all entitlements files and `SharedSnapshotStore`.

## First run

1. Grant Bluetooth and Local Network access.
2. Scan and select the intended device using name, RSSI, CoreBluetooth identifier, and—after connection—the device MAC.
3. Connect. The app discovers the live GATT table rather than assuming service placement.
4. Enter the provisioned device secret in hex, then switch faces, send gestures, or execute a console command.
5. Add **Astrolabe Remote** from the macOS widget gallery.

The device secret is stored per CoreBluetooth identifier in the platform Keychain and is never copied into the widget's App Group. Per-device widget configuration is the next milestone; the initial widget displays the first cached Astrolabe.
