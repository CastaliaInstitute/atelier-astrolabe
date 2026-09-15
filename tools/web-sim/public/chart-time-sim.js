(function (global) {
  "use strict";

  const FIELD_NAMES = ["year", "month", "day", "hour", "minute"];
  const MONTHS = ["JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"];

  function daysInMonth(year, month) {
    return new Date(Date.UTC(year, month, 0)).getUTCDate();
  }

  function normalize(state) {
    state.month = Math.max(1, Math.min(12, state.month));
    state.day = Math.max(1, Math.min(daysInMonth(state.year, state.month), state.day));
    state.hour = (state.hour + 24) % 24;
    state.minute = (state.minute + 60) % 60;
    return state;
  }

  function createEditor(date) {
    const d = date instanceof Date ? date : new Date(date || Date.UTC(2026, 7, 1, 12, 0));
    const state = {
      year: d.getUTCFullYear(), month: d.getUTCMonth() + 1, day: d.getUTCDate(),
      hour: d.getUTCHours(), minute: d.getUTCMinutes(), selected: 2, editing: false,
    };
    const editor = {
      state,
      fields: FIELD_NAMES.slice(),
      select(delta) {
        state.selected = (state.selected + delta + FIELD_NAMES.length) % FIELD_NAMES.length;
        return state.selected;
      },
      adjust(delta) {
        const field = FIELD_NAMES[state.selected];
        if (field === "year") state.year = Math.max(1900, Math.min(2200, state.year + delta));
        if (field === "month") state.month += delta;
        if (field === "day") state.day += delta;
        if (field === "hour") state.hour += delta;
        if (field === "minute") state.minute += delta;
        normalize(state);
        return editor.snapshot();
      },
      snapshot() { return { ...state }; },
      iso() { return new Date(Date.UTC(state.year, state.month - 1, state.day, state.hour, state.minute)).toISOString(); },
      label() { return `${MONTHS[state.month - 1]} ${String(state.day).padStart(2, "0")} · ${state.year}  ${String(state.hour).padStart(2, "0")}:${String(state.minute).padStart(2, "0")} UTC`; },
    };
    return editor;
  }

  function attach(root, editor) {
    const svg = root.querySelector("svg");
    const status = root.querySelector("[data-chart-status]");
    const title = root.querySelector("[data-chart-title]");
    let press = null;
    let armed = false;
    const rings = [...root.querySelectorAll("[data-ring]")];
    const visualRings = [...root.querySelectorAll("[data-visual-ring]")];
    function render() {
      rings.forEach((ring, i) => ring.classList.toggle("selected", i === editor.state.selected && editor.state.editing));
      visualRings.forEach((ring, i) => ring.classList.toggle("selected", i === editor.state.selected && editor.state.editing));
      title.textContent = editor.label();
      status.textContent = editor.state.editing ? `EDIT ${editor.fields[editor.state.selected].toUpperCase()} · release to set` : "LONG PRESS · select a ring · swipe to adjust";
    }
    function xy(event) { const r = svg.getBoundingClientRect(); return { x: event.clientX - r.left, y: event.clientY - r.top }; }
    svg.addEventListener("pointerdown", (event) => {
      const p = xy(event); press = { ...p, time: performance.now() }; svg.setPointerCapture(event.pointerId);
      setTimeout(() => { if (press && performance.now() - press.time >= 650) { editor.state.editing = true; armed = false; render(); } }, 660);
    });
    svg.addEventListener("pointerup", (event) => {
      if (!press) return;
      const p = xy(event); const dy = p.y - press.y; const dx = p.x - press.x; const dt = performance.now() - press.time;
      if (editor.state.editing && dt >= 650 && Math.abs(dy) > 18) { editor.select(dy < 0 ? 1 : -1); armed = true; render(); }
      else if (editor.state.editing && armed && Math.hypot(dx, dy) > 18) { editor.adjust(dx < 0 || dy < 0 ? 1 : -1); armed = false; render(); }
      else if (editor.state.editing && dt < 650 && Math.hypot(dx, dy) < 18) { editor.state.editing = false; armed = false; render(); }
      press = null;
    });
    render();
    return { render };
  }

  const api = { FIELD_NAMES, MONTHS, daysInMonth, createEditor, attach };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  global.AstrolabeChartTimeSim = api;
})(typeof window !== "undefined" ? window : globalThis);
