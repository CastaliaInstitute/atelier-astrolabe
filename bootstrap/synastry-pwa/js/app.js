import { captureAuthFromUrl, isSignedIn, signInUrl, signOut } from "./auth.js";
import { SynastryChart } from "./chart.js";
import { hasVoiceConfig, saveConfig, getConfig } from "./config.js";
import { computeBirthPositions, initEphemeris } from "./ephemeris.js";
import {
  cycleTarget,
  getActiveTarget,
  getUserBirth,
  listTargets,
  profileToBirth,
  setUserBirth,
} from "./profiles.js";
import { buildSynastryPrompts, playMp3, postVoiceMessage, requestBriefReading } from "./voice.js";

const $ = (sel) => document.querySelector(sel);

let chart;
let state = {
  loading: true,
  error: null,
  userPos: null,
  targetPos: null,
  aspects: [],
  voiceBusy: false,
};

function setStatus(msg) {
  const el = $("#status-line");
  if (el) el.textContent = msg || "";
}

function setFooterAspect(text) {
  const el = $("#aspect-line");
  if (el) el.textContent = text || "";
}

function setTargetName(name) {
  const el = $("#target-name");
  if (el) el.textContent = name || "";
}

async function recomputeChart() {
  const userBirth = getUserBirth();
  const target = getActiveTarget();
  if (!userBirth.valid) {
    state.error = "Set your birth chart in Settings.";
    setStatus(state.error);
    return;
  }
  if (!target) {
    state.error = "No family profiles — open Settings.";
    setStatus(state.error);
    return;
  }
  try {
    const targetBirth = profileToBirth(target);
    state.userPos = computeBirthPositions(userBirth);
    state.targetPos = computeBirthPositions(targetBirth);
    if (!state.userPos.ok || !state.targetPos.ok) {
      throw new Error("Chart calculation failed.");
    }
    state.aspects = chart.setData(
      state.userPos.lon,
      state.targetPos.lon,
      target.name,
    );
    setTargetName(target.name);
    setFooterAspect(chart.topAspectLine());
    state.error = null;
    setStatus("");
  } catch (e) {
    state.error = e.message || "Chart unavailable";
    setStatus(state.error);
  }
}

async function initApp() {
  captureAuthFromUrl();
  chart = new SynastryChart($("#chart"));
  setStatus("Loading ephemeris…");
  try {
    await initEphemeris(setStatus);
    state.loading = false;
    await recomputeChart();
  } catch (e) {
    state.loading = false;
    setStatus(e.message || "Could not load ephemeris.");
  }
  updateAuthUi();
}

function updateAuthUi() {
  const signed = isSignedIn();
  const voiceOk = hasVoiceConfig();
  $("#auth-label").textContent = signed ? "Signed in" : voiceOk ? "Anon voice" : "Voice: configure";
}

async function onBrief() {
  if (state.voiceBusy || state.loading || state.error) return;
  const userBirth = getUserBirth();
  const target = getActiveTarget();
  if (!state.userPos?.ok || !state.targetPos?.ok || !target) return;
  state.voiceBusy = true;
  setStatus("Preparing reading…");
  $("#btn-brief").disabled = true;
  try {
    const result = await requestBriefReading(
      userBirth,
      target,
      state.userPos,
      state.targetPos,
      state.aspects,
    );
    setStatus(result.reply?.slice(0, 120) || "Playing…");
    if (result.audio) {
      await playMp3(result.audio);
    } else if (result.reply) {
      speakFallback(result.reply);
    }
    setStatus("");
  } catch (e) {
    setStatus(e.message || "Voice failed");
  } finally {
    state.voiceBusy = false;
    $("#btn-brief").disabled = false;
  }
}

function speakFallback(text) {
  if (!window.speechSynthesis) return;
  const u = new SpeechSynthesisUtterance(text);
  u.rate = 0.95;
  speechSynthesis.speak(u);
}

async function onAsk() {
  if (state.voiceBusy || state.loading || state.error) return;
  const question =
    $("#ask-input").value.trim() ||
    prompt("Ask about this synastry chart:", "What pattern matters most today?");
  if (!question) return;
  const userBirth = getUserBirth();
  const target = getActiveTarget();
  if (!state.userPos?.ok || !state.targetPos?.ok || !target) return;
  state.voiceBusy = true;
  setStatus("Thinking…");
  $("#btn-ask").disabled = true;
  try {
    const { system } = buildSynastryPrompts(
      userBirth,
      target,
      state.userPos,
      state.targetPos,
      state.aspects,
    );
    const result = await postVoiceMessage(question, system);
    setStatus(result.reply?.slice(0, 100) || "");
    if (result.audio) await playMp3(result.audio);
    else if (result.reply) speakFallback(result.reply);
  } catch (e) {
    setStatus(e.message || "Voice failed");
  } finally {
    state.voiceBusy = false;
    $("#btn-ask").disabled = false;
  }
}

function onCycle(delta) {
  cycleTarget(delta);
  recomputeChart();
}

function bindGestures() {
  const zone = $("#chart-zone");
  let y0 = 0;
  zone.addEventListener(
    "touchstart",
    (e) => {
      y0 = e.changedTouches[0].clientY;
    },
    { passive: true },
  );
  zone.addEventListener(
    "touchend",
    (e) => {
      const dy = e.changedTouches[0].clientY - y0;
      if (Math.abs(dy) < 40) return;
      onCycle(dy < 0 ? 1 : -1);
    },
    { passive: true },
  );
  window.addEventListener("resize", () => {
    chart.resize();
    if (state.userPos?.ok) chart.draw();
  });
}

function bindSettings() {
  const dialog = $("#settings");
  $("#btn-settings").addEventListener("click", () => {
    const b = getUserBirth();
    const c = getConfig();
    $("#birth-year").value = b.year || 1972;
    $("#birth-month").value = b.month || 5;
    $("#birth-day").value = b.day || 6;
    $("#birth-hour").value = b.hour ?? 11;
    $("#birth-minute").value = b.minute ?? 30;
    $("#birth-place").value = b.place || "";
    $("#birth-tz").value = (b.tzOffsetSec ?? -14400) / 3600;
    $("#supabase-url").value = c.supabaseUrl || "";
    $("#supabase-key").value = c.supabaseAnonKey || "";
    const targets = listTargets();
    $("#targets-list").innerHTML = targets
      .map(
        (t, i) =>
          `<li>${t.name} <span class="muted">(${t.role})</span> ${i === (getStoreActiveIndex()) ? "· active" : ""}</li>`,
      )
      .join("");
    dialog.showModal();
  });

  function getStoreActiveIndex() {
    try {
      const raw = JSON.parse(localStorage.getItem("synastry.profiles.v1"));
      return raw?.activeIndex ?? 0;
    } catch {
      return 0;
    }
  }

  $("#settings-save").addEventListener("click", () => {
    setUserBirth({
      year: Number($("#birth-year").value),
      month: Number($("#birth-month").value),
      day: Number($("#birth-day").value),
      hour: Number($("#birth-hour").value),
      minute: Number($("#birth-minute").value),
      place: $("#birth-place").value,
      tzOffsetSec: Number($("#birth-tz").value) * 3600,
      latDeg: getUserBirth().latDeg,
      lonDeg: getUserBirth().lonDeg,
      valid: true,
    });
    saveConfig({
      supabaseUrl: $("#supabase-url").value.trim(),
      supabaseAnonKey: $("#supabase-key").value.trim(),
    });
    dialog.close();
    updateAuthUi();
    recomputeChart();
  });

  $("#settings-cancel").addEventListener("click", () => dialog.close());
  $("#btn-signin").addEventListener("click", () => {
    window.location.href = signInUrl();
  });
  $("#btn-signout").addEventListener("click", () => {
    signOut();
    updateAuthUi();
  });
}

function bindVoiceHold() {
  const btn = $("#btn-hold");
  const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
  if (!SpeechRecognition) {
    btn.hidden = true;
    return;
  }
  const rec = new SpeechRecognition();
  rec.lang = "en-US";
  rec.interimResults = false;
  rec.maxAlternatives = 1;
  let holding = false;

  const start = () => {
    if (state.voiceBusy || holding) return;
    holding = true;
    btn.classList.add("is-active");
    setStatus("Listening…");
    try {
      rec.start();
    } catch {
      holding = false;
      btn.classList.remove("is-active");
    }
  };
  const stop = () => {
    if (!holding) return;
    holding = false;
    btn.classList.remove("is-active");
    try {
      rec.stop();
    } catch {
      /* ignore */
    }
  };

  rec.onresult = async (ev) => {
    const text = ev.results[0][0].transcript;
    $("#ask-input").value = text;
    setStatus(`Heard: ${text}`);
    await onAsk();
  };
  rec.onerror = () => {
    holding = false;
    btn.classList.remove("is-active");
    setStatus("Could not hear — try Ask or type a question.");
  };
  rec.onend = () => {
    holding = false;
    btn.classList.remove("is-active");
  };

  btn.addEventListener("touchstart", (e) => {
    e.preventDefault();
    start();
  });
  btn.addEventListener("touchend", (e) => {
    e.preventDefault();
    stop();
  });
  btn.addEventListener("mousedown", start);
  btn.addEventListener("mouseup", stop);
  btn.addEventListener("mouseleave", stop);
}

function registerServiceWorker() {
  if ("serviceWorker" in navigator) {
    navigator.serviceWorker.register("./sw.js", { scope: "./" }).catch(() => {});
  }
}

document.addEventListener("DOMContentLoaded", () => {
  $("#btn-prev").addEventListener("click", () => onCycle(-1));
  $("#btn-next").addEventListener("click", () => onCycle(1));
  $("#btn-brief").addEventListener("click", onBrief);
  $("#btn-ask").addEventListener("click", onAsk);
  bindGestures();
  bindSettings();
  bindVoiceHold();
  registerServiceWorker();
  initApp();
});
