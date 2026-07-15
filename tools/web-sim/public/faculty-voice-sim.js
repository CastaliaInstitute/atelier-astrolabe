(function () {
  "use strict";

  const DEFAULT_TRANSCRIPT = "Ask Einstein: what is one precise way to test a small machine?";

  const FACULTY = [
    {
      slug: "hypatia",
      name: "Hypatia",
      asset: "assets/faculty/hypatia.png",
      aliases: ["hypatia"],
      voiceTerms: ["female", "en"],
      voiceStyle: "clear, mathematical",
    },
    {
      slug: "a.einstein",
      name: "Albert Einstein",
      asset: "assets/faculty/a.einstein.png",
      aliases: ["einstein", "albert einstein", "a einstein", "a.einstein"],
      voiceTerms: ["male", "en"],
      voiceStyle: "warm, precise",
    },
    {
      slug: "marie-curie",
      name: "Marie Curie",
      asset: "assets/faculty/marie-curie.png",
      aliases: ["curie", "marie curie", "m curie"],
      voiceTerms: ["female", "en"],
      voiceStyle: "measured, experimental",
    },
  ];

  const state = {
    activeFaculty: FACULTY[0],
    transcript: "",
    question: "",
    answer: "",
    stage: "idle",
    options: {},
    els: {},
  };

  function byId(id) {
    return document.querySelector(`#${id}`);
  }

  function findFaculty(name) {
    const normalized = String(name || "").trim().toLowerCase().replace(/[.:]+/g, " ");
    return FACULTY.find((faculty) => {
      if (faculty.name.toLowerCase() === normalized) {
        return true;
      }
      return faculty.aliases.some((alias) => alias === normalized || normalized.includes(alias));
    }) || null;
  }

  function parseFacultyRequest(text) {
    const transcript = String(text || "").trim();
    const match = transcript.match(/\bask\s+([a-z][a-z .'-]{1,48}?)(?::|,|\?| about\b| what\b| how\b| why\b| when\b|$)/i);
    let faculty = null;
    let question = transcript;

    if (match) {
      faculty = findFaculty(match[1]);
      const start = match.index + match[0].length;
      question = transcript.slice(start).replace(/^[\s:,.?-]+/, "").trim();
      if (!question && /\b(?:what|how|why|when)\b/i.test(match[0])) {
        const word = match[0].match(/\b(?:what|how|why|when)\b/i);
        question = transcript.slice(match.index + word.index).trim();
      }
    }

    if (!faculty) {
      faculty = FACULTY.find((entry) => transcript.toLowerCase().includes(entry.aliases[0])) || state.activeFaculty;
    }
    if (!question) {
      question = "what is one precise way to test a small machine?";
    }

    return { faculty, question, transcript };
  }

  function answerFor(faculty, question) {
    if (faculty.slug === "a.einstein") {
      return "Einstein: Change one condition at a time, predict the effect before you run it, then compare the observation with the prediction.";
    }
    if (faculty.slug === "marie-curie") {
      return "Curie: Isolate the variable, record the measurement plainly, and repeat until the result survives your doubt.";
    }
    return `${faculty.name}: Begin with a clean question, name the assumption, and let the result correct the method.`;
  }

  function setStage(stage) {
    state.stage = stage;
    if (state.els.lab) {
      state.els.lab.dataset.stage = stage;
    }
    if (state.els.stage) {
      state.els.stage.textContent = stage;
    }
  }

  function log(line) {
    if (!state.els.log) {
      return;
    }
    const prefix = state.els.log.textContent ? "\n" : "";
    state.els.log.textContent += `${prefix}${line}`;
    state.els.log.scrollTop = state.els.log.scrollHeight;
  }

  function fallbackAsset(faculty) {
    const initials = faculty.name.split(/\s+/).map((part) => part[0]).join("").slice(0, 2).toUpperCase();
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 148 148"><rect width="148" height="148" fill="#101821"/><circle cx="74" cy="62" r="31" fill="#d7b58b"/><path d="M26 142c7-34 25-51 48-51s41 17 48 51" fill="#2f5878"/><text x="74" y="132" text-anchor="middle" fill="#e9eef2" font-family="Arial" font-size="24">${initials}</text></svg>`;
    return `data:image/svg+xml,${encodeURIComponent(svg)}`;
  }

  function renderFaculty(faculty) {
    document.body.dataset.facultySlug = faculty.slug;
    if (state.els.name) {
      state.els.name.textContent = faculty.name;
    }
    if (state.els.slug) {
      state.els.slug.textContent = `slug=${faculty.slug} bust=ready voice=${faculty.voiceStyle}`;
    }
    if (state.els.bust) {
      state.els.bust.alt = `${faculty.name} faculty bust`;
      state.els.bust.onerror = () => {
        state.els.bust.src = fallbackAsset(faculty);
      };
      state.els.bust.onload = () => log(`[bust] ready slug=${faculty.slug}`);
      state.els.bust.src = faculty.asset;
    }
  }

  function showFirmwareFacultyFace(attempt = 0) {
    if (typeof state.options.setFace !== "function" || typeof state.options.findFaceId !== "function") {
      return false;
    }
    const faceId = state.options.findFaceId("faculty");
    if (faceId >= 0) {
      state.options.setFace(faceId);
      log("[faces] set faculty");
      return true;
    }
    if (attempt < 20) {
      window.setTimeout(() => showFirmwareFacultyFace(attempt + 1), 100);
    }
    return false;
  }

  function loadFacultyBySlug(slug, source = "manual") {
    const faculty = FACULTY.find((entry) => entry.slug === slug) || FACULTY[0];
    state.activeFaculty = faculty;
    setStage("bust-loading");
    renderFaculty(faculty);
    showFirmwareFacultyFace();
    setStage("bust-ready");
    log(`[faculty] ${source} slug=${faculty.slug} name=${faculty.name}`);
    return faculty;
  }

  function pickVoice(faculty) {
    if (!("speechSynthesis" in window)) {
      return null;
    }
    const voices = window.speechSynthesis.getVoices();
    const terms = faculty.voiceTerms || [];
    return voices.find((voice) => terms.every((term) => voice.name.toLowerCase().includes(term))) ||
      voices.find((voice) => /^en[-_]/i.test(voice.lang)) ||
      voices[0] ||
      null;
  }

  function speak(text, faculty = state.activeFaculty) {
    const line = text || state.answer;
    if (!line) {
      return Promise.resolve(false);
    }
    log(`[speak] ${faculty.name}: ${line}`);
    if (!("speechSynthesis" in window) || !("SpeechSynthesisUtterance" in window)) {
      setStage("spoken");
      return Promise.resolve(false);
    }
    return new Promise((resolve) => {
      const utterance = new SpeechSynthesisUtterance(line);
      utterance.rate = faculty.slug === "a.einstein" ? 0.92 : 0.98;
      utterance.pitch = faculty.slug === "marie-curie" ? 1.04 : 0.96;
      utterance.voice = pickVoice(faculty);
      utterance.onend = () => {
        setStage("spoken");
        resolve(true);
      };
      utterance.onerror = () => {
        setStage("spoken");
        resolve(false);
      };
      setStage("speaking");
      window.speechSynthesis.cancel();
      window.speechSynthesis.speak(utterance);
    });
  }

  async function runTurn(transcript, source = "demo") {
    const parsed = parseFacultyRequest(transcript || state.els.transcript.value || DEFAULT_TRANSCRIPT);
    state.transcript = parsed.transcript;
    state.question = parsed.question;
    if (state.els.transcript) {
      state.els.transcript.value = parsed.transcript;
    }

    setStage("stt-complete");
    log(`[stt] ${source}: ${parsed.transcript}`);
    const faculty = loadFacultyBySlug(parsed.faculty.slug, "request");
    state.answer = answerFor(faculty, parsed.question);
    setStage("answered");
    log(`[reply] ${state.answer}`);
    await speak(state.answer, faculty);
    log("[turn] done");
    return { faculty, question: parsed.question, answer: state.answer };
  }

  function listen() {
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!SpeechRecognition) {
      log("[stt] unavailable: browser SpeechRecognition missing");
      return Promise.resolve(null);
    }
    const recognition = new SpeechRecognition();
    recognition.lang = "en-US";
    recognition.interimResults = false;
    recognition.maxAlternatives = 1;
    setStage("listening");
    log("[listen] browser audio input armed");
    return new Promise((resolve) => {
      recognition.onresult = (event) => {
        const transcript = event.results[0][0].transcript;
        runTurn(transcript, "browser").then(resolve);
      };
      recognition.onerror = (event) => {
        setStage("stt-error");
        log(`[stt] error: ${event.error || "unknown"}`);
        resolve(null);
      };
      recognition.onend = () => {
        if (state.stage === "listening") {
          setStage("idle");
        }
      };
      recognition.start();
    });
  }

  function init(options = {}) {
    state.options = options;
    state.els = {
      lab: byId("faculty-voice-lab"),
      bust: byId("faculty-bust"),
      name: byId("faculty-name"),
      slug: byId("faculty-slug"),
      stage: byId("faculty-voice-stage"),
      transcript: byId("faculty-transcript"),
      demo: byId("faculty-demo-turn"),
      listen: byId("faculty-listen"),
      load: byId("faculty-load"),
      speak: byId("faculty-speak"),
      log: byId("faculty-voice-log"),
    };

    if (state.els.transcript && !state.els.transcript.value.trim()) {
      state.els.transcript.value = DEFAULT_TRANSCRIPT;
    }
    loadFacultyBySlug("hypatia", "init");

    state.els.demo?.addEventListener("click", () => runTurn(DEFAULT_TRANSCRIPT, "demo"));
    state.els.listen?.addEventListener("click", () => listen());
    state.els.load?.addEventListener("click", () => {
      const parsed = parseFacultyRequest(state.els.transcript.value);
      log(`[stt] typed: ${parsed.transcript}`);
      loadFacultyBySlug(parsed.faculty.slug, "typed");
    });
    state.els.speak?.addEventListener("click", () => speak(state.answer || answerFor(state.activeFaculty, "")));

    return api;
  }

  const api = {
    init,
    parseFacultyRequest,
    loadFacultyBySlug,
    runTurn,
    speak,
    getState: () => ({
      activeFaculty: state.activeFaculty,
      transcript: state.transcript,
      question: state.question,
      answer: state.answer,
      stage: state.stage,
    }),
    faculty: FACULTY.slice(),
  };

  window.AstrolabeFacultyVoiceSim = api;
})();
