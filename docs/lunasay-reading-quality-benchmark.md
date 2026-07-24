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

| Dimension | Points | What it rewards |
|---|---:|---|
| Evidence specificity | 20 | Verbatim evidence that is visibly carried into a non-redundant reading |
| Actionability | 20 | A validated structured action that begins with a direct verb and is present in spoken delivery |
| Epistemic humility | 15 | Conditional/reflection language without certainty or horoscope filler |
| Face distinctness | 15 | Correct face vocabulary and low repetition across the packet |
| Spoken delivery | 15 | Complete, bounded, natural TTS sentences |
| Face-specific nuance | 15 | Resource/tension balance, reciprocal synastry, reflective tarot, or observable sky |

The release target is:

- all hard checks pass;
- packet average at least 72;
- no generated face below 60;
- maximum pairwise face similarity no more than 0.42.

These thresholds are intentionally a floor, not a claim of parity with any
competitor. Comparative claims require dated, lawful reference samples and
human blind review. The benchmark prevents internal regressions and identifies
which face needs prompt or validator work next.

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

Benchmark version 2 makes actionability contractual rather than heuristic.
Moon, Inner Weather, Transits, Tarot, and Sky must return an `action` beginning
with a direct imperative verb. The generation schema targets 96 characters,
with a hard 120-character device boundary. Family Synastry's existing
`practice` is its action. The server preserves the action as structured data,
bounds any verbose interpretive prelude, and appends the exact action to
`spoken` when needed, so every cached face delivers the same concrete practice
that was validated.

`voice-pipeline` returns a compact `quality` summary with the benchmark version,
gate results, packet average, minimum face score, maximum cross-face
similarity, and weak faces. This makes quality observable without sending the
full diagnostic report to the device.

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

On 2026-07-24, a six-sample signed production soak using six independent
Gemini 2.5 Flash face calls passed every hard, release, and action-contract
gate with zero fallbacks. The packet average was 98.9, p10 was 97.8, the
lowest observed face score was 92, and face-local safety retries brought the
average to 6.5 model calls per packet.
