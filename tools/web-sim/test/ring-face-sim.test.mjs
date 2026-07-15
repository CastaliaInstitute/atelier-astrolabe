import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

const timers = [];
const options = [];

class FakeOption {
  constructor() {
    this.value = "";
    this.textContent = "";
  }
}

const fakeSelect = {
  append(option) {
    options.push(option);
  },
  querySelector() {
    return null;
  },
};

const context = {
  console,
  document: {
    createElement(tag) {
      assert.equal(tag, "option");
      return new FakeOption();
    },
    querySelector() {
      return null;
    },
  },
  window: {
    requestAnimationFrame(fn) {
      timers.push(fn);
      return timers.length;
    },
  },
};
context.window.window = context.window;
context.window.document = context.document;

vm.createContext(context);
vm.runInContext(
  fs.readFileSync("tools/web-sim/public/ring-face-sim.js", "utf8"),
  context,
  { filename: "ring-face-sim.js" },
);

const sim = context.window.AstrolabeRingFaceSim;
assert.ok(sim, "ring face sim API is exported");
assert.equal(sim.SPECIAL_FACE_VALUE, "ring24");

sim.install({ select: fakeSelect, setFirmwareFace() {} });
assert.equal(options.length, 1);
assert.equal(options[0].value, "ring24");
assert.match(options[0].textContent, /R10 24h/);

const midnight = sim.minuteToAngle(0);
const noon = sim.minuteToAngle(720);
assert.ok(Math.abs(midnight + Math.PI / 2) < 0.00001, "midnight is at top");
assert.ok(Math.abs(noon - Math.PI / 2) < 0.00001, "noon is at bottom");

assert.equal(sim.colorForStress(15), "#48c9a7");
assert.equal(sim.colorForStress(50), "#e6c65f");
assert.equal(sim.colorForStress(90), "#e86f61");
assert.equal(sim.colorForHrv(25), "#da6b62");
assert.equal(sim.colorForHrv(70), "#d7bd64");
assert.equal(sim.colorForHrv(105), "#70d6a5");

const demo = sim.makeDemoData(new Date(2026, 6, 14, 12, 0, 0));
assert.equal(demo.stress.length, 48);
assert.equal(demo.hrv.length, 48);
assert.equal(demo.spo2.length, 48);
assert.equal(demo.heart.length, 288);
assert.ok(demo.sleep.length >= 3);
assert.equal(demo.nowMinute, 720);
assert.equal(sim.cyclePhase(2, 28), "menstrual");
assert.equal(sim.cyclePhase(12, 28), "ovulatory");
assert.equal(sim.cyclePhase(23, 28), "luteal");
assert.deepEqual(JSON.parse(JSON.stringify(sim.cycleContext({ day: 23, length: 28, source: "calendar" }))), {
  day: 23,
  length: 28,
  phase: "luteal",
  source: "calendar",
  confidence: "estimated",
});

const summary = sim.stressSummary(demo);
assert.ok(summary.score >= 0 && summary.score <= 100);
assert.match(summary.stateLabel, /CALM|RISING|HIGH|RECOVERING/);
assert.equal(summary.chips.length, 6);
assert.ok(summary.chips.some((chip) => chip.label === "HRV"));
assert.ok(summary.chips.some((chip) => chip.label === "SPO2"));
assert.equal(summary.cycle.source, "calendar");
assert.ok(summary.chips.some((chip) => chip.label === "CYCLE*"));
assert.ok(summary.chips.some((chip) => chip.label === "FAMILY"));

const lowSpo2 = sim.stressSummary(sim.makeScenarioData({ hrv: 70, stress: 40, spo2: 91, hour: 8, cycleDay: 8 }));
assert.ok(lowSpo2.chips.some((chip) => chip.label === "SPO2" && chip.tone === "warn"));
assert.equal(lowSpo2.spo2, 91);

const highPhysiology = sim.stressSummary(sim.makeScenarioData({ hrv: 20, stress: 90, spo2: 88, hour: 0, cycleDay: 1 }));
assert.equal(highPhysiology.stateLabel, "HIGH");
assert.equal(highPhysiology.action, "check ring fit");

console.log("ring face websim test passed");
