# LunaSay Kickstarter launch readiness

**Target:** campaign live Saturday, August 1, 2026 (Lughnasadh)
**Status date:** July 18, 2026
**Source branch:** `integration`
**Decision:** August 1 is a campaign-launch target, not a claim that the
fulfillment firmware or production line is finished.

This is the single source of truth for the LunaSay Kickstarter go/no-go. A box
is checked only when its evidence link exists. A working feature is not the
same thing as a launch claim: every public claim must match the photographed
prototype, the tested firmware, and the costed reward.

## Campaign promise

**Title:** LunaSay — A Pocket Astrolabe for the People You Love

**Subtitle:** Moon, astrology, synastry, tarot, and intentional voice on a
beautiful round instrument that listens only when invited.

**One-sentence promise:** LunaSay turns the sky, your birth chart, and the
relationships you choose into calm daily faces, then lets you ask a spoken
question with an unmistakable push-to-talk gesture.

The hero demonstration may show these seven working prototype faces:

1. Moon — current phase and lunar terrain.
2. Astrology — a saved natal profile.
3. Transits — current movement against the saved chart.
4. Synastry — a weather-like relationship outlook based on two consent-safe
   profiles.
5. Tarot — a deliberate question and card reflection.
6. Alethiometer — listen to a question, choose three symbols with animated
   searching arrows, then make a second interpretation call and reveal the
   answer arrow.
7. Sky — a quiet astronomical view and return point.

The campaign must say that spoken interpretation currently requires Wi-Fi and
a cloud STT/LLM/TTS path. It must not imply always-listening, offline AI,
diagnostic certainty, objective relationship prediction, or guaranteed
astrological accuracy.

## August 1 critical path

All times are America/Denver. Kickstarter launches are manual; there is no
scheduled automatic launch.

| Due | Owner | Deliverable | Gate |
|---|---|---|---|
| Jul 18 | Founder + firmware | Freeze the seven-face demonstration and public claims | No new launch features afterward |
| Jul 19 | Founder | Freeze reward names, quantities, provisional prices, regions, and exclusions | Every physical reward has a cost row |
| Jul 20, 12:00 | Founder | Complete all Kickstarter editor tabs, identity, payment, risks, timeline, rewards, and working-prototype media | Ready to send to review |
| Jul 20 | Founder | Submit Design & Technology project for manual review | Hard deadline for review buffer |
| Jul 21 | Firmware | Build and flash the LunaSay release candidate; record revision, binary hash, device ID, and configuration | One immutable filming candidate |
| Jul 21–22 | Demo team | Run the 20-run campaign validation protocol | At least 19/20 complete runs and every check 19/20 |
| Jul 22 | Hardware | Record named battery scenarios and charging-stand behavior | Publish measurements, not estimates |
| Jul 22 | Operations | Obtain written 100/500/1,000-unit manufacturing, freight, packaging, and fulfillment quotes | Replace every provisional cost |
| Jul 23 | Founder | Activate the approved prelaunch page and publish the first proof update | Minimum one-week runway if approval is prompt |
| Jul 24 | Creative | Film the unedited hero flow and physical-product stills | No photorealistic product renders |
| Jul 25 | Founder + operations | Freeze campaign story, budget, delivery schedule, service allowance, risks, FAQ, shipping/tax language | Claims match evidence and quotes |
| Jul 26 | Firmware | Perform signed LunaSay OTA update, deliberate interruption/rollback test, and recovery flash | Preserve logs and hashes |
| Jul 27 | Founder | Final page review on phone and desktop; proof every reward, link, date, quantity, and currency | Zero placeholders |
| Jul 28 | Founder | Respond to any Kickstarter review request and resubmit | Allow another review cycle |
| Jul 29 | Founder | Make final go/no-go decision using the gates below | No exceptions hidden as “later” |
| Jul 30–31 | Founder | Rehearse launch, comments, live demo, support, and launch communications | Manual launch credentials confirmed |
| Aug 1 | Founder | Manually launch and publish the Lughnasadh message | Launch only if all hard gates pass |

## Hard go/no-go gates

### Kickstarter and campaign

- [ ] Project approved by Kickstarter.
- [ ] Working prototype is shown performing every claimed interaction.
- [ ] Campaign uses photographs or clearly labeled diagrams; no photorealistic
  rendering is presented as the product.
- [ ] Production plan, creator experience, risks, service dependence, and
  limitations are stated plainly.
- [ ] Launch-day email, social copy, live-demo link, and first update are ready.
- [ ] At least 30% of the funding goal is represented by price-qualified,
  explicit day-one commitments. Followers alone do not pass this gate.

### Physical demonstration

- [x] One real LunaSay prototype completes an STT–LLM–TTS turn on every featured
  face. Evidence:
  [`../artifacts/qa/lunasay-voice-soak-20260717-232655/summary.json`](../artifacts/qa/lunasay-voice-soak-20260717-232655/summary.json)
- [x] Alethiometer performs the two-call question/interpretation ritual and
  completes physical audio playback. Evidence:
  [`../artifacts/qa/lunasay-voice-soak-20260718-010517/summary.json`](../artifacts/qa/lunasay-voice-soak-20260718-010517/summary.json)
- [x] The privacy-hardened flashed build completes the full physical seven-face
  Mac `say` tour with HTTP 200, intelligible playback, and no crash on every
  face. Evidence:
  [`../artifacts/qa/lunasay-voice-soak-20260718-051136/summary.json`](../artifacts/qa/lunasay-voice-soak-20260718-051136/summary.json)
- [x] The final OTA/settings-readback image completes a fresh physical Moon
  STT–LLM–TTS smoke after factory/product recovery, with HTTP 200, audio
  playback, and no crash. Evidence:
  [`../artifacts/qa/lunasay-voice-soak-20260718-060300/summary.json`](../artifacts/qa/lunasay-voice-soak-20260718-060300/summary.json)
- [ ] The filming release candidate passes the 20-run campaign protocol at
  19/20 or better. Procedure:
  [`lunasay-campaign-demo-validation.md`](lunasay-campaign-demo-validation.md)
- [ ] Every face has a legible physical-device still and a short unedited clip.
- [ ] Touch/swipe, push-to-talk, voice playback, dock behavior, and quiet return
  are demonstrated on the same candidate.
- [ ] No crash, stuck listening state, unsolicited audio, progressive heap loss,
  or unexplained reboot occurs during the release run.

### Firmware, update, and recovery

- [x] LunaSay has a distinct compiled identity and OTA channel:
  `LunaSay` / `astrolabe-lunasay-175`.
- [x] The OTA publishing workflow builds and signs a LunaSay artifact independently
  of Faculty and Cyber.
- [ ] The resulting integration manifest and firmware are published at
  `releases/integration/astrolabe-lunasay-175/`.
- [ ] A representative device accepts the valid signed LunaSay image and rejects
  a wrong-channel or corrupt image.
- [ ] An interrupted update rolls back or reaches the documented recovery path.
- [x] A representative device rejects an incorrect SHA-256 without changing the
  factory slot, installs the correctly hashed LunaSay image, enters factory
  recovery without the BOOT button, and returns to the product OTA slot. This
  is local integrity/recovery evidence, not the still-open signed-manifest or
  interrupted-transfer gate. Evidence:
  [`../artifacts/qa/lunasay-ota-recovery-20260718-055234/summary.json`](../artifacts/qa/lunasay-ota-recovery-20260718-055234/summary.json)
- [ ] USB Serial/JTAG recovery is photographed for support; the operator
  procedure is documented in
  [`lunasay-usb-ota-recovery.md`](lunasay-usb-ota-recovery.md).
- [ ] Release candidate revision, build command, SHA-256, device identity, NVS
  variant, and OTA manifest are preserved with the QA report.

### Power and hardware

- [ ] Canonical SKU is frozen: board, display, enclosure, battery, microphone,
  speaker, PMU, dock/base, cable, finish, and packaging.
- [ ] Clock-only, typical mixed use, heavy voice, and docked runtime/thermal
  scenarios are measured on the campaign firmware.
- [ ] Docked power-saving behavior and charging transitions pass an overnight test.
- [ ] Three production-representative enclosures survive repeated demonstrations.
- [ ] Battery transport documentation and the required compliance plan are on file.

### Economics and delivery

- [ ] Written quotes exist at 100, 500, and 1,000 units, with lead times and
  quote-expiration dates.
- [ ] Critical components have alternates or a disclosed supply risk.
- [ ] Tooling, compliance, labor, packaging, inbound freight, duties/tariffs,
  fulfillment, failed payments, warranty/returns, cloud allowance, platform and
  payment fees, taxes, and contingency are included.
- [ ] The project can deliver every reward if it funds at exactly 100%.
- [ ] Delivery date includes engineering, pilot build, certification, volume
  production, ocean/air movement, customs, fulfillment, and a schedule buffer.

## Quantity, rewards, and provisional economics

**Recommended first campaign cap: 500 complete LunaSay devices plus 5% service
spares.** Unlocking a second batch should require a confirmed supplier slot,
not merely overfunding. A 1,000-unit promise is premature until quotes and pilot
yield exist.

Provisional reward architecture:

| Reward | Quantity | Working price | Purpose |
|---|---:|---:|---|
| Lughnasadh first light | 50 | $169 | Strictly limited launch reward |
| Early LunaSay | 150 | $179 | Early adopter reward |
| Kickstarter LunaSay | 300 | $199 | Core campaign reward |
| Supporter / field notes | Unlimited | $10 | Non-hardware support |

The 500-device hardware mix yields a weighted hardware pledge of **$190 per
device** before shipping and tax. Treat this as a pricing experiment, not an
approved funding goal.

Known per-device inputs supplied so far are **$40 for the main device plus
freight and related costs**, and **$3 for the base**. Everything below remains
a quote-required placeholder:

| Per-device cost | Current input | Status |
|---|---:|---|
| Main device | $40 | Founder assumption; supplier quote required |
| Base/dock | $3 | Founder assumption; supplier quote required |
| Enclosure/finish and assembly | TBD | Blocking |
| Provisioning and functional test labor | TBD | Blocking |
| Packaging and cable | TBD | Blocking |
| Inbound freight, duty, and tariff | TBD | Blocking |
| Fulfillment pick/pack | TBD | Blocking |
| Warranty, returns, and 5% spares | TBD | Blocking |
| Included cloud voice allowance | TBD | Blocking |

Kickstarter currently charges 5% and payment processing is roughly 3–5% on a
successfully funded project. Use **10%** as the working combined fee assumption
until the project budget supplies the exact geography/payment mix. Add at least
a separate **10% contingency** to cost, not to hoped-for profit.

Do not choose the final goal by multiplying the board cost by the number of
units. The minimum safe gross funding goal is:

```text
((all fixed launch/production costs)
 + (reward quantities × fully landed variable cost)
 + labor and support
 + contingency)
/ (1 - platform/payment fee rate - failed-payment reserve)
```

Shipping and applicable tax should be collected separately through a disclosed
method, but packing, freight to the fulfillment center, and fulfillment setup
still belong in the project budget. Final prices, goal, and regions remain a
hard no-go until the quote-required rows are replaced.

The auditable working model is
[`../config/lunasay_kickstarter_economics.json`](../config/lunasay_kickstarter_economics.json).
Run it with:

```bash
./scripts/lunasay_kickstarter_economics.py --allow-incomplete
```

Without `--allow-incomplete`, it exits nonzero until every required cost has a
number. Its incomplete result is a known-cost lower bound, never a funding goal.

## Filming storyboard

Use one continuous master take with a visible clock; close-ups may clarify the
screen but must not conceal latency or substitute simulated output.

1. Wake LunaSay on its inexpensive dock; show the physical button and the Moon.
2. Swipe Moon → Astrology → Transits; name one verifiable live detail.
3. Swipe to Synastry; show two consent-safe family profiles and the ten-day
   weather analogy without claiming objective prediction.
4. Hold to ask a short relationship question; release and preserve real latency.
5. Swipe to Tarot and ask for a reflection.
6. Swipe to Alethiometer; show the searching arrows, three selected symbols,
   second interpretation step, answer arrow, and spoken answer.
7. Swipe to Sky, return to Moon, and leave the device quiet on the dock.
8. End on the real prototypes, firmware revision, test tally, and the invitation:
   “Meet us at Lughnasadh.”

## Evidence index

- Campaign repeatability procedure:
  [`lunasay-campaign-demo-validation.md`](lunasay-campaign-demo-validation.md)
- Physical voice/face runner:
  [`../scripts/lunasay_voice_soak.py`](../scripts/lunasay_voice_soak.py)
- Operator-driven 20-run validator:
  [`../scripts/lunasay_campaign_demo_validate.py`](../scripts/lunasay_campaign_demo_validate.py)
- Product development gates: [`development-plan.md`](development-plan.md)
- Marketing strategy: [`marketing-strategy.md`](marketing-strategy.md)
- Campaign page, rewards, risks, and FAQ draft:
  [`lunasay-kickstarter-campaign-draft.md`](lunasay-kickstarter-campaign-draft.md)
- Email, social, personal-outreach, and comment-response sequence:
  [`lunasay-kickstarter-launch-comms.md`](lunasay-kickstarter-launch-comms.md)
- Audited privacy and data flow:
  [`lunasay-privacy-and-data-flow.md`](lunasay-privacy-and-data-flow.md)
- Unit-economics model:
  [`../config/lunasay_kickstarter_economics.json`](../config/lunasay_kickstarter_economics.json)
- Unit-economics auditor:
  [`../scripts/lunasay_kickstarter_economics.py`](../scripts/lunasay_kickstarter_economics.py)
- OTA deployment workflow:
  [`../.github/workflows/deploy-astrolabe175c-ota.yml`](../.github/workflows/deploy-astrolabe175c-ota.yml)
- OTA integrity/recovery validator:
  [`../scripts/lunasay_ota_recovery_validate.py`](../scripts/lunasay_ota_recovery_validate.py)
- USB Serial/JTAG and no-BOOT-button recovery procedure:
  [`lunasay-usb-ota-recovery.md`](lunasay-usb-ota-recovery.md)

## Decision record

On July 29, record GO or NO-GO here with links to the campaign preview, approved
Kickstarter review, demand count, final budget, release-candidate QA report,
battery report, OTA/recovery report, and manufacturing quotes. If a hard gate
is open, keep the prelaunch page active and announce a new date; do not convert
an engineering unknown into a backer promise.
