import fs from "node:fs";
import vm from "node:vm";

const context = {
  console,
  document: {
    createElement() {
      return {};
    },
    querySelector() {
      return null;
    },
  },
  window: {
    requestAnimationFrame() {
      return 1;
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

const hrvValues = [20, 35, 50, 65, 80, 95, 110];
const stressValues = [0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100];
const spo2Values = [88, 90, 92, 94, 96, 98, 100];
const hours = [0, 3, 6, 9, 12, 15, 18, 21];
const cycleDays = Array.from({ length: 28 }, (_, i) => i + 1);

const records = [];
const stateCounts = new Map();
const actionCounts = new Map();
const warningCounts = new Map();
const cycleCounts = new Map();

for (const hrv of hrvValues) {
  for (const stress of stressValues) {
    for (const spo2 of spo2Values) {
      for (const hour of hours) {
        for (const cycleDay of cycleDays) {
          const data = sim.makeScenarioData({
            hrv,
            stress,
            spo2,
            hour,
            cycleDay,
            cycleSource: "calendar",
            trend30m: stress >= 60 ? 10 : 0,
            familyStress: hour >= 18 ? 76 : 45,
            moodEmoji: stress >= 75 ? "😟" : "😐",
          });
          const summary = sim.stressSummary(data);
          const warnChips = summary.chips.filter((chip) => chip.tone === "warn").map((chip) => chip.label);
          for (const chip of warnChips) {
            warningCounts.set(chip, (warningCounts.get(chip) || 0) + 1);
          }
          stateCounts.set(summary.stateLabel, (stateCounts.get(summary.stateLabel) || 0) + 1);
          actionCounts.set(summary.action, (actionCounts.get(summary.action) || 0) + 1);
          cycleCounts.set(summary.cyclePhase, (cycleCounts.get(summary.cyclePhase) || 0) + 1);
          records.push({
            hrv,
            stress,
            spo2,
            hour,
            cycleDay,
            cyclePhase: summary.cyclePhase,
            cycleSource: summary.cycle.source,
            cycleConfidence: summary.cycle.confidence,
            score: summary.score,
            state: summary.stateLabel,
            action: summary.action,
            hrvDelta: summary.hrvDelta,
            hrDelta: summary.hrDelta,
            warnings: warnChips.join("|"),
            mismatch: summary.avatar.mismatch,
          });
        }
      }
    }
  }
}

const outDir = "build/web-sim";
fs.mkdirSync(outDir, { recursive: true });

const summary = {
  generatedAt: new Date().toISOString(),
  ranges: {
    hrv: hrvValues,
    stress: stressValues,
    spo2: spo2Values,
    hours,
    cycleDays: [1, 28],
  },
  count: records.length,
  states: Object.fromEntries([...stateCounts].sort()),
  actions: Object.fromEntries([...actionCounts].sort()),
  warnings: Object.fromEntries([...warningCounts].sort()),
  cyclePhases: Object.fromEntries([...cycleCounts].sort()),
  cycleGuardrail: "Cycle phase is calendar/user-confirmed context only. This sweep does not infer menstruation, ovulation, fertility, pregnancy, or health conditions from ring metrics.",
  examples: {
    lowestScore: records.reduce((best, item) => item.score < best.score ? item : best, records[0]),
    highestScore: records.reduce((best, item) => item.score > best.score ? item : best, records[0]),
    lowSpo2: records.find((item) => item.spo2 < 92 && item.warnings.includes("SPO2")),
    lutealHigh: records.find((item) => item.cyclePhase === "luteal" && item.state === "HIGH"),
  },
};

fs.writeFileSync(`${outDir}/ring-face-sweep-summary.json`, `${JSON.stringify(summary, null, 2)}\n`);

const csvColumns = ["hrv", "stress", "spo2", "hour", "cycleDay", "cyclePhase", "cycleSource", "cycleConfidence", "score", "state", "action", "hrvDelta", "hrDelta", "warnings", "mismatch"];
const csv = [
  csvColumns.join(","),
  ...records.map((record) => csvColumns.map((column) => JSON.stringify(record[column] ?? "")).join(",")),
].join("\n");
fs.writeFileSync(`${outDir}/ring-face-sweep.csv`, `${csv}\n`);

console.log(`ring face sweep scenarios=${records.length}`);
console.log(`summary=${outDir}/ring-face-sweep-summary.json`);
console.log(`csv=${outDir}/ring-face-sweep.csv`);
