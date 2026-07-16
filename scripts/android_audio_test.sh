#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT="$ROOT/apps/astrolabe-android-audio-test"
PACKAGE="org.castaliainstitute.astrolabe.audiotest"
ACTIVITY="$PACKAGE/.MainActivity"
ANDROID_HOME="${ANDROID_HOME:-$HOME/Library/Android/sdk}"
export ANDROID_HOME

device=()
if [[ "${1:-}" == "--device" ]]; then
    [[ $# -ge 3 ]] || { echo "usage: $0 [--device ADB_SERIAL] build|install|start|status" >&2; exit 2; }
    device=(-s "$2")
    shift 2
fi

action="${1:-build}"
apk="$PROJECT/app/build/outputs/apk/debug/app-debug.apk"

case "$action" in
    build)
        gradle --no-daemon -p "$PROJECT" assembleDebug
        echo "$apk"
        ;;
    install)
        gradle --no-daemon -p "$PROJECT" assembleDebug
        adb "${device[@]}" install -r "$apk"
        ;;
    start)
        adb "${device[@]}" shell am start -n "$ACTIVITY"
        ;;
    status)
        adb "${device[@]}" shell dumpsys media.audio_flinger
        adb "${device[@]}" shell dumpsys audio
        ;;
    *)
        echo "usage: $0 [--device ADB_SERIAL] build|install|start|status" >&2
        exit 2
        ;;
esac
