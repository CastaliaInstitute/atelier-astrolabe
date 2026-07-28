(function (global) {
  const fixture = {
    subject: "Demo chart · Tallahassee, FL",
    birthUtc: "1972-05-06T13:30:00Z",
    houseSystem: "Placidus",
    placements: [
      ["Sun", "Taurus 16°06′", 11], ["Moon", "Aquarius 16°39′", 9],
      ["Mercury", "Aries 20°43′", 11], ["Venus", "Gemini 27°42′", 1],
      ["Mars", "Gemini 26°09′", 12], ["Jupiter", "Capricorn 8°07′ ℞", 7],
      ["Saturn", "Gemini 6°47′", 12], ["Uranus", "Libra 15°04′ ℞", 4],
      ["Neptune", "Sagittarius 4°23′ ℞", 6], ["Pluto", "Virgo 29°36′ ℞", 4],
      ["Chiron", "Aries 15°11′", 11], ["N. Node", "Capricorn 28°55′", 8],
    ],
    angles: { asc: "Gemini 27°41′", mc: "Pisces 11°16′" },
    structures: [
      ["Venus · Ascendant", "conjunction within 2′"],
      ["Chiron · Uranus", "opposition within 7′"],
      ["Pluto T-square", "Venus and Mars square Pluto"],
    ],
    yearAhead: [
      ["2026-08-12", "Solar eclipse · Leo", "writing, teaching, local ties"],
      ["2026-12-11", "Saturn station direct", "partnerships meet reality"],
      ["2027-02-06", "Solar eclipse · Aquarius", "a shift in the felt center"],
      ["2027-03-19", "Saturn conjunct Chiron", "belonging gains durable structure"],
      ["2027-06-14", "Uranus conjunct Saturn", "a private rule begins to release"],
    ],
  };
  global.AstrolabeChartAnalysisFixture = fixture;
  if (typeof module !== "undefined" && module.exports) module.exports = fixture;
})(typeof window !== "undefined" ? window : globalThis);
