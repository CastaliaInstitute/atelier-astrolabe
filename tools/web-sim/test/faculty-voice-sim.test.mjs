import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

class FakeElement {
  constructor(id) {
    this.id = id;
    this.value = "";
    this.textContent = "";
    this.dataset = {};
    this.options = [];
    this.scrollTop = 0;
    this.scrollHeight = 0;
    this.listeners = {};
    this._src = "";
  }

  addEventListener(type, fn) {
    this.listeners[type] = fn;
  }

  set src(value) {
    this._src = value;
    if (this.onload) {
      this.onload();
    }
  }

  get src() {
    return this._src;
  }
}

const elements = new Map();
for (const id of [
  "faculty-voice-lab",
  "faculty-bust",
  "faculty-name",
  "faculty-slug",
  "faculty-voice-stage",
  "faculty-transcript",
  "faculty-demo-turn",
  "faculty-listen",
  "faculty-load",
  "faculty-speak",
  "faculty-voice-log",
]) {
  elements.set(id, new FakeElement(id));
}

elements.get("faculty-transcript").value =
  "Ask Einstein: what is one precise way to test a small machine?";

const faceSelections = [];
const spoken = [];
const timers = [];

class FakeUtterance {
  constructor(text) {
    this.text = text;
    this.rate = 1;
    this.pitch = 1;
    this.voice = null;
  }
}

const context = {
  console,
  document: {
    body: { dataset: {} },
    querySelector(selector) {
      if (selector.startsWith("#")) {
        return elements.get(selector.slice(1)) || null;
      }
      return null;
    },
  },
  window: {
    setTimeout(fn) {
      timers.push(fn);
      return timers.length;
    },
    speechSynthesis: {
      getVoices() {
        return [{ name: "English Test Voice", lang: "en-US" }];
      },
      cancel() {},
      speak(utterance) {
        spoken.push(utterance.text);
        utterance.onend?.();
      },
    },
    SpeechSynthesisUtterance: FakeUtterance,
  },
  SpeechSynthesisUtterance: FakeUtterance,
};
context.window.window = context.window;
context.window.document = context.document;

vm.createContext(context);
vm.runInContext(
  fs.readFileSync("tools/web-sim/public/faculty-voice-sim.js", "utf8"),
  context,
  { filename: "faculty-voice-sim.js" },
);

const sim = context.window.AstrolabeFacultyVoiceSim;
assert.ok(sim, "faculty voice sim API is exported");

const parsed = sim.parseFacultyRequest("Ask Einstein: what is one precise way to test a small machine?");
assert.equal(parsed.faculty.slug, "a.einstein");
assert.equal(parsed.question, "what is one precise way to test a small machine?");

sim.init({
  findFaceId(query) {
    return query === "faculty" ? 0 : -1;
  },
  setFace(faceId) {
    faceSelections.push(faceId);
  },
});

const result = await sim.runTurn(
  "Ask Einstein: what is one precise way to test a small machine?",
  "test",
);

assert.equal(result.faculty.slug, "a.einstein");
assert.match(result.answer, /Einstein:/);
assert.equal(context.document.body.dataset.facultySlug, "a.einstein");
assert.equal(elements.get("faculty-name").textContent, "Albert Einstein");
assert.match(elements.get("faculty-slug").textContent, /slug=a\.einstein/);
assert.match(elements.get("faculty-slug").textContent, /bust=ready/);
assert.ok(faceSelections.includes(0), "faculty face was selected in the WASM face catalog");
assert.ok(spoken.some((line) => line.includes("Einstein:")), "answer was sent through speech synthesis");

const log = elements.get("faculty-voice-log").textContent;
assert.match(log, /\[faces\] set faculty/);
assert.match(log, /\[stt\] test: Ask Einstein/);
assert.match(log, /\[reply\] Einstein:/);
assert.match(log, /\[speak\] Albert Einstein:/);
assert.match(log, /\[turn\] done/);

console.log("faculty voice websim test passed");
