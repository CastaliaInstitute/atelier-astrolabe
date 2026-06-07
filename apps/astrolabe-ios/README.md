# Astrolabe Link

Small Capacitor iOS app for provisioning an Astrolabe Faculty watch over BLE.

The app scans for the firmware BLE settings service in `faculty175/main/faculty175_ble.c`, connects to `Astrolabe Faculty`, and writes:

```json
{
  "wifi": {
    "ssid": "iPhone hotspot SSID",
    "password": "hotspot password"
  },
  "tz": "America/Denver",
  "epoch": 1780688400
}
```

## iOS hotspot limitation

iOS apps cannot turn Personal Hotspot on, create a hotspot, or read the hotspot password for the user. This app therefore asks the user to enable Personal Hotspot in Settings and enter the hotspot network name and password, then sends those credentials to Astrolabe over BLE.

This is also why a PWA is not enough on iOS: Safari/iOS WebKit does not expose Web Bluetooth for this kind of BLE provisioning flow.

## Development

```sh
npm install
npm run build
npm run cap:add:ios
npm run cap:sync
npm run cap:open
```

The generated native project includes `NSBluetoothAlwaysUsageDescription` in `ios/App/App/Info.plist`.
