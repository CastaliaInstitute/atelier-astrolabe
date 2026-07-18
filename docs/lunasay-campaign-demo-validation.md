# LunaSay campaign demo validation

Use the operator-driven validator before recording the Kickstarter hero video.
It exercises the complete physical-device sequence repeatedly and preserves
failures instead of allowing a polished single take to conceal them.

## Release gate

- Complete 20 uninterrupted runs on the same hardware and firmware intended
  for filming.
- Pass at least 19 complete runs.
- Each individual feature must also pass at least 19 times.
- Use real profiles/data and real service responses. Never credit a mock,
  simulated screen, edited response, or explanatory animation as a prototype
  result.
- Record voice response-start latency. The default gate is 20 seconds and can
  be tightened with `--max-voice-latency` once the campaign claim is fixed.
- Add `--offline-claim` only if offline behavior will be stated or shown in the
  campaign. The resulting check then becomes mandatory.

The default sequence is:

1. Wake into a current, readable Moon face.
2. Display the consent-safe test profile's natal chart.
3. Display current transits and verify one named aspect against the approved
   reference.
4. Compare two consent-safe test profiles in Synastry and verify one named
   aspect.
5. Hold to ask, “What is most active in my chart tonight?”, release, and allow
   the real answer and latency to remain evident. The validator uses the Mac's
   `say` command as the user so every run receives the same spoken prompt.
6. Return to Moon or the dock without unsolicited audio, a feed, or a stuck
   listening state.

## Run it

From the repository root:

```bash
./scripts/lunasay_campaign_demo_validate.py \
  --operator "Operator name" \
  --device-id "LUNASAY-PROTOTYPE-01" \
  --firmware "release-candidate identifier" \
  --say-voice Samantha \
  --say-rate 175
```

For each voice turn, hold the device's talk control and press Enter. The Mac
plays the prompt through its speakers. Release the talk control when playback
ends, then press Enter when LunaSay's intelligible response begins. The elapsed
time from release to response is recorded as voice latency. Set the Mac speaker
volume and placement before the first run and do not change them during the
session.

To validate a campaign offline claim:

```bash
./scripts/lunasay_campaign_demo_validate.py --offline-claim
```

To inspect the procedure without starting a session:

```bash
./scripts/lunasay_campaign_demo_validate.py --print-checklist
```

Each session writes `summary.json` and `report.md` under
`artifacts/qa/lunasay-demo-<timestamp>/`. Partial results are rewritten after
every run and retained if the operator interrupts the session.

The process exits zero only when the complete release gate passes. A shortened
bench check can use `--runs 1 --min-passes 1`, but it is not campaign-ready
evidence and will retain a `NOT READY TO FILM` verdict.
