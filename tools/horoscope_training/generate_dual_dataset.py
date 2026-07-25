#!/usr/bin/env python3
"""Generate a balanced astrology + Human Design instruction corpus.

The examples teach grounded explanation only. Astrology and Human Design
charts must be calculated by deterministic chart code; the model verbalizes
the structured facts supplied in each prompt.
"""

import argparse
import json
import random
from pathlib import Path

from generate_dataset import (
    ACTIONS,
    ASPECTS,
    CLOSERS,
    ELEMENTS,
    OPENERS,
    SIGNS,
)

HD_TYPES = ["Generator", "Manifesting Generator", "Projector", "Manifestor", "Reflector"]
HD_AUTHORITIES = ["Sacral", "Emotional Solar Plexus", "Splenic", "Ego", "Self-Projected"]
HD_PROFILES = ["1/3", "2/4", "3/5", "4/6", "5/1", "6/2"]
HD_CENTERS = [
    "Sacral", "Solar Plexus", "Spleen", "G Center", "Throat",
    "Ajna", "Head", "Root", "Heart",
]
HD_CHANNELS = [
    ("34-20", "bringing responsive energy into the present moment"),
    ("10-34", "living from embodied self-trust"),
    ("37-40", "balancing mutual support with clear agreements"),
    ("57-20", "recognizing what is true in the immediate moment"),
    ("1-8", "expressing a distinct contribution through creative direction"),
]
HD_ACTIONS = [
    "wait for a clear bodily response before committing",
    "give emotional clarity time before making the decision",
    "let recognition arrive instead of forcing visibility",
    "inform the people affected before moving forward",
    "protect space for your own rhythm and observation",
]
TAROT_CARDS = [
    ("The Star", "renewed hope and a direction worth tending"),
    ("The Hermit", "a quieter period of reflection before the next step"),
    ("Strength", "patient courage and gentle self-command"),
    ("Temperance", "a practical balance between two needs"),
    ("The Chariot", "focused movement after choosing a direction"),
]
LENORMAND_PAIRS = [
    ("Rider + Key", "news that opens a useful possibility"),
    ("Tree + Anchor", "steady growth supported by patience"),
    ("Birds + Letter", "a conversation that benefits from clear wording"),
    ("Crossroads + Ship", "a choice that widens the available horizon"),
    ("Heart + Garden", "connection becoming visible through community"),
]
CARD_ACTIONS = [
    "notice what the symbol invites you to practice",
    "treat the image as a prompt for reflection rather than a fixed outcome",
    "choose one small action that makes the message concrete",
]
FAMILY_CONTEXTS = [
    ("Self=Aries Sun; Spouse=Libra Sun; Children=Leo Sun,Pisces Sun", "your spouse and children have their own needs and agency"),
    ("Self=Cancer Moon; Spouse=Capricorn Moon; Children=Gemini Moon", "care and boundaries can coexist across the household"),
    ("Self=Taurus Rising; Spouse=Scorpio Rising; Children=Virgo Rising,Aquarius Rising", "different rhythms can be coordinated without making anyone the problem"),
    ("Self=Generator; Spouse=Projector; Children=Manifesting Generator", "each person may need a different pace and kind of recognition"),
]
SYNASTRY_ASPECTS = [
    ("Sun trine Moon", "natural warmth and emotional recognition"),
    ("Venus conjunct Mars", "strong attraction that benefits from clear consent and pacing"),
    ("Moon square Saturn", "feelings meeting a need for structure and reassurance"),
    ("Mercury opposite Mercury", "different communication styles that improve with translation"),
    ("Jupiter sextile Venus", "generosity and shared enjoyment supporting the bond"),
]
SYNASTRY_ACTIONS = [
    "name the need underneath the reaction",
    "ask a direct question before interpreting silence",
    "make room for both timing and autonomy",
    "turn the shared strength into one concrete agreement",
]

JYOTISH_SIGNS = SIGNS
JYOTISH_PLANETS = ["Sun", "Moon", "Mars", "Mercury", "Jupiter", "Venus", "Saturn", "Rahu", "Ketu"]
BAZI_STEMS = ["Jia", "Yi", "Bing", "Ding", "Wu", "Ji", "Geng", "Xin", "Ren", "Gui"]
BAZI_BRANCHES = ["Rat", "Ox", "Tiger", "Rabbit", "Dragon", "Snake", "Horse", "Goat", "Monkey", "Rooster", "Dog", "Pig"]
BAZI_ELEMENTS = ["Wood", "Wood", "Fire", "Fire", "Earth", "Earth", "Metal", "Metal", "Water", "Water"]


def family_context(index: int) -> tuple[str, str]:
    return FAMILY_CONTEXTS[index % len(FAMILY_CONTEXTS)]


def astrology_row(rng: random.Random, index: int) -> dict:
    sun = SIGNS[index % len(SIGNS)]
    moon = SIGNS[(index * 5 + 3) % len(SIGNS)]
    rising = SIGNS[(index * 7 + 1) % len(SIGNS)]
    aspect, meaning = ASPECTS[index % len(ASPECTS)]
    action = ACTIONS[rng.randrange(len(ACTIONS))]
    family, family_meaning = family_context(index)
    opener = rng.choice(OPENERS).format(sign=sun, element=ELEMENTS[sun], action=action)
    response = f"{opener} {aspect} suggests that {meaning}. In the family context, remember that {family_meaning}. {rng.choice(CLOSERS)}"
    prompt = (
        "Write a concise, warm astrology reading from these chart facts. "
        "Do not predict illness, death, catastrophe, or guaranteed events. "
        f"Domain=Astrology; Sun={sun}; Moon={moon}; Rising={rising}; Aspect={aspect}; Family={family}."
    )
    return {
        "id": f"astrology-{index:06d}",
        "source": "synthetic-dual-v1",
        "domain": "astrology",
        "chart": {"sun": sun, "moon": moon, "rising": rising, "aspects": [aspect]},
        "prompt": prompt,
        "response": response,
    }


def human_design_row(rng: random.Random, index: int) -> dict:
    hd_type = HD_TYPES[index % len(HD_TYPES)]
    authority = HD_AUTHORITIES[(index * 3 + 1) % len(HD_AUTHORITIES)]
    profile = HD_PROFILES[(index * 5 + 2) % len(HD_PROFILES)]
    channel, meaning = HD_CHANNELS[index % len(HD_CHANNELS)]
    centers = [HD_CENTERS[(index + i * 2) % len(HD_CENTERS)] for i in range(3)]
    action = rng.choice(HD_ACTIONS)
    family, family_meaning = family_context(index)
    response = (
        f"As a {hd_type} with {authority} authority and a {profile} profile, "
        f"your design emphasizes {meaning}. In the family context, remember that {family_meaning}. Today, {action}. "
        "Use this as an experiment in awareness rather than a fixed prediction."
    )
    prompt = (
        "Write a concise, warm Human Design reading from these chart facts. "
        "Explain the mechanics without presenting them as medical truth or certainty. "
        f"Domain=Human Design; Type={hd_type}; Authority={authority}; Profile={profile}; "
        f"DefinedCenters={','.join(centers)}; Channel={channel}; Family={family}."
    )
    return {
        "id": f"human-design-{index:06d}",
        "source": "synthetic-dual-v1",
        "domain": "human_design",
        "chart": {
            "type": hd_type,
            "authority": authority,
            "profile": profile,
            "defined_centers": centers,
            "channels": [channel],
        },
        "prompt": prompt,
        "response": response,
    }


def tarot_row(rng: random.Random, index: int) -> dict:
    cards = [TAROT_CARDS[(index + offset) % len(TAROT_CARDS)] for offset in range(3)]
    positions = ("Situation", "Challenge", "Guidance")
    action = rng.choice(CARD_ACTIONS)
    family, family_meaning = family_context(index)
    response = (
        f"In the situation, {cards[0][0]} points toward {cards[0][1]}. "
        f"The challenge is held by {cards[1][0]}, inviting {cards[1][1]}. "
        f"For guidance, {cards[2][0]} offers {cards[2][1]}. "
        f"{action.capitalize()}. In the family context, remember that {family_meaning}. Let the reading remain an invitation, not a guarantee."
    )
    prompt = (
        "Write a concise, warm Tarot reading from these draw facts. "
        "Do not present the cards as certainty, medical advice, or guaranteed prediction. "
        f"Domain=Tarot; Spread=Three Card; Situation={cards[0][0]}; "
        f"Challenge={cards[1][0]}; Guidance={cards[2][0]}; Family={family}."
    )
    return {
        "id": f"tarot-{index:06d}",
        "source": "synthetic-four-domain-v1",
        "domain": "tarot",
        "chart": {"spread": "three_card", "positions": dict(zip(positions, [c[0] for c in cards]))},
        "prompt": prompt,
        "response": response,
    }


def lenormand_row(rng: random.Random, index: int) -> dict:
    pair, meaning = LENORMAND_PAIRS[index % len(LENORMAND_PAIRS)]
    action = rng.choice(CARD_ACTIONS)
    family, family_meaning = family_context(index)
    response = f"The {pair} combination suggests {meaning}. {action.capitalize()}. In the family context, remember that {family_meaning}. Read the pair as context for a choice, not as a fixed fate."
    prompt = (
        "Write a concise, warm Lenormand reading from these card facts. "
        "Use concrete combination language and avoid certainty or guaranteed prediction. "
        f"Domain=Lenormand; Line=2 cards; Cards={pair}; Focus=Guidance; Family={family}."
    )
    return {
        "id": f"lenormand-{index:06d}",
        "source": "synthetic-four-domain-v1",
        "domain": "lenormand",
        "chart": {"line": 2, "cards": pair.split(" + "), "focus": "guidance"},
        "prompt": prompt,
        "response": response,
    }


def synastry_row(rng: random.Random, index: int) -> dict:
    aspect, meaning = SYNASTRY_ASPECTS[index % len(SYNASTRY_ASPECTS)]
    action = rng.choice(SYNASTRY_ACTIONS)
    family, family_meaning = family_context(index)
    response = (
        f"Between the two charts, {aspect} can show {meaning}. "
        f"Try to {action}. In the wider family context, remember that {family_meaning}. "
        "Use the comparison to support conversation, not to label either person."
    )
    prompt = (
        "Write a concise, warm synastry reading from two relationship charts. "
        "Do not predict relationship outcomes or treat either person as fixed. "
        f"Domain=Synastry; PersonA=Self Sun Aries Moon Cancer; PersonB=Spouse Sun Libra Moon Capricorn; "
        f"Aspect={aspect}; Family={family}."
    )
    return {
        "id": f"synastry-{index:06d}",
        "source": "synthetic-family-v1",
        "domain": "synastry",
        "chart": {"person_a": "self", "person_b": "spouse", "aspect": aspect, "family": family},
        "prompt": prompt,
        "response": response,
    }


def jyotish_row(rng: random.Random, index: int) -> dict:
    lagna = JYOTISH_SIGNS[index % len(JYOTISH_SIGNS)]
    moon = JYOTISH_SIGNS[(index * 5 + 2) % len(JYOTISH_SIGNS)]
    sun = JYOTISH_SIGNS[(index * 7 + 4) % len(JYOTISH_SIGNS)]
    planet = JYOTISH_PLANETS[(index * 3) % len(JYOTISH_PLANETS)]
    house = (index * 5) % 12 + 1
    action = rng.choice(ACTIONS)
    response = (
        f"With a sidereal {lagna} lagna, {moon} Moon, and {sun} Sun, the Jyotish chart invites "
        f"attention to how {planet} in house {house} may shape a pattern of effort and response. "
        f"Today, {action}. Treat the rasi as a reflective map, not a fixed prediction."
    )
    prompt = (
        "Write a concise, warm Jyotish reading from supplied sidereal chart facts. "
        "Keep the rasi facts separate from interpretation and avoid certainty, diagnosis, or guaranteed events. "
        f"Domain=Jyotish; System=Sidereal; Lagna={lagna}; Moon={moon}; Sun={sun}; "
        f"Planet={planet}; House={house}; Family={family_context(index)[0]}."
    )
    return {
        "id": f"jyotish-{index:06d}",
        "source": "synthetic-seven-domain-v1",
        "domain": "jyotish",
        "chart": {"system": "sidereal", "lagna": lagna, "moon": moon, "sun": sun, "placements": [f"{planet}:{house}"]},
        "prompt": prompt,
        "response": response,
    }


def bazi_row(rng: random.Random, index: int) -> dict:
    pillars = []
    for offset in range(4):
        stem = BAZI_STEMS[(index * 3 + offset * 2) % len(BAZI_STEMS)]
        branch = BAZI_BRANCHES[(index * 5 + offset * 3) % len(BAZI_BRANCHES)]
        pillars.append((stem, branch))
    day_master = pillars[2][0]
    element = BAZI_ELEMENTS[BAZI_STEMS.index(day_master)]
    action = rng.choice(ACTIONS)
    pillar_text = "; ".join(f"{name}={stem} {branch}" for name, (stem, branch) in zip(("Year", "Month", "Day", "Hour"), pillars))
    response = (
        f"The BaZi chart has {pillar_text}. The Day Master is {day_master}, associated with {element}. "
        f"Read this as a pattern for reflection: {action}. The pillars describe symbolic timing, not a guaranteed outcome."
    )
    prompt = (
        "Write a concise, warm BaZi reading from supplied Four Pillars facts. "
        "Preserve Year, Month, Day, and Hour labels; identify the Day Master; avoid deterministic claims or diagnosis. "
        f"Domain=BaZi; System=Four Pillars; {pillar_text}; DayMaster={day_master}; Element={element}; "
        f"Family={family_context(index)[0]}."
    )
    return {
        "id": f"bazi-{index:06d}",
        "source": "synthetic-seven-domain-v1",
        "domain": "bazi",
        "chart": {
            "system": "four_pillars",
            "pillars": {name.lower(): {"stem": stem, "branch": branch} for name, (stem, branch) in zip(("Year", "Month", "Day", "Hour"), pillars)},
            "day_master": day_master,
            "element": element,
        },
        "prompt": prompt,
        "response": response,
    }


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--rows", type=int, default=50000)
    ap.add_argument("--seed", type=int, default=185)
    ap.add_argument("--out", type=Path, default=Path("data/astrology_human_design.jsonl"))
    args = ap.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    rng = random.Random(args.seed)
    with args.out.open("w", encoding="utf-8") as f:
        for i in range(args.rows):
            index = i // 4
            row = [
                astrology_row,
                human_design_row,
                tarot_row,
                lenormand_row,
                synastry_row,
                jyotish_row,
                bazi_row,
            ][i % 7](rng, index)
            f.write(json.dumps(row, ensure_ascii=False) + "\n")
    print(f"wrote {args.rows} balanced rows to {args.out}")


if __name__ == "__main__":
    main()
