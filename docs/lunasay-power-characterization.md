# LunaSay power characterization

This document defines the evidence required before LunaSay battery-life claims
are used on Kickstarter. It deliberately separates functional smoke tests,
projected runtime, and measured runtime to shutdown.

## Test article and controls

- Photograph the battery label before accepting any mAh-based calculation. The
  [official 1.75C product documentation](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75C)
  distinguishes “with battery” and “without battery” SKUs but does not publish
  the supplied cell capacity. Do not infer capacity from the enclosure or PMU.
- Record firmware commit/build identifier, hardware revision, battery label and
  rated capacity, battery cycle count when known, ambient temperature, and unit
  identifier for every publishable run.
- Charge to termination, allow 30 minutes of rest, record starting voltage and
  fuel-gauge percentage, then remove VBUS with the controllable USB hub.
- Keep automatic brightness, face, Wi-Fi credentials, audio volume, prompt
  cadence, and server/model configuration fixed for the whole run.
- A runner must always restore VBUS on completion or failure. Network polling
  may establish liveness but must not wake HTTP/audio services during idle runs.
- Attribute only PMU samples between `vbus_off` and `vbus_on`. Never combine
  docked history or another scenario with the current discharge segment.
- Treat AXP2101 percentage as a coarse secondary signal and retain voltage for
  every point. Waveshare documents that its AXP2101 percentage estimate is
  voltage-based, nonlinear, and prone to fluctuations after load or charger
  changes; rested voltage and complete runtime are therefore required.

## Required matrix

| Scenario | Display | Radios | Workload | Primary result |
|---|---|---|---|---|
| `full-wifi` | 100% | Wi-Fi | idle face | runtime to shutdown |
| `full-offline` | 100% | off | idle face | display cost |
| `dim-wifi` | 10% | Wi-Fi | idle face | runtime to shutdown |
| `dim-offline` | 10% | off | idle face | display/radio separation |
| `off-wifi` | off | Wi-Fi | idle/listener | connected standby runtime |
| `sleep-offline` | off | off | CPU awake | display-off baseline |
| true deep sleep | off/unused rails quiesced | off | timer + BOOT (GPIO0) wake | sleep current/runtime |
| conversation | dim, then off | Wi-Fi | repeated STT→LLM→TTS | successful turns and hours |
| journal/meeting | off | Wi-Fi as needed | continuous recording/transcription | recording hours and upload duty cycle |
| BLE advertising | full, dim, or off | BLE only | CoreBluetooth liveness probe | BLE standby runtime |
| BLE configuration | off or dim | BLE | periodic fetch/set | configuration-session cost |

Run every advertised mode to automatic low-voltage shutdown on one
release-candidate unit, then repeat it on a second unit. Shorter runs may be used
to tune firmware but are not final runtime evidence.

## True deep sleep gate

The current ESP-IDF `sleep-offline` scenario is a display-off, radio-off idle
baseline; the CPU remains awake. It must not be called deep sleep in reports.
The previous Arduino implementation used ESP32 timer wake plus active-low GPIO0
wake. The ESP-IDF port must additionally:

1. Reject entry while VBUS is present or a voice/OTA/write transaction is active.
2. Mute the amplifier, stop I2S, stop Wi-Fi/BLE, blank the AMOLED, and quiesce
   safe AXP2101 peripheral rails without disabling the ESP32 supply.
3. Persist the test start, requested wake interval, starting voltage/percentage,
   firmware build, and wake reason across reset.
4. Wake by BOOT/GPIO0 or timer, restore all required rails, and expose the completed
   interval in serial/API telemetry.
5. Demonstrate BOOT-button wake and timed wake on battery before beginning a long run.

The board's separate PWR key connects to the AXP2101 `PWRON` input. Its `PWROK`
output controls ESP32 reset, while `AXP_IRQ` is not routed to an ESP GPIO in the
official schematic. Therefore firmware must not describe the PWR key as an
ESP32 deep-sleep wake source; only BOOT/GPIO0 and the timer are asserted here.

## Evidence levels

- **Functional only:** mode applied correctly, source reports `battery`, and the
  expected display/radio/voice behavior works. No battery-life claim.
- **Projected runtime:** at least one hour, at least three battery-only samples,
  and at least 2% monotonic fuel-gauge drop. Label it as a projection.
- **Measured runtime:** elapsed time from rested full charge to automatic
  shutdown, with endpoint recovery confirmed when VBUS is restored.
- **Kickstarter claim:** measured runtime repeated on a second release-candidate
  unit. Publish a conservative rounded value and state the exact scenario.

Generate the current evidence table and curve data with:

```sh
python3 scripts/lunasay_power_report.py
```

BLE-only runs use the non-persistent firmware command `ble power-test on` and
the native `scripts/lunasay_ble_probe.swift` CoreBluetooth scanner. The runner
must send `ble power-test off` after recovery; neither command changes the
owner's saved BLE preference.

The generated `report.md`, `runs.csv`, and `curves.csv` live under
`artifacts/qa/lunasay-power-report/` by default.
