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
| Actionability | 20 | A concrete choice, observation, question, or care practice |
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

`voice-pipeline` returns a compact `quality` summary with the benchmark version,
gate results, packet average, minimum face score, maximum cross-face
similarity, and weak faces. This makes quality observable without sending the
full diagnostic report to the device.
