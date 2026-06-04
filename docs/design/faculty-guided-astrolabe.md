# Faculty-Guided Astrolabe

## Summary

This brief translates the faculty recommendations in [Inquirer Volume IV: Responsum Castaliense](https://inquirer.castalia.institute/Inquirer/4/inquirer-volume-4.pdf) into product constraints for Astrolabe.

The core change is not to make the watch more conversational or more automated. The faculty direction is to make Astrolabe a humane instrument: a small, bounded object that protects attention, discloses provenance, preserves human judgment, and knows when to stop.

## Governing Commitments

Astrolabe should optimize for these durable behaviors:

- **Orientation over engagement:** A user should always know the current face, mode, data destination, and whether AI, local firmware, cloud services, or a human steward is involved.
- **Silence as success:** Non-use, protected attention, and retreat blocks are successful states, not failures to retain a user.
- **Human-first artifacts:** Before AI synthesis, the user should be invited to make a small human artifact: an observation, question, source card, sketch, voice note, or rough draft.
- **Provenance over fluency:** Generated speech, summaries, readings, faculty replies, and recommendations should carry enough source, prompt, model, and human-decision metadata to be questioned later.
- **Assistance without authority theater:** Astrolabe may rehearse, retrieve, remind, compare, and challenge. It must not simulate faculty certification, spiritual authority, clinical judgment, confession, blessing, worship, or irreversible human responsibility.
- **Handoff over enclosure:** The best interaction often ends with a printed/exported plan, a saved commonplace note, a human conversation, a class, a meal, prayer, sleep, or silence outside the device.

## Recommended Product Contracts

### Stable Threshold

Every significant mode change should be visible and logged in a compact way:

| Transition | Required user-visible state |
|------------|-----------------------------|
| Local face to cloud request | endpoint/purpose and auth mode |
| Device-only data to synced data | destination and retention expectation |
| Local generation to LLM/TTS | model route and whether sources are used |
| AI rehearsal to human review | named handoff, not machine certification |
| Private note to shared/commonplace record | audience and editability |

Firmware and Edge Functions should treat mode, route, data destination, retention policy, and authority level as first-class fields. UI copy alone is not enough.

### Provenance Path

Astrolabe-generated artifacts should preserve compact provenance:

- face and route, such as `faculty`, `question_day`, `astrology`, `notes`, or `calcifer`;
- input type, such as voice, typed text, sensor fact, calendar fact, ephemeris fact, or source passage;
- prompt/template version when applicable;
- model/provider route when applicable;
- cited source IDs or local KB version when applicable;
- human action taken: accepted, edited, saved, replayed, dismissed, escalated, or exported.

On-device storage can keep this sparse. The point is not surveillance; it is answerability.

### Recollection Blocks

Astrolabe should support protected time blocks that deliberately reduce interaction:

| Block | Watch behavior |
|-------|----------------|
| Recollection | no unsolicited prompts; direct safety/accessibility queries allowed |
| Reading | show timer/source card; suppress cloud synthesis until a note/question exists |
| Studio/fieldwork | allow capture; defer analysis and summaries |
| Sabbath/rest | batch non-critical notifications; no streaks, recaps, or engagement rewards |
| Seminar/class | allow agenda and source access; suppress companion chatter |

Bypasses should require a minimal declared reason, visible expiration, and later review only where an institutional steward is responsible. Personal/private use should remain invitational rather than coercive.

### Temporal Rhythm

Astrolabe already has time, circadian hue, calendar, astrology, and moon surfaces. These should become temporal affordances rather than engagement loops:

- **Celebration:** elevate community, family, academic, civic, or ecclesial dates without gamifying participation.
- **Restraint:** make voluntary scarcity easier, such as fewer prompts, fewer faces, fewer cloud calls, or fewer summaries.
- **Rest boundary:** suspend non-critical notifications and analytics; treat the strongest mode as screen-off or device-silent.
- **Seasonal memory:** let human stewards map calendars into visible rhythms without the model inventing obligations.
- **Vigilance:** gather materials for preparation, but do not infer hidden anxiety, devotion, holiness, or spiritual state.

Do not build digital substitutes for non-remediable acts: confession, absolution, Eucharistic participation, blessing, pastoral crisis counsel, clinical diagnosis, or human certification of mastery.

### Human-First AI

For educational, literary, theological, or faculty-mediated flows, AI should unlock only after a prior user artifact exists.

Examples:

- `Notes`: record a voice note or source card before asking for synthesis.
- `Faculty`: ask the user to name the question and the desired faculty or discipline before routing.
- `Question of the Day`: preserve the user's first answer before offering faculty commentary.
- `Astrology/Rhythms`: show deterministic local facts and confidence before any LLM prose.
- `Poetry/literary exercises`: require first attention notes and a draft before machine critique.

The machine may then serve as critic, adversary, retrieval assistant, or rehearsal partner. It should not supply the governing image, final judgment, or institutional certification.

### Faculty Triage, Not Faculty Replacement

Faculty workflows should use AI to reduce clerical load and improve human attention:

- summarize unresolved knots for a mentor;
- prepare oral-defense rehearsal questions;
- flag missing provenance or missing source contact;
- find repeated evasions or sudden stylistic discontinuities as prompts for human review;
- generate counterexamples and objections, labeled as prompts.

Astrolabe should not assign virtue scores, certify wisdom, simulate a named professor's authority, or close a serious dispute. Difficult cases should be labeled as requiring human judgment.

### Commons Governance

Shared datasets, faculty bust assets, embedded interpretation KBs, face packs, prompts, and model routes should be governed as common infrastructure:

- maintain public or repo-local charters for shared AI resources;
- keep source provenance and license records with generated assets;
- support portability and forking where possible;
- define who can approve prompt, model, dataset, and calendar-rule changes;
- provide appeal/repair paths when a generated result harms, misleads, or misattributes.

The governed object is the technical system, not the user's soul, attention, or dignity as a metric.

## Current Surface Implications

| Current surface | Faculty-guided change |
|-----------------|-----------------------|
| Hue home face | Remain calm and non-summoning; no feed behavior. |
| Faculty face | Add mode/provenance indicators, conversation history disclosure, and human-first question capture. |
| Notes/Commonplace | Store first-contact artifact before AI summary; include AI-use provenance when saved. |
| Question of the Day | Preserve the user's first answer before faculty commentary; make faculty choice explainable. |
| Astrology/Rhythms | Prefer local deterministic facts first; disclose precision and avoid fate-like certainty. |
| Calcifer/schedule | Treat agenda as obligation support, not productivity pressure; include rest boundaries. |
| Voice pipeline | Include route, model, prompt version, source/provenance, and handoff flags in responses where feasible. |
| Face packs/OTA | Require a humane-interface checklist before adding faces that prompt, rank, infer, or retain. |

## Implementation Epics

1. **Mode and provenance envelope**
   - Add a shared response envelope for voice/faculty/question/rhythm services.
   - Include route, model/prompt version, source IDs, generated-at, auth mode, and handoff recommendation.
   - Render a tiny mode/provenance cue on relevant faces and expose detail in logs/settings.

2. **Recollection and rest blocks**
   - Add an NVS-backed protected-time scheduler.
   - Suppress unsolicited prompts, notification-like UI, and cloud refreshes during protected blocks.
   - Add a minimal bypass path for safety/accessibility and explicit user intent.

3. **Human-first artifact gate**
   - Extend Notes/Commonplace and Faculty flows to save an initial user artifact before AI synthesis.
   - Forward artifact metadata to Edge Functions.
   - Require the artifact in high-stakes or formation-oriented flows.

4. **Temporal rhythm contracts**
   - Introduce firmware-level flags for celebration, restraint, rest, seasonal memory, and vigilance.
   - Require each face to declare behavior under these contracts.
   - Add a release checklist for faces that break rest, prompt proactively, or use sacred/community calendars.

5. **Commons and asset stewardship**
   - Add charters for embedded KBs, faculty bust sources, prompt templates, and model routes.
   - Track license/provenance for generated or imported assets.
   - Define review ownership for changes to faculty prompts, calendar mappings, and shared datasets.

6. **Magnificat audit for product review**
   - Add a short checklist to design reviews:
     - Who is protected?
     - Whose labor/data/attention makes this possible?
     - What proud power is restrained?
     - What human act remains human?
     - What technical mechanism enforces the answer?

## Non-Goals

- No always-on companion behavior for v1.
- No engagement streaks, vanity metrics, or return hooks.
- No inference of spirituality, holiness, mood, personality, diagnosis, intent, or stable mental state from watch data.
- No automated certification of learning, virtue, pastoral readiness, or faculty judgment.
- No sacred calendar mapping without a human steward for the relevant community.

## Design Review Checklist

Before a feature ships, answer:

- What mode thresholds does it cross?
- What user artifact exists before AI synthesis?
- What provenance is kept, and what private content is intentionally not kept?
- How does the feature behave during recollection/rest?
- What human judgment does it refuse to automate?
- What is the handoff path out of the device?
- What deletion or non-deployment decision did the team consider?

## Source Map

Volume IV sections most directly informing this design:

- Teresa, "The Soul in an Age of Simulation" — recollection, silence, withdrawal, and Astrolabe retreat blocks.
- Newman, "Education as Apprenticeship" — human-first artifacts, tutorial gates, oral defense, and AI as rehearsal rather than judgment.
- Alexander, "Toward Humane Interfaces" — stable thresholds, provenance paths, repair, bounded assistants, and living adaptation.
- Guardini, "The Liturgical Calendar as Interface" — temporal affordances, anti-interface handoff, non-remediable sacred acts, and rest boundaries.
- Ostrom, "The Commons and the Cloud" — governance of datasets, models, annotation systems, platform rules, and shared infrastructure.
- Pestalozzi, "Toward a Castalian Pedagogy" — observation, reconstruction, synthesis, production, public defense, and AI-use ledgers.
- Closing Magnificat rule — product review must name the protected person, served person, restrained power, human responsibility, and enforcing mechanism.
