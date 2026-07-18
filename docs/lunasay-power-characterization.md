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
- Require firmware battery-only scenario counters or retained history metadata
  to show that the requested display mode and Wi-Fi/BLE state dominated the
  attributed interval. A requested scenario name alone is not evidence that it
  was applied.
- Firmware retains 240 fixed-cadence samples in NVS: 15-minute resolution for
  up to 60 hours, plus source/charging transitions. Fetch the ring only after
  VBUS restoration so idle measurements do not wake the HTTP/audio stack.
- Treat AXP2101 percentage as a coarse secondary signal and retain voltage for
  every point. Waveshare documents that its AXP2101 percentage estimate is
  voltage-based, nonlinear, and prone to fluctuations after load or charger
  changes; rested voltage and complete runtime are therefore required.
- Do not infer shutdown from lost pings. After restoring VBUS, require either a
  low recovered endpoint (≤5% or ≤3400 mV), or all three independent reset
  signals: an uptime discontinuity, ESP power-on reset, and AXP2101
  `PWROFF_STATUS` bit 3 (VSYS undervoltage). The
  [AXP2101 register specification](https://files.waveshare.com/wiki/common/X-power-AXP2101_SWcharge_V1.0.pdf)
  defines register `0x21` bit 3 as the undervoltage power-off source.

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
| conversation | full, dim, and off | Wi-Fi | repeated STT→LLM→TTS | successful turns and hours |
| journal/meeting | off | Wi-Fi as needed | repeated 30-second recording/transcription segments with no configured gap | recording hours and measured capture/upload duty cycle |
| BLE advertising | full, dim, or off | BLE only | CoreBluetooth liveness probe | BLE standby runtime |
| BLE configuration | off or dim | BLE | periodic fetch/set | configuration-session cost |

Run every advertised mode to automatic low-voltage shutdown on one
release-candidate unit, then repeat it on a second unit. Shorter runs may be used
to tune firmware but are not final runtime evidence. Release readiness also
requires a synchronized battery-path analyzer trace covering at least 95% of
each run and a recorded labeled cell capacity; runtime alone does not fully
characterize power consumption.

## Current and energy measurement

The on-board AXP2101 interface used by this board reports battery voltage,
coarse fuel-gauge percentage, source, and charge/discharge state. It does not
report instantaneous battery current. The AXP2101 register specification's ADC
table lists only VBAT, VBUS, VSYS, TS voltage, and die temperature; its PMU
status register reports current *direction* but no current magnitude. The
controllable USB hub removes and restores VBUS reproducibly, but it is not a
current meter. Consequently:

- `rated_capacity_mAh / measured_runtime_h` may be reported as a
  **capacity-derived average current** only after the physical cell label has
  been photographed and recorded. It is not a direct current measurement, and
  its uncertainty includes cell tolerance, age, temperature, conversion loss,
  and the cutoff voltage.
- Direct active-mode power requires an inline battery-path power analyzer or
  coulomb counter. Record voltage, current, and accumulated mAh/Wh at a fixed
  cadence while the hub controls VBUS. Measuring the USB input while charging
  does not isolate device load from charger current.
- Deep-sleep current must be measured with a microamp-capable instrument whose
  burden voltage does not reset the board. Fuel-gauge percentage alone is too
  coarse for a short sleep test; use a long battery-runtime test only as a
  secondary cross-check.
- For each scenario, report steady-state median current, peak current, energy
  per voice turn or recorded minute where applicable, and full runtime. Until
  an analyzer trace or labeled-capacity runtime exists, current and watt-hour
  fields remain unknown rather than inferred from voltage slope.

Import a synchronized battery-path analyzer trace with:

```sh
python3 scripts/lunasay_power_report.py --analyzer-csv analyzer.csv
```

The CSV requires `run_id`, positive-discharge `current_ma`, one time column
(`epoch_s` or ISO-8601 `timestamp`), and one voltage column (`voltage_mv` or
`voltage_v`). The report trapezoid-integrates charge and energy, records average
and peak current, and calculates direct mWh per successful conversation turn,
per recorded journal minute, or per scheduled BLE configuration-set interval.
The BLE interval value includes intervening standby and settings reads; it is a
workload-cycle value, not isolated GATT transaction energy.
Without such a trace, those direct fields remain unknown; a labeled-capacity
runtime is reported separately as
`capacity-derived` current. A trace must span at least 95% of the attributed
VBUS-off window to receive the `direct-battery-analyzer` label; shorter traces
are retained but explicitly marked `direct-battery-analyzer-partial`, and their
energy is not used for per-turn or per-recorded-minute claims.

## True deep sleep gate

The current ESP-IDF `sleep-offline` scenario is a display-off, radio-off idle
baseline; the CPU remains awake. It must not be called deep sleep in reports.
The previous Arduino implementation used ESP32 timer wake plus active-low GPIO0
wake. The ESP-IDF port now implements the following behavior, but it remains
unqualified until the hardware gates below pass:

1. Reject entry while VBUS is present or a voice/OTA/write transaction is active.
2. Mute the amplifier, stop I2S, stop Wi-Fi/BLE, blank the AMOLED, and quiesce
   safe AXP2101 peripheral rails without disabling the ESP32 supply.
3. Persist the test start, requested wake interval, starting voltage/percentage,
   firmware build, and wake reason across reset.
4. Wake by BOOT/GPIO0 or timer, restore all required rails, and expose the completed
   interval in serial/API telemetry.
5. Demonstrate BOOT-button wake and timed wake on battery before beginning a long run.

Run the non-publishable timer smoke gate first:

```sh
python3 scripts/lunasay_deep_sleep_validate.py \
  --duration-min 2 --wake-source timer --allow-not-ready
```

Then run the physical wake gate and press BOOT only after the runner prints
`awaiting_gpio0`:

```sh
python3 scripts/lunasay_deep_sleep_validate.py \
  --duration-min 5 --wake-source gpio0 --boot-timeout-s 120 --allow-not-ready
```

Both gates require the device to disappear from the network, return on the
expected wake cause, restore Wi-Fi and `/api/battery`, retain start telemetry,
and wake within the permitted time window. A deep-sleep claim requires a
passing GPIO0 artifact for every unit contributing direct-current projection
evidence; timer evidence alone cannot open the claim gate.

After flashing a new release candidate, run the non-publishable post-flash
smoke matrix before any charged qualification run:

```sh
python3 scripts/lunasay_power_matrix_run.py \
  --matrix config/lunasay_power_smoke_matrix.json \
  --state artifacts/qa/lunasay-power-smoke-state.json \
  --unit-id luna-dev1 --battery-id smoke-cell --allow-not-ready
```

It exercises full, dim, and display-off STT→LLM→TTS; near-continuous journal
capture; real BLE settings reads and verified writes; cleanup after every hub
cycle; and timed deep sleep. All six tests remain `functional-only`. Delete or
choose a new state file after changing firmware so an earlier smoke completion
cannot be reused; the matrix hash prevents reuse after the smoke definition
itself changes. The physical BOOT/GPIO0 gate remains a separate operator action.

The board's separate PWR key connects to the AXP2101 `PWRON` input. Its `PWROK`
output controls ESP32 reset, while `AXP_IRQ` is not routed to an ESP GPIO in the
official schematic. Therefore firmware must not describe the PWR key as an
ESP32 deep-sleep wake source; only BOOT/GPIO0 and the timer are asserted here.

## Evidence levels

- **Functional only:** mode applied correctly, source reports `battery`, and the
  expected display/radio/voice behavior works. Firmware counters and retained
  history prove the commanded display mode and radio state; they do not measure
  panel luminance. Verify full and dim brightness visually (or with an optical
  meter) during post-flash QA. No battery-life claim.
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

After flashing and completing the short timer/BOOT wake gates, run the matrix
sequentially with a dedicated state file for each physical unit and battery:

```sh
python3 scripts/lunasay_power_matrix_run.py \
  --unit-id luna-rc1 --battery-id cell-serial-from-label \
  --battery-mah CAPACITY_FROM_LABEL --battery-photo /path/to/battery-label.jpg
```

The orchestrator resumes completed test IDs, binds its state to the matrix
SHA-256, source/harness build, and device firmware build; records each exact test
definition and command; waits for charge termination and the required rest
before every run; and stops at the first
failure. A changed matrix requires a new state file rather than silently reusing
stale completions. Use `--dry-run` to review all commands. `--allow-not-ready`
is only for smoke tests and permanently classifies those artifacts as
unqualified. Qualified runners copy the battery-label photo into every artifact
directory and store its SHA-256 with the labeled capacity.

BLE-only runs use the non-persistent firmware command `ble power-test on` and
the native `scripts/lunasay_ble_probe.swift` CoreBluetooth client. Advertising
runs scan for liveness. The `ble-config` workload connects to the settings GATT
service and fetches and validates the settings JSON every minute. Once per hour
it writes only the current epoch, then reads it back; the lower write cadence
avoids needless NVS wear while still testing a real setting transaction. It
deliberately does not echo the returned Wi-Fi object,
because the read representation omits the password and echoing it would replace
a stored credential with an empty password. The runner sends `ble power-test
off` after recovery; neither power-test command changes the owner's saved BLE
preference.

The generated `report.md`, `runs.csv`, `deep_sleep_runs.csv`, `curves.csv`,
`curves.svg`, and analyzer-input hash manifest `analyzer_sources.json` live under
`artifacts/qa/lunasay-power-report/` by default.
