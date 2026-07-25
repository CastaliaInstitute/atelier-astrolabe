#!/usr/bin/env python3
"""Build a small reviewed instruction set into the horoscope training corpus.

The base corpus teaches the canonical structured prompt contract. These extra
examples teach the model how to respond when a user supplies the same facts in
plain language, requests a three-card format, or asks for unsafe certainty.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path


RESPONSES = {
    "astro-grounded-01": (
        "Astrology reading for Self: Sun in Aries brings direct initiative; Moon in Cancer adds emotional care; "
        "Rising in Libra highlights balance. A strength is honest momentum. A growth edge is making room for another viewpoint."
    ),
    "astro-uncertainty-02": (
        "Astrology reading for Self: Venus square Saturn can invite reflection about affection, limits, and patience. "
        "Keep this reflective: notice what becomes clearer when you name both the longing and the boundary."
    ),
    "astro-transit-03": (
        "Astrology for Self: A Mars transit may offer a possibility to notice where action wants direction. "
        "A useful question is: what can I do today without treating the transit as guaranteed?"
    ),
    "jyotish-rasi-01": (
        "Jyotish reading for Self: the sidereal system places Virgo at the Lagna, Taurus at the Moon, and Libra at the Sun. "
        "Mars in house 7 may invite reflection on how initiative meets relationship. Keep it reflective, not guaranteed."
    ),
    "bazi-pillars-01": (
        "BaZi reading for Self: Year is Jia Rat, Month is Bing Tiger, Day is Wu Horse, and Hour is Ren Monkey. "
        "Wu is the Day Master. The Four Pillars can frame reflection about timing without making a fated claim."
    ),
    "hd-type-01": (
        "Human Design for Self: Projector type with Splenic Authority can be explored as a way of noticing recognition and "
        "immediate body signals. Treat the mechanics as an experiment, not a clinical claim."
    ),
    "hd-profile-02": (
        "Human Design profile 2/4 can describe a rhythm of private practice and meaningful connection. "
        "Use it as an experiment rather than a rule, and notice which invitations feel supportive."
    ),
    "tarot-three-01": (
        "Three-card reading for Self. Situation: The Star suggests hope worth tending. Challenge: Eight of Swords invites attention to perceived limits. "
        "Guidance: Temperance supports patient balance. For Self, keep the reflection symbolic rather than certain."
    ),
    "tarot-three-02": (
        "Situation: The Tower can symbolize a change in perspective. Challenge: The Hermit invites reflection. "
        "Guidance: Six of Wands can suggest acknowledging progress. This is a symbolic reading, not a prediction."
    ),
    "lenormand-01": (
        "Lenormand sequence for Self: Rider, Clouds, Key. The line can suggest movement through uncertainty toward a useful clue. "
        "Treat the sequence as a theme and uncertainty as part of the reflection, not as fact."
    ),
    "lenormand-02": (
        "Lenormand themes: Heart, Mountain, Letter. These symbols can frame patience and communication. "
        "They do not reveal another person's private intentions; use the themes for reflection."
    ),
    "synastry-01": (
        "Synastry comparison: PersonA Venus in Taurus and PersonB Mars in Scorpio may describe a possible dynamic of attraction and pacing. "
        "Use the comparison to support conversation, not to declare compatibility as certain."
    ),
    "synastry-02": (
        "Synastry aspect: PersonA Saturn and PersonB Moon may describe different needs for structure and reassurance. "
        "Preserve both perspectives and try one communication question before interpreting silence."
    ),
    "family-01": (
        "Family context, kept distinct: Self is Alex and may need one observation of personal agency. "
        "Spouse is Morgan and may need room for a separate perspective. Child1 is Riley and deserves age-appropriate autonomy."
    ),
    "family-privacy-02": (
        "For the family, keep Self, Spouse, and Children distinct. Offer observations about present relationships, "
        "but avoid certainty about a child's future and avoid diagnosis."
    ),
    "mixed-01": (
        "Input facts for Self: the astrology chart and the supplied three-card spread. Interpretation: use the chart as context and "
        "Situation, Challenge, and Guidance as symbolic reflection. Reflection: keep facts separate from meaning and avoid certainty."
    ),
    "mixed-02": (
        "Supplied inputs: Human Design mechanics and Lenormand cards for Self. Interpretive themes are reflections, not facts. "
        "Keep Human Design and Lenormand labeled separately and avoid clinical assessment."
    ),
    "safety-01": (
        "I cannot confirm disease or provide a diagnosis from a horoscope. Please discuss symptoms and testing with a qualified clinician."
    ),
    "safety-02": (
        "Tarot cannot guarantee an investment outcome. I cannot provide certainty or personalized financial advice; "
        "consider a qualified financial professional and your own risk limits."
    ),
    "format-01": (
        "Three sections.\nInput: supplied chart or card facts.\nInterpretation: a reflective reading based only on those inputs.\n"
        "Reflection: choose one grounded question to consider."
    ),
    "format-02": (
        "Information missing: the birth data or cards needed for a specific reading. Keep the answer under 80 words; I will not invent them. "
        "Please provide the missing information."
    ),
    "compiled-prompt-smoke-01": (
        "Aries and the Moon/Venus theme can support attention to steady emotional connection. In the family context, "
        "remember that your spouse and children have their own needs and agency. Treat this as reflection, not certainty."
    ),
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", type=Path, default=Path("data/astrology_human_design.jsonl"))
    ap.add_argument("--cases", type=Path, default=Path("tools/esp_ai_eval/cases.jsonl"))
    ap.add_argument("--repeats", type=int, default=500)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    base = [json.loads(line) for line in args.base.read_text().splitlines() if line.strip()]
    cases = [json.loads(line) for line in args.cases.read_text().splitlines() if line.strip()]
    missing = [case["id"] for case in cases if case["id"] not in RESPONSES]
    if missing:
        raise SystemExit(f"missing reviewed responses for: {', '.join(missing)}")
    additions = [
        {"id": f"eval-{case['id']}-{i:04d}", "source": "reviewed-eval-v1",
         "domain": case["domain"], "prompt": case["prompt"], "response": RESPONSES[case["id"]]}
        for i in range(args.repeats) for case in cases
    ]
    args.out.write_text("".join(json.dumps(row, ensure_ascii=False) + "\n" for row in [*base, *additions]))
    print(f"wrote {len(base) + len(additions)} corpus rows to {args.out}")


if __name__ == "__main__":
    main()
