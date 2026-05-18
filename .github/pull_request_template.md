## Summary

<!-- What changed and why (1–3 sentences). -->

**Base branch:** `integration` (not `main`)

Closes #

## Backlog

- [ ] [`docs/BACKLOG.md`](docs/BACKLOG.md) updated (In progress → Done with date)

## Test plan

- [ ] `./scripts/build.sh` (or `pio run -e waveshare_s3_175`)
- [ ] **Clock faces / touch / buttons touched?**
  - [ ] **Firmware build** green (merge gate)
  - [ ] **Integration sim gate** green (QEMU) — optional; runs after build
  - [ ] Optional before **main** promotion: **Integration device gate** on m1 or `./scripts/functional_test.py --flash`
- [ ] Flashed / smoke-tested on hardware before promoting to `main` (if applicable)
- [ ] Optional visual QA: `screen.bmp` ([`hardware-qa.mdc`](.cursor/rules/hardware-qa.mdc))

## Gates

<!-- Sim gate run (merge). Hardware device gate run (promotion to main). -->

## Notes

<!-- Edge functions to deploy in mynah, secrets, follow-up issues. -->
