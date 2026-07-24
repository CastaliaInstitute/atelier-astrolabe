(function (root) {
  const CONDITIONS = Object.freeze([
    Object.freeze({
      name: "Open",
      icon: "☀",
      guidance: "Make the plan together",
      tone: "open",
    }),
    Object.freeze({
      name: "Easy",
      icon: "◌",
      guidance: "Share the warmth; say the kind thing",
      tone: "easy",
    }),
    Object.freeze({
      name: "Changeable",
      icon: "≈",
      guidance: "Stay curious and check assumptions",
      tone: "changeable",
    }),
    Object.freeze({
      name: "Tender",
      icon: "☂",
      guidance: "Slow down; make room for feelings",
      tone: "tender",
    }),
    Object.freeze({
      name: "Intense",
      icon: "ϟ",
      guidance: "Protect the bond; pause before reacting",
      tone: "intense",
    }),
  ]);

  function condition(value) {
    const index = Number(value);
    return CONDITIONS[
      Number.isInteger(index) && index >= 0 && index < CONDITIONS.length
        ? index
        : 2
    ];
  }

  function normalizeArc(value) {
    return String(value || "")
      .slice(0, 10)
      .split("")
      .map((code) => condition(Number(code)));
  }

  function shiftDate(value, delta) {
    if (!/^\d{4}-\d{2}-\d{2}$/.test(String(value || ""))) return "";
    const date = new Date(`${value}T12:00:00Z`);
    if (!Number.isFinite(date.getTime())) return "";
    date.setUTCDate(date.getUTCDate() + Number(delta || 0));
    return date.toISOString().slice(0, 10);
  }

  function relativeLabel(offset) {
    const days = Number(offset) || 0;
    if (days === 0) return "Today";
    if (days === 1) return "Tomorrow";
    if (days === -1) return "Yesterday";
    return days > 0 ? `${days} days ahead` : `${Math.abs(days)} days ago`;
  }

  const api = Object.freeze({
    CONDITIONS,
    condition,
    normalizeArc,
    relativeLabel,
    shiftDate,
  });
  root.LunaSayRelationship = api;
  if (typeof module !== "undefined" && module.exports) {
    module.exports = api;
  }
})(typeof globalThis !== "undefined" ? globalThis : window);
