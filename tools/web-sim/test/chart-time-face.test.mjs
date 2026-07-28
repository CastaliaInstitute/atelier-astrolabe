import assert from "node:assert/strict";
import fs from "node:fs";
import test from "node:test";
import vm from "node:vm";

const context = { console, setTimeout, clearTimeout };
context.globalThis = context;
vm.createContext(context);
vm.runInContext(fs.readFileSync(new URL("../public/chart-time-sim.js", import.meta.url), "utf8"), context);
const { createEditor, daysInMonth, FIELD_NAMES } = context.AstrolabeChartTimeSim;
const analysisContext = { console };
analysisContext.globalThis = analysisContext;
vm.createContext(analysisContext);
vm.runInContext(fs.readFileSync(new URL("../public/chart-analysis-fixture.js", import.meta.url), "utf8"), analysisContext);
const analysis = analysisContext.AstrolabeChartAnalysisFixture;

test("chart time editor selects rings and normalizes calendar fields", () => {
  const editor = createEditor(Date.UTC(2024, 1, 29, 23, 59));
  assert.deepEqual(JSON.parse(JSON.stringify(FIELD_NAMES)), ["year", "month", "day", "hour", "minute"]);
  assert.equal(daysInMonth(2024, 2), 29);
  assert.equal(editor.state.selected, 2);
  editor.select(1);
  assert.equal(editor.state.selected, 3);
  editor.adjust(1);
  assert.equal(editor.state.hour, 0);
  editor.select(1);
  editor.adjust(1);
  assert.equal(editor.state.minute, 0);
  assert.match(editor.iso(), /^2024-02-29T00:00:00/);
});

test("detailed analysis fixture preserves chart spine and year-ahead events", () => {
  assert.equal(analysis.birthUtc, "1972-05-06T13:30:00Z");
  assert.equal(analysis.houseSystem, "Placidus");
  assert.equal(analysis.placements.length, 12);
  assert.deepEqual(JSON.parse(JSON.stringify(analysis.angles)), { asc: "Gemini 27°41′", mc: "Pisces 11°16′" });
  assert.equal(analysis.structures.length, 3);
  assert.ok(analysis.structures.some(([title]) => title === "Chiron · Uranus"));
  assert.equal(analysis.yearAhead.length, 5);
  assert.ok(analysis.yearAhead.some(([date]) => date === "2027-03-19"));
});

console.log("chart time face websim test passed");
