#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
SDK=/opt/astrolabe-android
mkdir -p "$SDK/classes" "$SDK/dex"
java -jar "$SDK/ecj.jar" -8 -cp "$SDK/android-35/android.jar" -d "$SDK/classes" "$HERE/BleBridge.java"
java -cp "$SDK/r8.jar" com.android.tools.r8.D8 --min-api 26 --lib "$SDK/android-35/android.jar" \
  --output "$SDK/dex" "$SDK"/classes/org/castalia/astrolabe/*.class
mkdir -p /proc/1/root/data/local/tmp/astrolabe-ble
cp "$SDK/dex/classes.dex" /proc/1/root/data/local/tmp/astrolabe-ble/bridge.dex
chmod 755 /proc/1/root/data/local/tmp/astrolabe-ble
chmod 644 /proc/1/root/data/local/tmp/astrolabe-ble/bridge.dex
