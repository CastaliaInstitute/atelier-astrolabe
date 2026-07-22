# LunaSay Kickstarter marketing operations

**Internal operating package — do not publish verbatim.**

**Launch intent:** Saturday, August 1, 2026 — Lughnasadh

This is the execution brief for the LunaSay campaign. It turns the campaign
draft, communications library, economics model, and release gates into one
owner-ready plan. A meaningful launch date is not permission to make an
unverified product, delivery, battery, privacy, or funding claim. The hard
gates in [lunasay-kickstarter-launch-readiness.md](lunasay-kickstarter-launch-readiness.md)
remain controlling.

## 1. Positioning and message system

### The position

**LunaSay is a pocket astrolabe for people who want a beautiful, intentional
way to notice the sky, their relationships, and a question worth asking.**

It is a real round instrument—not a phone app, an always-listening assistant,
or a promise that symbolic systems can determine a person's future.

### The promise, in layers

| Use | Copy |
|---|---|
| Title | **LunaSay — A Pocket Astrolabe for the People You Love** |
| Subtitle | **Moon, astrology, synastry, tarot, and intentional voice in a round instrument that listens only when invited.** |
| One sentence | LunaSay turns the Moon, a chosen birth chart, and the relationships you choose into calm daily faces, then lets you ask a spoken question with an unmistakable hold-to-talk gesture. |
| Ten-second spoken pitch | “LunaSay is a pocket astrolabe: a small round object for the Moon, astrology, people you love, tarot, and a voice question—only when you choose to ask.” |
| Primary CTA | **Follow the Kickstarter prelaunch page** before launch; **choose a reward** after launch. |

### Four proof messages

1. **A real object, not a rendering.** Show hands, the display, the dock, the
   physical control, wake, swipe, and real response wait in the same take.
2. **A ritual instead of a feed.** A glance at the Moon or Sky must feel useful
   even when no question is asked.
3. **Voice by invitation.** The listener action is visible: hold, speak,
   release. Cloud speech and interpretation require Wi-Fi; do not call it
   offline AI or imply ambient recording.
4. **Relationship weather, not a score.** Synastry is a symbolic prompt about
   two chosen, consent-safe profiles. It neither rates people nor predicts
   outcomes.

### Language guardrails

Use: *pocket astrolabe, instrument, intentional, hold-to-talk, working
prototype, symbolic reflection, relationship weather, cloud voice.*

Do not use: *always listening, mind-reading, compatibility score, prediction,
medical/relationship advice, offline AI, unlimited/lifetime AI, replaces your
phone.* Describe the Alethiometer as a reflective interaction, never as an
oracle or source of facts.

## 2. Kickstarter page: ready-to-fill editorial brief

### Above the fold

**Hero headline:** A sky you can hold.

**Hero copy:** Most digital tools ask for more attention. LunaSay is made for a
different ritual: pick it up, notice what is present, ask a question if you
wish, and let it become quiet again.

**Proof line beneath video:** This is a working physical prototype. The film
shows real interactions and real cloud-voice latency. Screen graphics, if any,
are labeled as graphics.

**Button:** Follow LunaSay / Back LunaSay (replace at launch).

### Recommended page order

1. Hero film and physical-object still.
2. “What it is” — the one-sentence promise above.
3. Seven faces, one calm visual and one sentence each: Moon, Astrology,
   Transits, Synastry, Tarot, Alethiometer, and Sky.
4. “Ask only when you choose” — hold-to-talk sequence, Wi-Fi/cloud boundary,
   and no-ambient-listening statement.
5. “What is local, what uses Wi-Fi” — a simple, verified matrix, linked to
   the data-flow document.
6. The physical object — final dimensions, materials, box contents, dock,
   battery results, and accessibility notes only after they are measured.
7. Proof before polish — 20-run result, firmware revision/hash, real latency
   summary, a continuous clip, and an honest failure note if relevant.
8. Rewards, shipping/tax, cloud allowance, and delivery schedule.
9. Production plan and risks: prototype → engineering validation → compliance
   → pilot → production → fulfillment.
10. FAQ, creator/team credibility, and the closing invitation.

### Face copy

| Face | Public copy | Required proof |
|---|---|---|
| Moon | “A textured lunar world for the phase you are living through.” | Physical still with date/location note. |
| Astrology | “Carry a chosen birth chart as a daily object.” | Consent-safe profile and physical still. |
| Transits | “A symbolic calendar of current movement against a saved chart.” | Physical still and checked example. |
| Synastry | “Relationship weather: one condition now, ten days around the dial.” | Two consent-safe profiles, still, and clear non-score disclosure. |
| Tarot | “Ask a question; receive a card and a bounded reflection.” | Physical card still and real voice clip. |
| Alethiometer | “Three arrows search for question symbols; a fourth searches for an answer.” | Continuous physical two-call clip, including wait. |
| Sky | “A quiet astronomical view, with nowhere else to go.” | Physical still. |

### Core FAQ answer that must be exact

**Is LunaSay always listening?** No. The campaign interaction is
hold-to-talk: hold, speak, release. There is no campaign promise of a wake word
or ambient recording.

**Does voice work without Wi-Fi?** The demonstrated speech-to-text,
interpretation, and spoken response require Wi-Fi and cloud services. The page
must identify exactly which visual functions remain useful without a network.

Use the fuller approved working copy in
[lunasay-kickstarter-campaign-draft.md](lunasay-kickstarter-campaign-draft.md).

## 3. Rewards, quantity, and campaign target recommendation

### Recommended first-run offer

| Reward | Quantity | Working price | Reason |
|---|---:|---:|---|
| Field Notes Supporter | Unlimited | $10 | Lets sympathetic non-buyers participate; no hardware obligation. |
| Lughnasadh First Light | 50 | $169 | Honest launch-day scarcity; reward first committed backers. |
| Early LunaSay | 150 | $179 | Tests price elasticity without promising too many units cheaply. |
| Kickstarter LunaSay | 300 | $199 | One simple, core complete-device SKU. |

**Recommended quantity target: 500 complete devices, plus 25 service spares.**
This is a campaign cap, not automatic permission to manufacture 500 units. Do
not unlock a second batch unless a supplier slot, component allocation, pilot
yield, test capacity, and delivery window are written and confirmed.

The stated inputs establish only a **known hardware floor**: $40 for the main
device + $3 for the base/dock = **$43 per shipped-unit BOM before** enclosure
finish/assembly, testing, cable/packaging, inbound freight/duty/tariff,
fulfillment, warranty/returns, cloud allowance, fees, labor, compliance,
tooling, and contingency. At the proposed mix, 500 hardware rewards generate
$95,000 before separately collected shipping and tax, with a $190 weighted
pledge. With 5% spares, the known device-plus-dock floor is $22,575 before the
missing costs.

**Do not publish a funding goal yet.** The correct gross goal is the completed
economics model, not $95,000 or the known-cost floor. Obtain quotes at 100,
500, and 1,000 units; complete every null cost row; then run:

```bash
./scripts/lunasay_kickstarter_economics.py
```

Set the goal no lower than `minimum_safe_gross_goal`, and keep shipping/tax
separate only with clear regional and duty language. Until the model is
complete, prices and quantity are provisional.

## 4. Aug 1 / Lughnasadh launch calendar

All times are America/Denver. The founder owns decisions and manual Kickstarter
actions; the marketing owner prepares, checks, and records evidence.

| Date | Owner | Must happen | Success signal |
|---|---|---|---|
| Jul 21 | Firmware + creative | Freeze photographed release candidate; start 20-run validation; name each asset required for the page. | Revision, hash, device ID, and asset checklist recorded. |
| Jul 22 | Operations | Request/collect 100/500/1,000-unit manufacturing, packaging, freight, fulfillment, and compliance quotes; run battery scenarios. | Every cost owner has a quote deadline; no unmeasured runtime claim. |
| Jul 23 | Founder | Complete Kickstarter preview and submit it for review; publish prelaunch page only after approval. | Review submitted; prelaunch URL is live when approved. |
| Jul 24 | Creative | Film continuous master demo plus physical stills. | Seven stills and master take exist; no simulated interaction. |
| Jul 25 | Marketing | Send invitation email; post Moon/hero proof; begin personal, one-to-one outreach. | New qualified follows and price-qualified commitments logged. |
| Jul 26 | Firmware + ops | Complete OTA/recovery proof; publish/update clear privacy and local/cloud explanation. | Evidence link and review-ready claim matrix. |
| Jul 27 | Marketing | Publish Synastry proof and Alethiometer clip; answer objections in a shared FAQ tracker. | Engagement is answered within one business day. |
| Jul 28 | Founder | Publish “proof before polish” with validation tally, latency, and any failure note; resubmit review if needed. | Zero required placeholders in campaign preview. |
| Jul 29 | Founder | Go/no-go meeting against hard gates and demand coverage. | Written GO or NO-GO; no soft exception. |
| Jul 30 | Team | Rehearse launch, live demo, comments, support replies, and physical backup device. | Timed rehearsal succeeds; launch roles assigned. |
| Jul 31 | Marketing | Send “tomorrow” email; post launch-time reminder; confirm links and reward inventory. | Every link resolves; creator can manually launch. |
| Aug 1 | Founder + marketing | Manually launch; send live email; post launch copy; host a short real-device demo; issue first update within 12 hours. | Backers, funding, source attribution, questions, and incidents logged hourly for first 12 hours. |

If Kickstarter approval, the evidence gates, or completed economics are absent
on July 29, keep the prelaunch page live and choose a new date. Lughnasadh is a
narrative frame, not a delivery promise.

## 5. Creator video and product photography plan

### 90-second creator-video master

Film a continuous master take first; capture close-ups only as explanatory
cutaways. Never edit away a device failure or cloud wait while presenting it as
a live sequence.

| Time | Frame/action | Spoken or on-screen point |
|---:|---|---|
| 0–8 s | LunaSay wakes on the dock, in hand. | “This is LunaSay, a pocket astrolabe.” |
| 8–20 s | Physical Moon face, then a single hand swipe. | “It begins with a glance at the Moon.” |
| 20–34 s | Astrology and Transits; keep the device and hand visible. | “A chosen chart and the movement around it.” |
| 34–47 s | Synastry weather dial with consent-safe profiles. | “Relationship weather, not a compatibility score.” |
| 47–58 s | Tarot reflection. | “A question can have room for reflection.” |
| 58–75 s | Alethiometer: hold-to-talk, question, three gold arrows, answer arrow, actual wait and audio. | “It listens only while I hold the control. Voice uses Wi-Fi.” |
| 75–83 s | Sky face and return to quiet/dock. | “Then it becomes quiet again.” |
| 83–90 s | Founder with real prototype(s). | “We are funding the responsible path from prototype to first run.” |

### Required stills

1. **Hero:** hand + Moon face + dock, screen legible, no composited UI.
2. **Object honesty:** front, three-quarter, side, back, dock, cable, and a
   scale-reference shot; label prototype versus final-production details.
3. **One still per face:** Moon, Astrology, Transits, Synastry, Tarot,
   Alethiometer, Sky. Maintain identical lighting and a readable display.
4. **Intentional voice:** one frame that makes the physical hold action and
   listening screen unmistakable.
5. **Proof:** bench/QA evidence, but never use debug imagery as the hero.
6. **Founder/team:** real people with the actual object, manufacturing or
   testing context, no stock “team” image.

### Shoot checklist

- Clean the display and dock; remove temporary labels only if doing so does not
  misrepresent the prototype.
- Lock exposure, focus, white balance, and device time/location before takes.
- Record enough room audio to hear the device; separately capture a close mic
  only when labeled as such.
- Use fictional or explicitly consented birth/family data; obscure no claim by
  later masking a screen.
- Preserve raw continuous takes, revision/hash, date, device identity, and
  accompanying test report in the asset folder.

## 6. Communications and outreach

Use the ready-written email and social library in
[lunasay-kickstarter-launch-comms.md](lunasay-kickstarter-launch-comms.md).
Prioritize this sequence:

1. Invitation: the object and Aug 1 date.
2. Synastry: relationship weather, not scores.
3. Proof before polish: 20-run tally and real latency.
4. Tomorrow: exact launch time, tier quantities, and honest scope.
5. Live: one link, one clear CTA.

For personal outreach, use a specific reason: “I thought of you because you
care about lunar practice / reflective tools / intentional technology,” then
send the real-device clip. Do not mass-message strangers, imply endorsement,
or promise a reward that is not in the campaign.

## 7. KPIs, dashboard, and decision thresholds

Track daily in one source of truth. Segment every signup and pledge source:
email, Kickstarter follower, direct referral, organic social, press/creator,
or other. Do not substitute views or followers for purchasing intent.

| Stage | KPI | Target / decision rule |
|---|---|---|
| Product proof | Complete campaign runs / every-feature passes | ≥19/20 each, same release candidate. Otherwise do not film/launch. |
| Approval | Kickstarter review | Approved before public launch; no exceptions. |
| Economics | Completed quote-backed model | `launch_ready: true` and goal ≥ calculated safe gross goal. |
| Demand | Price-qualified day-one commitments | ≥30% of final funding goal. At a $95k illustrative goal, that is $28.5k, about 150 devices at the $190 weighted pledge; the actual target changes with the approved goal. |
| Prelaunch | Kickstarter follows | Track as a leading indicator, not a launch gate; target ≥2 followers for each price-qualified commitment. |
| Email | Delivery / unique click / pledge conversion | ≥98% delivery; ≥8% unique click on launch email; ≥10% of clickers reaching Kickstarter should pledge. Diagnose list/source quality, not only copy, if below. |
| Launch day | Funding / backers | ≥30% funded in 24 hours; ≥50% by 48 hours. If behind, activate personal outreach, live proof, and objection answers—not discounts or new hardware promises. |
| Campaign | Visitor-to-backer conversion | ≥3% overall; ≥5% from email/direct high-intent traffic. |
| Support | First-response time | <1 business day before launch; <2 hours during launch-day waking hours. |
| Trust | Unanswered claim/privacy questions | Zero after 24 hours; log answer and update FAQ if repeated three times. |

### Daily dashboard fields

`date, source, followers, email subscribers, qualified commitments, commitment
dollars, page visitors, pledges, pledge dollars, conversion rate, top question,
response owner, proof asset published, risk/blocker`.

## 8. Immediate operating checklist

- [ ] Put the title, subtitle, one-sentence promise, exact local/cloud matrix,
  risks, FAQ, and reward copy into a Kickstarter preview—not a live page.
- [ ] Replace every `REQUIRED:` marker in the campaign draft with evidence or
  remove the claim.
- [ ] Get written quotes; complete the economics model; choose a final goal,
  regions, and delivery date only from that result.
- [ ] Complete and preserve the 20-run physical validation on the filming
  candidate.
- [ ] Capture seven physical face stills and the 90-second continuous master.
- [ ] Submit Kickstarter review; do not announce an unapproved live date as
  certain.
- [ ] Open a simple daily dashboard and log explicit day-one commitments.
- [ ] Rehearse manual launch, support replies, live demo, and first update.

## Linked source material

- [Campaign draft](lunasay-kickstarter-campaign-draft.md)
- [Launch communications](lunasay-kickstarter-launch-comms.md)
- [Launch readiness gates](lunasay-kickstarter-launch-readiness.md)
- [Campaign demo validator](lunasay-campaign-demo-validation.md)
- [Economics model](../config/lunasay_kickstarter_economics.json)
- [Economics auditor](../scripts/lunasay_kickstarter_economics.py)
- [Privacy and data flow](lunasay-privacy-and-data-flow.md)
