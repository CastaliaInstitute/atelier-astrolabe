(function () {
  "use strict";
  const faces = [
    ["faculty", "Faculty", "home", true, "A question is ready. Let us ask which voice and source can help us meet it."],
    ["classic", "Classic Analog", "home", true, "It is time to return to the one thing that deserves your attention now."],
    ["apocalypso", "Apocalypso", "home", true, "The atmosphere is changing. Check the weather, then choose what the day can safely hold."],
    ["digital", "Digital Local", "home", false, "The local time is exact; your next commitment is close enough to prepare for."],
    ["spotify", "Spotify", "home", false, "This song is shaping the room. Keep it, change it, or let the room become quiet."],
    ["notes", "Notes", "commonplace", true, "A small thought is worth keeping. Say it plainly, and return to what you were doing."],
    ["moon", "Moon", "oracle", true, "The Moon marks a phase, not a command. Notice what is growing and what is ready to rest."],
    ["calcifer", "Calcifer", "home", false, "The little fire is awake. What can be warmed, finished, or released today?"],
    ["castalia", "Castalia", "system", true, "Bring a precise question. Castalia will answer briefly and leave judgment with you."],
    ["astrology", "Astrology", "oracle", true, "The sky offers a pattern for reflection. Which part of the pattern feels active now?"],
    ["synastry", "Synastry", "oracle", true, "Two charts meet here. Look for a place where curiosity can replace assumption."],
    ["partner-wellness", "Partner Wellness", "home oracle", true, "Connection begins with attention. Is there a kind check-in you can offer today?"],
    ["tarot", "Tarot", "oracle", true, "A card is a mirror, not a verdict. Name the question before you interpret the image."],
    ["inq", "iNQ Card", "oracle", true, "Today’s inquiry is simple: what deserves a better question?"],
    ["runes", "Runes", "oracle", true, "The rune gives you a symbol. You give it context, proportion, and choice."],
    ["alethiometer", "Alethiometer", "oracle", true, "Set the hands toward your question, then listen for meanings without forcing certainty."],
    ["crystal-ball", "Crystal Ball", "oracle", true, "The future is not fixed. Imagine one possibility clearly enough to test it."],
    ["spectrum", "Spectrum", "instrument", true, "Sound is moving across the spectrum. Listen for shape before naming the source."],
    ["chakra", "Chakra", "instrument", true, "Take one easy breath and notice where the body asks for less effort."],
    ["bowl", "Tibetan Bowl", "instrument", true, "Let the tone arrive, bloom, and disappear without needing another one."],
    ["rocket", "Rocket", "home", true, "The launch window is a reminder: prepare carefully, then commit when the count reaches zero."],
    ["radar", "Radar", "home", true, "The sweep shows nearby motion. Notice what is approaching before deciding what it means."],
    ["weather", "Weather", "home", true, "Outside conditions are part of the plan. Dress, carry, and travel accordingly."],
    ["globe", "Globe", "home", true, "The world is larger than this moment. Choose a place and remember the people living there."],
    ["scale", "Scale Atlas", "home", true, "Choose a scale, hear its interval pattern, and notice how the tonal center changes the feeling."],
    ["almanac", "Almanac", "home oracle", false, "Today belongs to a season. Notice the practical work that this part of the year invites."],
    ["phenology", "Phenology", "home oracle", false, "A local season is visible in small events. What has budded, migrated, ripened, or gone quiet?"],
    ["sky", "Sky", "home", true, "Look up when you can. The sky is present beyond the forecast and the screen."],
    ["quotes", "Quotes", "commonplace", true, "Keep only the sentence that still asks something of your life."],
    ["transits", "Live Transits", "oracle", true, "A transit describes timing, not fate. What response remains yours to choose?"],
    ["ocarina", "Ocarina", "instrument", true, "Breathe gently across the instrument and let one clear tone be enough."],
    ["pitch", "Pitch Pipe", "instrument", true, "Here is your reference tone. Match it softly before adding volume."],
    ["bongo", "Bongo", "instrument", true, "Find a pulse you can sustain, then leave enough space to hear the room."],
    ["piano", "Piano", "instrument", true, "Begin with two notes. Listen to their relationship before adding a third."],
    ["kalimba", "Kalimba", "instrument", true, "Pluck one quiet pattern and allow the overtones to finish the phrase."],
    ["drone", "Drone", "instrument", true, "A steady tone can hold the center while everything else changes around it."],
    ["chord", "Chord", "instrument", true, "Hear the chord as several voices sharing one moment."],
    ["level", "Level", "instrument", true, "The horizon is slightly tilted. Adjust slowly until balance becomes effortless."],
    ["tuning", "Tuning", "instrument", true, "You are close to the pitch. Make the smallest adjustment you can hear."],
    ["pandrum", "Pan Drum", "instrument", true, "Tap lightly, wait for the resonance, and answer it with a different tone."],
    ["orient", "Orientation", "instrument", true, "Pause and find up, forward, and home before choosing a direction."],
    ["luopan", "Luopan", "oracle", false, "Stand still long enough to know your bearing before interpreting the space."],
    ["qday", "Question Day", "commonplace", true, "Carry one honest question today, and do not hurry it into an answer."],
    ["focus", "Focus Timer", "home", true, "The interval has begun. Protect this small field of attention until the bell."],
    ["bio", "Biometrics", "home", true, "Your body is reporting, not grading. Notice the trend and respond with care."],
    ["arc-reactor", "Arc Reactor", "home", true, "Power is available, but it still needs direction. Choose the work worth energizing."],
    ["watcher", "Watcher", "system", true, "The watch is observing system health. No intervention is needed unless a signal persists."],
    ["lenormand", "Lenormand", "oracle", true, "Read the cards as a sentence about the situation, then check it against reality."],
    ["pythia", "Pythia", "oracle", false, "The oracle speaks in ambiguity. Ask what can be tested before trusting an interpretation."],
    ["geomancy", "Geomancy", "oracle", true, "Cast the figures, name the context, and keep chance separate from authority."],
    ["enochian", "Enochian", "oracle", true, "Treat the symbols as a contemplative language, never as a substitute for judgment."],
    ["hid", "Presentation Remote", "system", true, "Presentation mode is ready. Advance only when the room has finished with this slide."],
    ["babel", "Babel Fish", "commonplace", true, "I can translate the words. Please keep the speaker’s intent and context in the conversation."],
    ["human-design", "Human Design", "oracle", true, "Use the bodygraph as a prompt for reflection, not a boundary around who you may become."],
    ["maze", "Maze", "home", true, "The route is not visible all at once. Choose the next honest turn."],
    ["deathstar", "Death Star", "home", true, "Systems that look invulnerable still have dependencies. Find the smallest critical path."],
    ["solar", "Solar", "oracle", true, "The Sun sets the day’s broad rhythm. Notice the available light and spend it deliberately."],
    ["magnetosphere", "Magnetosphere", "oracle", true, "Solar weather is pressing on Earth’s field. Observe the disturbance without borrowing alarm."],
    ["tron", "Tron", "home", false, "The grid is active. Follow the clean line and avoid needless collisions."],
    ["wscan", "Wi-Fi Scan", "system", true, "Nearby networks are visible. Scan only where you have permission to observe."],
    ["deauth", "Deauthentication Lab", "system", true, "This security exercise can disrupt connections. Continue only inside an authorized lab."],
    ["eviltwin", "Evil Twin Lab", "system", true, "A familiar name does not prove identity. Verify the network before trusting it."],
    ["handshake", "Handshake Lab", "system", true, "Capture only authorized test traffic, then protect or delete the resulting evidence."],
    ["incidents", "Wi-Fi Incidents", "system", true, "An incident is recorded. Review the evidence before assigning cause or severity."],
    ["settings", "Settings", "system", true, "Change only what you intend to keep, and leave the rest of the instrument quiet."],
    ["pocketwatch", "Pocket Watch", "home", true, "Time is close at hand. Close the cover when the glance has done its work."],
    ["battery", "Battery", "system", true, "Power is sufficient for now. Charge when convenient rather than waiting for urgency."],
  ].map(([slug, title, categories, ported, tts]) => ({ slug, title, categories: categories.split(" "), ported, tts }));

  const catalog = document.querySelector("[data-face-catalog]");
  if (!catalog) return;
  const search = document.querySelector("[data-face-search]");
  const category = document.querySelector("[data-face-category]");
  const available = document.querySelector("[data-face-available]");
  const result = document.querySelector("[data-face-result]");
  const detailPages = new Map([
    ["alethiometer", "alethiometer"], ["apocalypso", "apocalypso"], ["astrology", "astro"],
    ["babel", "babel-fish"], ["bio", "biometrics"], ["bongo", "bongo"], ["bowl", "bowl"],
    ["calcifer", "calcifer"], ["castalia", "castalia"], ["chakra", "chakra"], ["classic", "classic"],
    ["digital", "digital"], ["enochian", "enochian"], ["faculty", "faculty"], ["focus", "focus"],
    ["geomancy", "geomancy"], ["human-design", "human-design"], ["lenormand", "lenormand"],
    ["level", "level"], ["luopan", "luopan"], ["moon", "moon"], ["notes", "notes"],
    ["ocarina", "ocarina"], ["pandrum", "pandrum"], ["piano", "piano"], ["pythia", "pythia"],
    ["quotes", "quotes"], ["radar", "radar"], ["rocket", "rocket"], ["runes", "runes"],
    ["settings", "settings"], ["spectrum", "spectrum"], ["spotify", "spotify"], ["synastry", "synastry"],
    ["tarot", "tarot"], ["transits", "transits"], ["tuning", "tuning"], ["weather", "weather"]
  ]);

  function speak(face, button) {
    if (!("speechSynthesis" in window)) { button.textContent = "Speech unavailable"; button.disabled = true; return; }
    window.speechSynthesis.cancel();
    const utterance = new SpeechSynthesisUtterance(face.tts);
    utterance.rate = 0.92; utterance.pitch = 0.96;
    button.textContent = "Speaking…"; button.setAttribute("aria-pressed", "true");
    const reset = () => { button.textContent = "Hear example"; button.setAttribute("aria-pressed", "false"); };
    utterance.onend = reset; utterance.onerror = reset; window.speechSynthesis.speak(utterance);
  }

  function render() {
    const needle = search.value.trim().toLowerCase();
    const family = category.value;
    const visible = faces.filter((face) => (!needle || `${face.title} ${face.slug} ${face.tts}`.toLowerCase().includes(needle)) && (family === "all" || face.categories.includes(family)) && (!available.checked || face.ported));
    catalog.replaceChildren(...visible.map((face) => {
      const card = document.createElement("article"); card.className = "face-catalog-card";
      const detailPath = detailPages.get(face.slug);
      const heading = detailPath ? `<h2><a href="${detailPath}.html">${face.title}</a></h2>` : `<h2>${face.title}</h2>`;
      card.innerHTML = `<div class="face-catalog-card__head"><p class="eyebrow">${face.categories.join(" · ")}</p><span class="face-status face-status--${face.ported ? "available" : "planned"}">${face.ported ? "Available" : "Planned"}</span></div>${heading}<p class="face-catalog-card__slug"><code>${face.slug}</code></p><blockquote>${face.tts}</blockquote><button class="btn btn--ghost face-tts-button" type="button" aria-pressed="false">Hear example</button>`;
      card.querySelector("button").addEventListener("click", (event) => speak(face, event.currentTarget)); return card;
    }));
    result.textContent = `Showing ${visible.length} of ${faces.length} faces`;
  }

  document.querySelector("[data-face-count]").textContent = faces.length;
  document.querySelector("[data-ported-count]").textContent = faces.filter((face) => face.ported).length;
  document.querySelector("[data-planned-count]").textContent = faces.filter((face) => !face.ported).length;
  search.addEventListener("input", render); category.addEventListener("change", render); available.addEventListener("change", render); render();
}());
