const assert = require("node:assert/strict");
const test = require("node:test");
const relationship = require("./relationship.js");

test("relationship arc accepts only ten bounded condition codes", () => {
  const arc = relationship.normalizeArc("012349999999");
  assert.equal(arc.length, 10);
  assert.deepEqual(
    arc.slice(0, 5).map((day) => day.name),
    ["Open", "Easy", "Changeable", "Tender", "Intense"],
  );
  assert.equal(arc[5].name, "Changeable");
});

test("date travel is civil-day stable across month and leap boundaries", () => {
  assert.equal(relationship.shiftDate("2028-02-28", 1), "2028-02-29");
  assert.equal(relationship.shiftDate("2028-02-29", 1), "2028-03-01");
  assert.equal(relationship.shiftDate("2026-01-01", -1), "2025-12-31");
  assert.equal(relationship.shiftDate("not-a-date", 1), "");
});

test("relative date labels remain plain and readable", () => {
  assert.equal(relationship.relativeLabel(0), "Today");
  assert.equal(relationship.relativeLabel(-1), "Yesterday");
  assert.equal(relationship.relativeLabel(14), "14 days ahead");
});
