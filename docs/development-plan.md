# Astrolabe Product Development Plan

**Working plan — July 2026**
**Target:** campaign-ready humane AI pocketwatch
**Base branch:** `integration`

## 1. Outcome

Deliver one campaign-ready Astrolabe configuration that can reliably complete
this daily loop:

> Notice → capture → recall → reflect

The launch product must be an excellent pocketwatch and capture instrument
before it is treated as a general AI device. The campaign build includes:

- a fast, dependable home/time face;
- push-to-talk Commonplace capture with clear state feedback;
- an offline capture queue that survives power loss;
- reminders and intentional resurfacing;
- schedule and a compact Today card;
- a visible privacy/status surface;
- six polished, bounded faces;
- honest battery behavior and measured runtime;
- safe recovery and OTA;
- a reproducible manufacturing, provisioning, and QA process.

This plan complements [`BACKLOG.md`](BACKLOG.md), which remains the detailed
feature ledger. Marketing and launch requirements are in
[`marketing-strategy.md`](marketing-strategy.md).

## 2. Product scope

### Campaign-critical release

The first fulfillment release contains one hardware platform, one principal
firmware variant, and no more than two cosmetic finishes.

Required user-facing capabilities:

1. **Time:** local time, reliable NTP/RTC behavior, readable always-first home
   experience.
2. **Capture:** hold the physical button, speak, release, and receive an
   unambiguous saved/queued/failed result.
3. **Offline continuity:** store captures while disconnected and retry safely
   later without duplication.
4. **Recall:** browse recent items and retrieve simple keyword matches by voice.
5. **Reminders:** create time-based reminders from a capture and surface them
   without turning the device into an attention feed.
6. **Today:** next appointment, one intention/reminder, and an optional daily
   rhythm card.
7. **Privacy:** show microphone, network, authentication, pending-upload, and
   local/cloud state; permit local deletion and microphone lockout.
8. **Six faces:** Time, Commonplace, Today, Rhythms, Alethiometer, and Music.
9. **Power:** trustworthy battery percentage, dim/sleep/wake behavior, and
   measured runtime scenarios.
10. **Maintenance:** recovery path, signed or integrity-checked OTA, diagnostic
    export, and factory reset.

### Post-fulfillment release

- Morning and evening reflection cards
- Personalized home-face builder
- Encrypted backup/export
- Home Assistant and richer CalDAV integration
- More capable semantic recall
- Face SDK and reviewed community gallery

### Explicitly deferred

- Wake word or always-listening mode
- Camera-based inference
- Cellular connectivity
- General application store
- Medical or diagnostic features
- Autonomous external actions without confirmation
- Multiple display sizes in the first fulfillment run
- New hardware stretch goals

## 3. Architecture decisions required before feature work

The former sketch implementation has been removed. `astrolabe175c` is the
canonical round-pocketwatch ESP-IDF source tree. Before opening the remaining
product epics, record these decisions in an architecture note:

1. **Canonical fulfillment SKU:** select the exact board/display/audio/PMU
   configuration within the `astrolabe175c` hardware family.
2. **Product variant:** select the one `astrolabe175c` firmware variant that represents the
   Kickstarter device. Other variants remain development or post-launch
   targets.
3. **Storage budget:** allocate durable space for offline audio/capture records,
   retry metadata, face assets, logs, and OTA slots.
4. **Commonplace contract:** define IDs, timestamps, local state, idempotency
   key, authentication, upload state, deletion, and server acknowledgment.
5. **Reminder ownership:** decide which reminders execute fully on-device and
   which may synchronize through Castalia.
6. **Security baseline:** confirm TLS trust, credential storage, signed update
   policy, log redaction, and factory-reset behavior.
7. **Cloud cost boundary:** specify included transcription/TTS behavior and the
   experience when an allowance or service is unavailable.

No new face should begin until decisions 1–4 are settled. Reliability work may
continue in parallel.

## 4. Delivery model

Use focused GitHub issues and branches from `integration`:

- `feature/<issue>-slug` for a bounded capability;
- `fix/<issue>-slug` for defects;
- one focused commit per completed backlog item;
- PR base is always `integration`;
- promotion to `main` requires the documented build, flash, and hardware gate.

Keep no more than three issues in progress. Each epic below should become a
GitHub milestone; each numbered deliverable should become an issue or a small
cluster of issues.

### Definition of done for firmware issues

- Acceptance criteria pass on the canonical physical device.
- Host/unit tests cover deterministic logic and failure transitions.
- Logs are useful and do not expose credentials, captured speech, or private
  content by default.
- UI changes have a physical-device screenshot and visual review.
- Power, heap, task-stack, and storage impact are recorded when relevant.
- Offline, unauthenticated, low-battery, and server-failure states are tested.
- Documentation and `BACKLOG.md` are updated in the same PR.
- CI passes and the feature has a rollback or recovery path where applicable.

## 5. Roadmap

The schedule is expressed as sixteen engineering weeks. Calendar dates should
be assigned only after capacity and the canonical SKU are confirmed. Hardware
lead time and certification run alongside firmware but may extend the campaign
date.

### Phase 0 — Product freeze and baseline (Weeks 1–2)

**Goal:** establish one measurable product baseline before adding features.

Deliverables:

1. Publish the canonical SKU/source-tree architecture decision.
2. Freeze the campaign feature matrix and offline/online behavior matrix.
3. Inventory flash, PSRAM, heap, tasks, partitions, and binary size.
4. Establish cold boot, warm wake, face-change, capture latency, and battery
   measurement scripts.
5. Create a repeatable six-minute product smoke test.
6. Replace remaining insecure TLS behavior on the canonical target or document
   the concrete migration issue blocking release.
7. Define privacy-safe diagnostic logging and an export format.

Acceptance criteria:

- A clean checkout from `integration` builds reproducibly.
- One command builds the campaign image; one documented procedure flashes it.
- The device completes 20 consecutive smoke-test loops without reboot, heap
  exhaustion, corrupted UI, or lost input.
- Baseline measurements are stored as versioned reports, not informal notes.
- The team can point to a single table explaining every network dependency.

### Phase 1 — Durable Commonplace capture (Weeks 3–5)

**Goal:** make capture reliable enough to be the flagship interaction.

Deliverables:

1. Define a versioned local capture record:
   `capture_id`, created time, audio/text reference, mode, retry count, state,
   server ID, and deletion state.
2. Implement a power-loss-safe queue with bounded storage and explicit eviction
   rules.
3. Record locally before beginning cloud processing.
4. Add idempotent upload/retry so reconnects never create duplicate entries.
5. Add visible and haptic states: listening, processing, saved, queued, full,
   authentication needed, and failed.
6. Add recent-capture browsing and local deletion.
7. Add queue inspection and recovery commands to the diagnostic console.

Acceptance criteria:

- A capture made without Wi-Fi remains present after reboot.
- Reconnection uploads each queued capture exactly once.
- Power interruption during record, finalize, upload, and acknowledgment cannot
  corrupt the queue.
- Storage-full behavior preserves existing records and clearly informs the
  user.
- Fifty sequential captures and a 100-cycle disconnect/reconnect test complete
  without duplicates, leaks, or crashes.
- Private content is absent from normal serial and crash logs.

### Phase 2 — Recall and reminders (Weeks 6–7)

**Goal:** close the loop so saved thoughts return at useful moments.

Deliverables:

1. Recent-item list on the Commonplace face.
2. Simple local text/metadata index after transcription is available.
3. Voice commands for last item, recent items, and keyword retrieval.
4. Time-based reminder model stored locally with stable IDs.
5. Parser/service contract for phrases such as “tonight,” “tomorrow at nine,”
   and explicit dates, with confirmation before saving ambiguous times.
6. Reminder presentation, snooze, complete, and quiet-hours behavior.
7. Optional server synchronization designed so the on-device reminder remains
   authoritative while offline.

Acceptance criteria:

- Recent and keyword recall function without contacting an LLM after local
  transcription/indexing is present.
- Reminders survive reboot, time synchronization, timezone changes, and offline
  operation.
- Duplicate synchronization cannot produce duplicate alerts.
- Ambiguous dates are confirmed rather than guessed.
- Quiet/rest blocks suppress nonessential alerts and record the reason.

### Phase 3 — Today and rhythms (Weeks 8–9)

**Goal:** create the calm daily glance that makes Astrolabe useful without voice.

Deliverables:

1. Resolve local timezone and observer-location configuration.
2. Cache the next schedule item and a bounded agenda locally.
3. Build the Today data model: local time, next event, selected intention,
   pending reminder, and optional rhythm card.
4. Complete the on-device Castalian Rhythms V0 path already described in
   [`castalian-rhythms.md`](castalian-rhythms.md), or explicitly reduce it to the
   subset proven deliverable for the campaign.
5. Implement Today and Rhythms faces with stale-data and precision indicators.
6. Ensure cloud prose expansion is optional and never blocks the local card.

Acceptance criteria:

- The Today face renders useful content within one second of wake from cached
  state.
- Schedule failure does not remove local time, intention, or reminders.
- Stale, approximate, and unavailable data are visibly distinguishable.
- Birth, cycle, and personal rhythm settings remain local unless the user
  explicitly invokes an online feature.
- Both faces pass screenshot, gesture, wake, offline, and low-power hardware QA.

### Phase 4 — Privacy, controls, and onboarding (Weeks 10–11)

**Goal:** make device behavior understandable without reading a policy.

Deliverables:

1. Privacy/status face showing microphone state, network state, auth mode, last
   cloud contact, pending queue count, and local storage use.
2. Persistent microphone lockout with a conspicuous on-device indicator. A
   hardware cutoff is preferred if the enclosure/electrical design can support
   it; otherwise implement and describe the software boundary honestly.
3. Local delete-one, delete-all, sign-out, credential erase, and factory reset.
4. Guided Wi-Fi and account provisioning through the existing settings flow.
5. Plain-language consent at the moment an online voice feature is first used.
6. Data-flow and offline/online documentation suitable for the campaign page.

Acceptance criteria:

- The user can determine whether the microphone and cloud path are available at
  a glance.
- Microphone lockout prevents recording in every face and persists across
  reboot.
- Factory reset removes credentials, captures, reminders, profiles, queue
  metadata, and user configuration while preserving recovery firmware.
- Failed or partial reset is detected and safely resumed.
- Network traces match the published data-flow matrix.

### Phase 5 — Six-face release and interaction polish (Weeks 12–13)

**Goal:** ship a small, coherent face set instead of a large demonstration menu.

Campaign face set:

1. **Time** — default, fast, readable, next-event option.
2. **Commonplace** — capture state, recent items, queue state.
3. **Today** — event, intention, reminder, daily card.
4. **Rhythms** — local-first moon/seasonal/personal rhythm.
5. **Alethiometer** — bounded symbolic question and interpretation ritual.
6. **Music** — reliable current playback and deliberate control; advanced Vinyl
   Queue behavior may remain post-launch if its backend is not production-ready.

Deliverables:

- Shared layout, state, typography, accessibility, stale-data, and error
  conventions.
- Deterministic swipe/tap/hold ownership with no face-specific conflicts.
- High-contrast and enlarged-text option.
- Face enable/disable and ordering in settings.
- Haptic vocabulary documented and consistent.
- Remove or hide experimental faces from the fulfillment build without deleting
  their development code.

Acceptance criteria:

- A new user can identify each face’s purpose and primary action without a
  manual.
- A full face tour completes 50 times without accidental capture, unintended
  playback, crash, or stuck gesture state.
- Every face has explicit offline, loading, empty, stale, auth, and error states.
- Screens pass physical visual QA at minimum, normal, and maximum brightness.
- No campaign demonstration depends on a stub or simulated backend.

### Phase 6 — Power, update, and endurance (Weeks 14–15)

**Goal:** turn a working prototype into supportable consumer firmware.

Deliverables:

1. Finish dim, sleep, wake, and network-on-demand behavior for battery use.
2. Calibrate battery percentage and document measured scenarios:
   clock-only, typical daily captures, heavy voice, and charging-stand mode.
3. Complete recovery-safe runtime OTA before face-pack OTA expansion.
4. Enforce image integrity, channel compatibility, space/heap preflight, and
   rollback.
5. Add watchdog, crash reason, boot-loop detection, and recovery entry.
6. Run long-duration voice, face-tour, reconnect, storage, and update testing.

Acceptance criteria:

- Wake always presents time before starting nonessential networking.
- Twenty low-battery sleep/wake cycles preserve time, captures, reminders, and
  settings.
- Interrupted OTA returns to the previous image or factory recovery.
- A seven-day mixed-use soak has no unexplained reboot, queue corruption,
  progressive heap loss, or stuck audio/network task.
- Published battery claims use the same procedures and firmware as the campaign
  units.

### Phase 7 — Release candidate and pilot (Week 16 plus pilot time)

**Goal:** prove the complete product with people who did not build it.

Deliverables:

1. Provision 15–25 representative pilot devices.
2. Run a minimum three-week field pilot.
3. Collect structured failure, comprehension, battery, retention, and support
   data.
4. Freeze the campaign release candidate; accept only release-blocking fixes
   afterward.
5. Produce the manufacturing flash image, provisioning procedure, per-unit test,
   serial/device identity record, and packaging reset procedure.
6. Record an unedited hero demonstration using a release-candidate unit.

Acceptance criteria:

- At least 90% of pilot users complete capture and recall without assistance.
- At least 95% of attempted captures end in a truthful saved, queued, or failed
  state; silent loss is zero.
- No unresolved severity-one privacy, data-loss, recovery, charging, thermal, or
  update defect remains.
- Support can diagnose common failures from user-visible status and a redacted
  diagnostic export.
- The product shown in campaign material matches the release configuration.

## 6. Cross-functional hardware and production track

This track begins in Phase 0 and must not wait for firmware completion.

### Industrial and electrical

- Freeze board, display, microphone, speaker, PMU, battery, buttons, antenna,
  charging connector, and attachment method.
- Decide whether a physical microphone cutoff is feasible.
- Produce enclosure revisions for thermal behavior, sound, antenna performance,
  button reliability, serviceability, and drop protection.
- Minimize cosmetic and electrical variants.

### Compliance and safety

- Identify required radio, battery, transport, EMC, materials, labeling, and
  regional compliance work with qualified specialists.
- Obtain battery documentation and shipping test evidence.
- Complete charging, thermal, short-circuit, cable, and abnormal-use testing.
- Treat cycle/rhythm features as wellness/calendar tools, not medical devices.

### Manufacturing readiness

- BOM and approved-vendor list with alternates.
- Design-for-manufacture and assembly review.
- Golden unit and per-unit automated functional test.
- Firmware provisioning, device identity, key handling, and audit trail.
- Incoming inspection, final inspection, failure quarantine, and rework plan.
- Packaging, freight, fulfillment, spares, returns, and warranty assumptions.

Manufacturing release requires a successful pilot build before a volume order.

## 7. Test matrix

Every release candidate is tested across these dimensions:

| Area | Required cases |
|---|---|
| Power | USB, battery, low battery, charge transition, sleep, wake, abrupt loss |
| Network | Valid Wi-Fi, absent Wi-Fi, captive/broken network, reconnect, slow server |
| Account | Anonymous, signed in, expired token, revoked token, reset |
| Capture | Short, long, silence, interruption, queue full, reboot at every state |
| Recall | Empty, recent, keyword miss/hit, deleted item, offline |
| Reminder | Reboot, timezone change, NTP correction, quiet hours, duplicate sync |
| Audio | Record/play overlap, codec failure, repeated sessions, low heap |
| UI | Every face and state, gesture conflicts, brightness, large text |
| Storage | Full, corrupt record, partial write, migration, factory reset |
| OTA | Valid, wrong channel, corrupt, interrupted, insufficient space, rollback |
| Privacy | Mic lockout, log inspection, network capture, deletion, credential erase |

Automate deterministic portions. Preserve physical-device evidence for power,
audio, touch, display, charging, antenna, and thermal behavior.

## 8. Release gates

### Alpha gate

- Durable offline capture passes fault injection.
- The canonical device survives the smoke test.
- Security and data contracts are frozen enough for migration support.

### Beta gate

- Capture, recall, reminders, Today, privacy, and six faces are integrated.
- Recovery OTA and factory reset work.
- No known silent data-loss path remains.
- Battery scenarios are measurable and repeatable.

### Campaign gate

- Release candidate passes the seven-day soak and device gate.
- Pilot acceptance criteria pass.
- Manufacturing quotes and unit economics are current.
- Campaign demonstrations use production-representative hardware and software.
- Offline/cloud behavior, limitations, service costs, and delivery scope are
  published plainly.

### Production gate

- Pilot manufacturing run passes yield and per-unit test targets.
- Required compliance and battery/shipping work is complete.
- Golden image, provisioning, rollback, support, spares, and RMA paths exist.
- Production firmware is promoted from `integration` to `main` only after the
  documented flash QA gate.

## 9. Risk register

| Risk | Early warning | Mitigation |
|---|---|---|
| Feature sprawl | New faces enter the campaign scope | Freeze six faces; additions are post-launch |
| Silent capture loss | UI says saved before durable commit | Local commit first; explicit state machine and fault tests |
| Cloud cost surprise | Unlimited or vague service promise | Meter real usage; publish allowance and degraded behavior |
| Battery disappointment | Claims rely on idle lab conditions | Publish named scenarios from release firmware |
| Backend instability | Face blocks waiting for service | Cache, timeout, truthful stale/offline states |
| Storage exhaustion | Audio queue grows without bound | Capacity UI, bounded queue, export/delete, safe eviction policy |
| Security delay | TLS/update signing deferred repeatedly | Make security items campaign blockers |
| Variant fragmentation | Multiple SKUs require separate QA | One canonical fulfillment SKU |
| Manufacturing delay | Enclosure/compliance starts after firmware | Run hardware track from Phase 0 |
| Support overload | Failures require serial console | Status face and redacted diagnostic export |

## 10. Suggested GitHub milestone and issue structure

Create these milestones in order:

1. **Campaign 0 — Product baseline**
2. **Campaign 1 — Durable capture**
3. **Campaign 2 — Recall and reminders**
4. **Campaign 3 — Today and rhythms**
5. **Campaign 4 — Privacy and onboarding**
6. **Campaign 5 — Six-face release**
7. **Campaign 6 — Power, OTA, and endurance**
8. **Campaign 7 — Pilot and release candidate**

Each issue should contain:

- user outcome;
- in-scope and explicitly out-of-scope behavior;
- state/data contract;
- acceptance criteria copied or refined from this plan;
- automated and physical test requirements;
- security/privacy considerations;
- dependencies and rollback behavior;
- documentation and hardware-QA checklist.

Do not create all implementation issues before Phase 0 decisions are complete.
Create the milestone skeleton, then detail only the next one or two milestones so
the plan can respond to measured constraints without losing its product scope.

## 11. Immediate next actions

1. Approve or revise the campaign-critical scope in Section 2.
2. Write the canonical SKU/source-tree architecture decision.
3. Create the eight GitHub milestones with `integration` as the working base.
4. Open Phase 0 issues for reproducible build, resource baseline, behavior
   matrix, smoke test, TLS/security baseline, and diagnostic logging.
5. Define the durable Commonplace record and idempotency contract before writing
   the queue implementation.
6. Assign an owner to the parallel enclosure/manufacturing track.
7. Begin capturing baseline latency, stability, battery, and storage evidence
   from the current device.
