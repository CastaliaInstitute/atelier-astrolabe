# LunaSay reading-quality benchmark

LunaSay should not call itself better than a polished astrology app merely
because its calculations are correct or its prose sounds gentle. A release
candidate must pass two independent gates:

1. **Hard evidence and safety gate** — every generated face retains verbatim
   supporting evidence; timed faces retain server-owned timing; spoken output
   fits the device; Family Synastry is a complete Pattern/Today/Next/Practice
   narrative; no deterministic or stock-horoscope language passes.
2. **User-value gate** — each face is specific, actionable, epistemically
   humble, distinct from its neighbors, natural when spoken, and nuanced in a
   way appropriate to that face.

The deterministic benchmark lives in
`supabase/functions/_shared/lunasayReadingQuality.ts`. It scores the six
generated daily faces on a 100-point scale:

| Dimension            | Points | What it rewards                                                                                |
| -------------------- | -----: | ---------------------------------------------------------------------------------------------- |
| Evidence specificity |     20 | Verbatim evidence that is visibly carried into a non-redundant reading                         |
| Actionability        |     20 | A validated structured action that begins with a direct verb and is present in spoken delivery |
| Epistemic humility   |     15 | Conditional/reflection language without certainty or horoscope filler                          |
| Face distinctness    |     15 | Correct face vocabulary and low repetition across the packet                                   |
| Spoken delivery      |     15 | Complete, bounded, natural TTS sentences                                                       |
| Face-specific nuance |     15 | Resource/tension balance, reciprocal synastry, reflective tarot, or observable sky             |

The release target is:

- all hard checks pass;
- packet average at least 72;
- no generated face below 60;
- maximum pairwise face similarity no more than 0.42.

These thresholds are intentionally a floor, not a claim of parity with any
competitor. Comparative claims require dated, lawful reference samples and human
blind review. The benchmark prevents internal regressions and identifies which
face needs prompt or validator work next.

Run it with:

```sh
deno run --allow-read scripts/lunasay_reading_quality.ts packet.json facts.txt
```

The report is JSON so CI, campaign evidence, and later human-review tooling can
consume the same artifact.

Production uses one Gemini 2.5 call for each generated face. Every candidate is
validated against only that face's server-selected evidence. A hard provenance
or safety failure receives one corrective retry; user-value scores remain
telemetry and do not discard a safe reading. If both attempts fail, the
deterministic fallback retains the exact evidence, timing, and card identity so
the replacement is still auditable.

Benchmark version 2 makes actionability contractual rather than heuristic. Moon,
Inner Weather, Transits, Tarot, and Sky must return an `action` beginning with a
direct imperative verb. The generation schema targets 96 characters, with a hard
120-character device boundary. Family Synastry's existing `practice` is its
action. The server preserves the action as structured data, bounds any verbose
interpretive prelude, and appends the exact action to `spoken` when needed, so
every cached face delivers the same concrete practice that was validated.

Benchmark version 3 makes Family Synastry reciprocity structural rather than
stylistic. A generated relationship reading now carries two separate,
conditional perspectives. Each begins with the corresponding person’s
device-supplied name, the perspectives must differ, and both are composed into
the spoken Pattern beat. A response can no longer pass by saying only “both
people” or by using reciprocal-sounding vocabulary around a one-sided
interpretation. The hard gate rejects a missing side, duplicated side, fixed
trait, wrong person, or unnamed participant before the reading reaches the
device.

Benchmark version 4 closes the remaining “technically reciprocal” loopholes. The
two named perspectives must differ in substance, not merely by replacing one
name with another. Each is short enough for both sides to survive the device
speech boundary. Causal blame language fails the hard gate even when it is
softened with “may” or “can.” For a parent-child relationship, the practice also
fails if it directs the child to calm, comfort, reassure, or regulate the adult.

`voice-pipeline` returns a compact `quality` summary with the benchmark version,
gate results, packet average, minimum face score, maximum cross-face similarity,
and weak faces. This makes quality observable without sending the full
diagnostic report to the device.

Before release, run repeated signed production samples rather than relying on a
single favorable generation:

```sh
SUPABASE_URL=... \
SUPABASE_ANON_KEY=... \
ASTROLABE_DEVICE_MAC=... \
ASTROLABE_DEVICE_CHANNEL=... \
ASTROLABE_DEVICE_SECRET=... \
deno run --allow-env --allow-net --allow-read \
  scripts/lunasay_quality_soak.ts facts.txt 6
```

The soak fails unless every sample passes the hard gate, release gate, and
structured-action contract with no face fallback. Its JSON report also records
average model calls, mean and p10 packet scores, and the lowest observed face
score.

To verify private longitudinal continuity, pass a third JSON fixture containing
either the legacy prior-day envelope or a `history` array with up to seven
recent dates and each face's prior server-generated `headline`, `action`, and
SHA-256 `evidenceHash`:

```sh
deno run --allow-env --allow-net --allow-read \
  scripts/lunasay_quality_soak.ts facts.txt 6 prior-reading.json
```

In this mode the soak also fails unless the service applies continuity to every
supplied face and every new face avoids every retained exact action.

On 2026-07-24, a six-sample signed production soak using six independent Gemini
2.5 Flash face calls passed every hard, release, and action-contract gate with
zero fallbacks. The packet average was 98.9, p10 was 97.8, the lowest observed
face score was 92, and face-local safety retries brought the average to 6.5
model calls per packet.

A second six-sample signed production soak supplied prior summaries for all six
faces. All 36 face readings applied continuity, avoided the prior exact action,
and passed the hard, release, and action gates with zero fallbacks. The packet
average was 98.7, p10 was 97.7, the lowest observed face score was 87, and the
average request used 6.17 model calls including one successful quality retry.
