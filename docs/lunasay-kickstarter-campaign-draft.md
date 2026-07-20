# LunaSay Kickstarter campaign draft

> **Internal draft — not ready to publish.** Replace every `REQUIRED:` marker,
> attach the cited evidence, obtain Kickstarter approval, and pass the go/no-go
> gates in [`lunasay-kickstarter-launch-readiness.md`](lunasay-kickstarter-launch-readiness.md).

## Basics

**Project title:** LunaSay — A Pocket Astrolabe for the People You Love

**Subtitle:** Moon, astrology, synastry, tarot, and intentional voice on a
beautiful round instrument that listens only when invited.

**Category:** Design & Technology / Product Design

**Location:** `REQUIRED: creator/project location`

**Target launch:** Saturday, August 1, 2026 — Lughnasadh

**Campaign duration:** `REQUIRED: choose duration in Kickstarter editor`

**Funding goal:** `REQUIRED: calculated goal after written production quotes`

## Short description

LunaSay is a round, pocket-sized instrument for meaningful time. Glance at the
Moon, explore a natal chart, consider current transits, or compare two chosen
profiles through synastry. When you want a spoken interpretation, hold the
control, ask, and release. LunaSay listens only during that deliberate gesture.

We have a working physical prototype. The campaign is the next step: refine the
enclosure and firmware, complete compliance and pilot production, and make a
carefully bounded first run.

## Hero

### A sky you can hold

Most digital tools ask for more attention. LunaSay is meant to create a small
ritual instead: pick it up, notice what is present, ask a question if you wish,
and put it down again.

It is part moon clock, part personal astrolabe, and part reflective voice
instrument. It is not an always-listening assistant, a medical device, or a
machine that claims certainty about you or anyone you love.

`REQUIRED: embed the unedited 90-second working-prototype film`

**Film disclosure:** This video shows the working prototype and real service
latency. Spoken transcription and interpretation use Wi-Fi and cloud services.
Explanatory titles are identified as graphics; they are not simulated product
screens.

## What LunaSay does

### Follow the Moon

The Moon face shows the current lunar phase as a textured world rather than a
flat icon. It is the quiet home of LunaSay: useful at a glance and beautiful on
the dock.

`REQUIRED: physical Moon-face photograph and measured date/location note`

### Keep a natal chart close

Store a chosen birth profile on the device and carry its chart as a daily
object. Birth date, time, and location are entered deliberately through the
local setup interface.

`REQUIRED: consent-safe physical Astrology-face photograph`

### See current transits

The Transits face places current planetary movement in conversation with the
saved chart. It is a symbolic calendar and reflection aid, not a scientific
forecast or diagnosis.

`REQUIRED: physical Transits-face photograph plus one independently checked example`

### Explore synastry without turning people into scores

Choose two consent-safe profiles and LunaSay presents their relationship
weather: one central condition and a ten-day dial. Weather is the right
metaphor because relationships are lived, changing systems—not compatibility
percentages.

The face offers prompts for attention and conversation. It does not infer
private facts, judge a relationship, or predict an inevitable outcome.

`REQUIRED: physical Synastry-face photograph and family-data consent statement`

### Ask Tarot for a reflection

Tarot is presented as an intentional reflective practice. Ask a question,
receive a card, and hear a bounded interpretation. The card is not represented
as factual advice or guaranteed divination.

`REQUIRED: physical Tarot-face photograph and real voice clip`

### Consult the Alethiometer

Hold the control and ask a question. Three gold arrows move while LunaSay
searches for the symbols that frame the question. They settle one by one. A
blue answer arrow then searches and settles before LunaSay speaks.

The working prototype uses two distinct interpretation steps: one to choose the
three question symbols, and another to consider those fixed symbols and choose
the answer. The movement is part of the ritual; the result is an invitation to
reflection, not an oracle of objective truth.

`REQUIRED: continuous physical Alethiometer clip showing real latency`

### Return to the Sky

The Sky face closes the tour with a quiet astronomical view. LunaSay does not
need a feed, a notification stream, or an unsolicited voice to remain useful.

`REQUIRED: physical Sky-face photograph`

## Intentional voice

LunaSay's microphone interaction is visible and physical:

1. Hold the talk control.
2. The screen shows that LunaSay is listening.
3. Speak your question.
4. Release the control.
5. LunaSay transcribes, interprets, and speaks the response.

There is no wake word in the campaign prototype and no promise of ambient,
always-on recording. Voice questions currently travel over a network connection
to cloud speech and language services. The release build must use verified
HTTPS without an automatic plaintext downgrade. The answer returns as audio for
playback on the device. `REQUIRED: link the final data-flow diagram, retention policy, cloud
providers, deletion behavior, and service allowance before publication.`

## What works locally and what needs Wi-Fi

| Capability | On device | Wi-Fi/cloud required | Campaign claim |
|---|---|---|---|
| Display and face navigation | Yes | No | Working prototype |
| Moon and astronomical calculations | `REQUIRED: verify exact boundary` | `REQUIRED` | Do not publish until verified |
| Saved family/birth profiles | Device NVS | No for storage | Working prototype; document deletion |
| Astrology/transit rendering | `REQUIRED: verify cache/calculation boundary` | `REQUIRED` | Do not publish until verified |
| Synastry rendering | `REQUIRED: verify boundary` | Spoken interpretation does | Working prototype with chosen profiles |
| STT–LLM–TTS voice response | Capture begins on device | Yes | Working prototype |
| Tarot interpretation | Face/card assets on device | Voice interpretation does | Working prototype |
| Alethiometer interpretation | Animation and symbols on device | Yes, two interpretation calls | Working prototype |
| Signed firmware update | Device verifies update | Yes to download | Integration path implemented; recovery test required |

If the network or service is unavailable, LunaSay must say so truthfully. The
campaign will not promise offline AI or unlimited lifetime cloud processing.

## The physical object

The campaign prototype uses a round 466 × 466 AMOLED display and an ESP32-S3
platform with touch, microphone, speaker, Wi-Fi, Bluetooth hardware, battery
power, and a simple dock/base. The production reward will preserve the round
LunaSay experience shown here, but final enclosure materials, dimensions,
weight, battery capacity, finish, and included cable remain subject to the
production freeze.

`REQUIRED: final physical specification table, dimensions, weight, battery,
materials, finish, box contents, accessibility notes, and production-representative photographs`

We will not quietly substitute a materially different product. If engineering
requires a visible or functional change after funding, backers will receive the
reason, evidence, schedule effect, and available choices in a campaign update.

## Rewards

Prices below are working values and cannot be published until unit economics
and the final funding goal pass review.

### $10 — Field Notes Supporter

- Campaign updates and digital LunaSay field notes
- Name on the supporter page, if desired
- No physical device or implied future discount

### $169 — Lughnasadh First Light

- One complete LunaSay from the first campaign run
- Dock/base and included cable
- Founding-backers field note
- Limited to 50
- `REQUIRED: shipping, tax, tariff, region, warranty, cloud allowance, and delivery estimate`

### $179 — Early LunaSay

- One complete LunaSay
- Dock/base and included cable
- Limited to 150
- `REQUIRED: shipping, tax, tariff, region, warranty, cloud allowance, and delivery estimate`

### $199 — Kickstarter LunaSay

- One complete LunaSay
- Dock/base and included cable
- Limited to 300 in the initial campaign cap
- `REQUIRED: shipping, tax, tariff, region, warranty, cloud allowance, and delivery estimate`

Do not add new hardware stretch goals. Additional funding may improve testing,
documentation, repair inventory, or unlock a separately confirmed production
batch; it does not authorize a larger unquoted order.

## Why Kickstarter

The prototype proves the interaction. Kickstarter funds the work between a
prototype and a responsible product: enclosure refinement, design for
manufacture, certification, pilot units, per-device testing, packaging,
production, and fulfillment.

`REQUIRED: insert the final budget graphic derived from the approved project budget`

The project will be funded all-or-nothing. The goal will represent the minimum
gross amount required to deliver the stated rewards—not an attractive but
undeliverable marketing threshold.

## Development and manufacturing plan

1. **Campaign release candidate:** freeze and preserve the photographed
   firmware, configuration, test results, and prototype identity.
2. **Engineering validation:** finalize enclosure, audio, antenna, thermal,
   charging, buttons, dock, and battery behavior.
3. **Compliance and pilot preparation:** complete applicable radio, EMC,
   battery, transport, materials, labeling, and regional work with qualified
   partners.
4. **Pilot build:** manufacture a small run; measure assembly yield, test time,
   failure modes, and packaging.
5. **Production release:** approve only after pilot findings and update the
   schedule openly if they require correction.
6. **Volume build and QA:** provision each device, run the per-unit functional
   test, quarantine failures, and preserve traceability.
7. **Fulfillment and support:** inspect incoming goods, ship by supported region,
   retain service spares, and publish continuing status updates.

`REQUIRED: manufacturer/assembler, fulfillment partner, quote dates, lead times,
production geography, tested pilot quantity, schedule dates, and team owners`

## Development timeline

Do not publish dates until written supplier, compliance, and fulfillment
estimates exist. The final schedule must include explicit buffers.

| Phase | Public estimate | Evidence required before publication |
|---|---|---|
| Campaign and design freeze | `REQUIRED` | Approved campaign scope and preserved RC |
| Enclosure/engineering validation | `REQUIRED` | DFM plan and prototype schedule |
| Compliance and pilot preparation | `REQUIRED` | Lab scope and booked/quoted lead time |
| Pilot build and corrections | `REQUIRED` | Supplier slot and pilot quantity |
| Volume production | `REQUIRED` | Quote, component allocation, and yield assumption |
| Freight and fulfillment | `REQUIRED` | Packed dimensions/weight and partner quote |
| Backer delivery | `REQUIRED` | Sum of the above plus schedule buffer |

## Proof, not polish

Before filming, the release candidate must complete the same six-minute
physical sequence at least 19 times out of 20. Every featured face must also
pass at least 19 times. We preserve failures and real response latency rather
than selecting a polished take and calling it reliability.

Current engineering evidence includes a complete physical STT–LLM–TTS face
tour and a separate successful two-call Alethiometer interaction. These are
prototype milestones, not substitutes for the formal release-candidate gate.

`REQUIRED: publish the final 20-run tally, latency distribution, firmware hash,
battery report, signed OTA/recovery report, and consent-safe photographs`

## Privacy and family data

Birth data can be deeply personal. LunaSay should contain only profiles whose
use is appropriate and consented. The campaign demo uses fictional or
explicitly consented profiles. A buyer must be able to view, edit, and remove
stored profiles and erase device configuration through the documented reset
path.

We will publish exactly:

- which profile fields remain in device NVS;
- which fields, if any, accompany a spoken cloud request;
- which providers process speech and interpretation;
- what identifiers and logs are retained;
- how deletion and factory reset work;
- what happens when authentication, Wi-Fi, or a provider is unavailable;
- the included cloud allowance and the experience after it is exhausted.

`REQUIRED: final privacy/data-flow document and network-trace verification`

## Risks and challenges

### This is a working prototype, not a finished production unit

The demonstrated interactions run on real hardware, but enclosure details,
production tooling, certification, pilot yield, and final packaging remain.
These steps can reveal changes that affect appearance, cost, or schedule. We
mitigate that risk by limiting the first run, freezing one principal SKU,
requiring a pilot build, and showing changes openly.

### Component availability and price may change

Display, board, battery, audio, and enclosure components can move in price or
lead time. Before launch we require written quotes at 100, 500, and 1,000 units
and will identify alternates for critical parts where possible. We will not
unlock a larger batch without a confirmed supplier slot.

### Battery, charging, heat, and radio performance require physical validation

Small round devices make thermal, antenna, audio, and runtime tradeoffs visible.
Published battery claims will come from named scenarios on release-candidate
firmware. Charging and abnormal-use testing are campaign blockers, not items to
discover after volume production.

### Voice depends on networks and third-party services

Transcription, interpretation, and synthesized speech currently depend on
Wi-Fi and cloud providers. Latency, outages, provider changes, and ongoing cost
are real risks. LunaSay uses explicit push-to-talk, bounded requests, visible
states, timeouts, and truthful failure behavior. We will publish the included
allowance and will not promise unlimited lifetime AI service.

### Symbolic interpretations are not objective advice

Astrology, synastry, tarot, and the Alethiometer are reflective and symbolic.
They may be meaningful without being deterministic. LunaSay is not medical,
legal, financial, psychological, or relationship advice and should not be used
as a substitute for a qualified person or direct conversation.

### Firmware defects and updates

Touch, audio, storage, networking, and power interact under tight embedded
constraints. The release process includes repeated physical voice/face tests,
long-duration testing, signed channel-specific OTA, rollback or recovery, and
a documented USB recovery path. The final reward will not be declared ready
from a clean build alone.

### Certification, batteries, freight, customs, and tariffs can delay delivery

Compliance scope, battery documentation, carrier rules, tariffs, customs, and
international fulfillment can change. We will limit shipping regions to those
we can quote and support, collect variable shipping/tax using the disclosed
campaign mechanism, and include schedule and financial contingency.

### Small-team execution

The same focus that makes LunaSay coherent also creates concentration risk. We
reduce it through one principal SKU, bounded faces, written release gates,
external manufacturing/compliance partners, preserved test evidence, service
spares, and regular updates even when news is difficult.

## Frequently asked questions

### Is LunaSay always listening?

No. The campaign interaction is push-to-talk: hold, speak, release. There is no
campaign promise of a wake word or ambient recording.

### Does voice work without Wi-Fi?

Not in the demonstrated STT–LLM–TTS path. LunaSay needs Wi-Fi for cloud speech
and interpretation. The final local/offline matrix will identify which visual
faces and cached data remain available without a connection.

### Is astrology or synastry presented as scientific prediction?

No. LunaSay presents symbolic calendars and reflective prompts. Synastry uses a
weather analogy precisely to avoid reducing a relationship to a fixed score or
inevitable outcome.

### What happens to birth information?

Profiles are deliberately entered and stored in device NVS in the current
prototype. Before publication we will provide a field-by-field data-flow and
deletion description, including what accompanies cloud voice requests.

### Is the product shown in the video real?

Yes. The campaign film will identify the working physical prototype and preserve
real response latency. Diagrams or explanatory graphics will be labeled and
will not be presented as product footage.

### What is included?

The planned complete reward includes LunaSay, its dock/base, and an included
cable. `REQUIRED: final box contents and power-adapter decision.`

### How long does the battery last?

`REQUIRED: insert measured clock-only, typical mixed-use, heavy-voice, and
docked results from the photographed release-candidate firmware.` We will not
publish an estimate as measured runtime.

### Is there a subscription?

The physical clock and local instrument must remain useful without a
subscription. Cloud voice has recurring cost, so the campaign will include a
clearly bounded allowance and explain the optional continuation before launch.
`REQUIRED: allowance, metering, price, and exhausted-service behavior.`

### Can I update or recover the firmware?

LunaSay has a distinct signed OTA channel, with channel and variant checks. The
campaign will publish the validated rollback/recovery procedure after the
release-candidate interruption test. USB Serial/JTAG is the documented physical
recovery path.

### Can I add family members?

The local setup interface can read and write chosen family profiles used by the
Synastry face. The final campaign scope, supported number of profiles, editing,
geocoding behavior, and deletion workflow will be documented before launch.

### Why launch on August 1?

August 1 is Lughnasadh, a traditional seasonal threshold associated with first
fruits and the beginning of harvest. It is an apt date to invite a working
prototype into its next, communal stage—provided every launch gate is actually
ready. The date is meaningful; it does not override a failed safety, delivery,
or honesty gate.

### When will LunaSay ship?

`REQUIRED: final evidence-based delivery window.` It will be set from quoted
engineering, certification, pilot, production, freight, customs, and
fulfillment lead times plus a schedule buffer.

### Where will you ship?

`REQUIRED: supported regions after packed-product freight, tax, tariff,
certification, and returns quotes.` Shipping, taxes, and possible import duties
must be disclosed separately from the reward price.

### What if the campaign raises more than the goal?

The initial device quantity stays capped unless a second production batch has
a confirmed quote, component allocation, test capacity, and delivery window.
Overfunding first strengthens testing, documentation, support inventory, and
contingency; it does not create unplanned hardware stretch goals.

## Creator and team

`REQUIRED: creator biography, project history, named engineering,
manufacturing, compliance, creative, fulfillment, and support responsibilities,
plus honest disclosure of anything the team has not manufactured before.`

## Closing

LunaSay begins with a simple idea: technology can help us notice without asking
to possess our attention.

Look at the Moon. Hold the people you love in mind. Ask only when you choose.
Then let the instrument become quiet again.

If that is the kind of object you want in the world, meet us at Lughnasadh.

`REQUIRED: final pledge call to action after Kickstarter approval`
