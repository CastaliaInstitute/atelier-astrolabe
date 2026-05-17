## Summary

<!-- What changed and why (1–3 sentences). -->

**Base branch:** `integration` (not `main`)

Closes #

## Backlog

- [ ] [`docs/BACKLOG.md`](docs/BACKLOG.md) updated (In progress → Done with date)

## Test plan

- [ ] `./scripts/build.sh` (or `pio run -e waveshare_s3_175`)
- [ ] **Clock faces / touch / buttons touched?**
  - [ ] **Integration device gate** green on this SHA, or
  - [ ] `./scripts/functional_test.py --flash` with `artifacts/functional/latest/report.json` linked below
- [ ] Flashed / smoke-tested on hardware (if applicable)
- [ ] Optional visual QA: `screen.bmp` ([`hardware-qa.mdc`](.cursor/rules/hardware-qa.mdc))

## Device gate

<!-- Link workflow run or report path when faces/* changed -->

## Notes

<!-- Edge functions to deploy in mynah, secrets, follow-up issues. -->
