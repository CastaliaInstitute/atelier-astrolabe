(function () {
  window.astrolabeVariants = [
    {
      id: "pocket",
      title: "Astrolabe Pocket",
      subtitle: "Pocket SKU",
      promise: "Orientation · time · rhythm",
      accessory: "Pocket watch chain and carry-first presentation.",
    },
    {
      id: "lunasay",
      title: "Astrolabe LunaSay",
      subtitle: "Desktop dock SKU",
      promise: "Reflection · insight · timing",
      accessory: "Magnetic desktop dock for daily lunar and chart reflection.",
    },
    {
      id: "ocarina",
      title: "Astrolabe Ocarina",
      subtitle: "Breath / music SKU",
      promise: "Calm · tone · harmony",
      accessory: "Breath, sound, and music companion tuning.",
    },
    {
      id: "cameo",
      title: "Astrolabe Cameo",
      subtitle: "Necklace pendant SKU",
      promise: "Remember · honor · connect",
      accessory: "Pendant presentation for memory, presence, and keepsake rituals.",
    },
    {
      id: "enso",
      title: "Astrolabe Enso",
      subtitle: "Forehead / focus SKU",
      promise: "Clarity · presence · flow",
      accessory: "Headband and contact interface for quieting the mind.",
    },
    {
      id: "luopan",
      title: "Astrolabe Luopan",
      subtitle: "Compass SKU",
      promise: "Direction · alignment · space",
      accessory: "Compass-oriented kit for space, bearing, and alignment.",
    },
    {
      id: "core",
      title: "Core Firmware",
      subtitle: "All SKUs",
      promise: "Setup · identity · connection",
      accessory: "Shared module behavior available across every Astrolabe variant.",
    },
  ];

  window.astrolabeFaces = [
    {
      slug: "classic",
      title: "Classic Analog",
      kicker: "Time as a quiet home base",
      image: "../assets/faces/bench-00-classic.png",
      summary:
        "The default glance: local time, a circadian hue field, and the calmest place to begin a question.",
      inquiry:
        "What time is it, and what kind of time does this part of the day feel like?",
      questions: [
        "Is this a moment for action, rest, or attention?",
        "What is the household rhythm right now?",
        "Do I want to capture a commonplace note before it disappears?",
      ],
      use: [
        "Glance for analog time without opening another rectangular screen.",
        "Hold PWR to capture a Commonplace note from the home face.",
        "Press BOOT to replay the last spoken answer or hear an agenda brief.",
      ],
      interactions: "Swipe left or right to move through the dial. Tap returns attention to the center.",
    },
    {
      slug: "apocalypso",
      title: "Apocalypso",
      kicker: "Weather with consequence",
      image: "../assets/faces/bench-01-apocalypso.png",
      summary:
        "A climate and weather glance for asking how the outside world is pressing on the day.",
      inquiry: "What is the atmosphere asking of me before I step outside?",
      questions: [
        "Will weather change the plan?",
        "Is there a practical risk I should prepare for?",
        "What should I carry, postpone, or check before leaving?",
      ],
      use: [
        "Read the face before a walk, school run, errand, or commute.",
        "Use it as an environmental cue, not a full forecast feed.",
        "Let the watch keep the question embodied: what is happening outside?",
      ],
      interactions: "Swipe left or right to continue through faces.",
    },
    {
      slug: "digital",
      title: "Digital",
      kicker: "Exact time, no interpretation",
      image: "../assets/faces/bench-02-digital.png",
      summary:
        "A large local-time face for moments when precision matters more than mood or symbolism.",
      inquiry: "What is the exact time, and what commitment does that imply?",
      questions: [
        "How much time do I have before the next thing?",
        "Am I early, late, or exactly where I need to be?",
        "Do I need the agenda read aloud?",
      ],
      use: [
        "Glance when analog estimation is too slow.",
        "Press BOOT for an agenda brief when there is no reply to replay.",
        "Keep attention on time without inviting a notification loop.",
      ],
      interactions: "Swipe left or right to move through the dial.",
    },
    {
      slug: "spotify",
      title: "Spotify",
      kicker: "Music as household atmosphere",
      image: "../assets/faces/bench-03-spotify.png",
      summary:
        "A compact music face for noticing what is playing and nudging the listening context.",
      inquiry: "What sound is shaping this room right now?",
      questions: [
        "What is playing?",
        "Does the room need continuity, quiet, or a change in energy?",
        "Is this track part of the mood I want to keep?",
      ],
      use: [
        "Check the current track without picking up a phone.",
        "Use transport controls when the face is active.",
        "Treat music as part of the household field, not a scrollable library.",
      ],
      interactions: "Tap the transport area, double tap, or swipe up and down for music controls. Swipe left exits the face.",
    },
    {
      slug: "astro",
      title: "Astrology",
      kicker: "The chart as a question wheel",
      image: "../assets/faces/bench-04-astro.png",
      summary:
        "A transit wheel for asking what pattern is active now and where attention might gather.",
      inquiry: "What is being emphasized in the sky right now?",
      questions: [
        "Which body or sign is asking for attention?",
        "What tension, support, or timing pattern should I notice?",
        "How does today’s sky change the question I am already carrying?",
      ],
      use: [
        "Read the current transit wheel as a prompt for reflection.",
        "Tap highlighted regions to focus the reading.",
        "Ask Castalia aloud when the symbols raise a more specific question.",
      ],
      interactions: "Tap the lower chart region for focus. Swipe left to continue.",
    },
    {
      slug: "moon",
      title: "Moon",
      kicker: "Phase, texture, and nightly attention",
      image: "../assets/faces/bench-05-moon.png",
      summary:
        "A lunar face for asking what kind of night this is and what phase of a cycle you are in.",
      inquiry: "Where am I in the cycle: beginning, swelling, fullness, release, or rest?",
      questions: [
        "What is the Moon doing tonight?",
        "What is ready to be noticed, named, or let go?",
        "What daily fortune or prompt belongs to this phase?",
      ],
      use: [
        "Glance for phase and illumination.",
        "Tap for a daily fortune-style prompt.",
        "Swipe up or down to move through moon-focused modes.",
      ],
      interactions: "Tap the center for the day’s lunar prompt. Swipe up or left to navigate.",
    },
    {
      slug: "calcifer",
      title: "Hue Daywheel",
      kicker: "Calendar time in a twelve-hour ring",
      image: "../assets/faces/bench-06-calcifer.png",
      summary:
        "A rolling daywheel for seeing upcoming events, countdowns, and color time at once.",
      inquiry: "What is approaching, and how much space is there before it arrives?",
      questions: [
        "What is the next appointment or household event?",
        "How much transition time do I really have?",
        "What does the next twelve hours feel like as a whole?",
      ],
      use: [
        "Use the ring as a gentle countdown instead of a calendar grid.",
        "Notice event wedges before they become urgent.",
        "Press BOOT for a spoken agenda brief.",
      ],
      interactions: "Swipe left or right to move through faces. BOOT speaks agenda when available.",
    },
    {
      slug: "cycle",
      title: "Cycle",
      kicker: "Wellness timing in a ring",
      summary:
        "A menstrual-cycle and pregnancy wellness glance that maps cycle timing onto the round display.",
      inquiry: "Where am I in this body cycle, and what kind of care does that suggest?",
      questions: [
        "What phase of the cycle am I in today?",
        "Is this a day for energy, tenderness, preparation, or rest?",
        "What dates or body signals should I keep visible without opening a tracker?",
      ],
      use: [
        "Tap to log period started today when the face is active.",
        "Swipe through cycle length presets.",
        "Use Settings for last period, cycle length, period length, and pregnancy due date.",
      ],
      interactions: "Tap logs a new period start. Swipe up or down adjusts the cycle-length preset.",
    },
    {
      slug: "castalia",
      title: "Castalia Sign-In",
      kicker: "Account context for spoken answers",
      image: "../assets/faces/bench-07-castalia.png",
      summary:
        "A QR sign-in surface that connects the instrument to your Castalia account and household context.",
      inquiry: "Who is asking, and what context should Castalia be allowed to use?",
      questions: [
        "Am I signed in?",
        "Should this watch use my Castalia identity for voice answers?",
        "Is the next question anonymous or personal?",
      ],
      use: [
        "Open the Castalia page from Settings.",
        "Scan the QR code to pair or refresh sign-in.",
        "Return to the dial once account context is ready.",
      ],
      interactions: "Reached through the Settings hub rather than the normal left-right dial.",
    },
    {
      slug: "settings",
      title: "Settings",
      kicker: "The practical doorway",
      image: "../assets/faces/bench-08-settings.png",
      summary:
        "The hub for Wi-Fi, Castalia pairing, and device configuration that should not interrupt daily use.",
      inquiry: "What does the instrument need before it can answer well?",
      questions: [
        "Is Wi-Fi connected?",
        "Do I need to update account, family, location, or cycle settings?",
        "What setup task is blocking the face I want to use?",
      ],
      use: [
        "Open setup pages by QR when keyboard entry belongs on a larger device.",
        "Move between Wi-Fi and Castalia settings.",
        "Use it briefly, then return to the instrument surfaces.",
      ],
      interactions: "Swipe between settings pages. Swipe up returns from Settings.",
    },
    {
      slug: "synastry",
      title: "Synastry",
      kicker: "Relationship patterns at a glance",
      image: "../assets/faces/bench-09-synastry.png",
      summary:
        "A dual-chart face for saved partner and family profiles, built for questions about relational timing.",
      inquiry: "What pattern appears between us today?",
      questions: [
        "Which relationship field is active?",
        "Where is there harmony, friction, or a need for care?",
        "What conversation would benefit from better timing?",
      ],
      use: [
        "Compare saved profiles without opening a charting app.",
        "Use aspect highlights as a prompt, not a verdict.",
        "Ask a more specific question aloud when the face surfaces tension.",
      ],
      interactions: "Tap the center to focus the relationship view. Swipe left to continue.",
    },
    {
      slug: "spectrum",
      title: "Spectrum",
      kicker: "Sound made visible",
      image: "../assets/faces/bench-10-spectrum.png",
      summary:
        "A polar audio visualizer for seeing microphone and speaker energy on the round screen.",
      inquiry: "What is the room sounding like right now?",
      questions: [
        "Is the room quiet, noisy, balanced, or overloaded?",
        "What frequencies dominate the current sound?",
        "Does the listening environment need adjustment?",
      ],
      use: [
        "Watch live sound energy without turning it into a recording task.",
        "Compare input and output energy when audio is active.",
        "Use visual feedback to tune room volume and presence.",
      ],
      interactions: "Swipe up or down to change visualization mode. Tap to center attention.",
    },
    {
      slug: "chakra",
      title: "Chakra",
      kicker: "Tone, symbol, and body attention",
      image: "../assets/faces/bench-11-chakra.png",
      summary:
        "A solfeggio tone face for asking where attention wants to settle in the body.",
      inquiry: "Where does attention want to land before I speak or act?",
      questions: [
        "Which center feels present, blocked, or overactive?",
        "What tone helps me settle?",
        "What should be felt before it is explained?",
      ],
      use: [
        "Tap to toggle the current tone.",
        "Swipe up and down through centers.",
        "Use it as a short ritual before a voice question.",
      ],
      interactions: "Tap toggles tone. Swipe up or down changes chakra. Swipe left exits.",
    },
    {
      slug: "bowl",
      title: "Tibetan Bowl",
      kicker: "A touch instrument for settling",
      image: "../assets/faces/bench-12-bowl.png",
      summary:
        "A singing bowl face for striking tones from the rim and watching ripples answer the gesture.",
      inquiry: "Can I settle the room before I ask the question?",
      questions: [
        "What tone helps the moment become quieter?",
        "How does touch change the sound?",
        "Is the question ready, or do I need one more breath?",
      ],
      use: [
        "Touch or drag the rim to strike.",
        "Swipe through bowl presets.",
        "Use sound as preparation rather than entertainment alone.",
      ],
      interactions: "Tap or drag near the circumference to strike. Swipe up or down changes preset.",
    },
    {
      slug: "rocket",
      title: "Rocket",
      kicker: "Launches on a time dial",
      image: "../assets/faces/bench-13-rocket.png",
      summary:
        "A launch clock for seeing upcoming orbital launches as events in the near future.",
      inquiry: "What is leaving Earth soon, and when should I look up?",
      questions: [
        "What launches are approaching?",
        "Which mission is next?",
        "Is there a stream or window I want to follow?",
      ],
      use: [
        "Glance at the launch window instead of checking a feed.",
        "Use the dial to understand launches as time, not a list.",
        "Tap when the face offers mission detail or stream context.",
      ],
      interactions: "Tap the center for launch detail. Swipe left continues through the dial.",
    },
    {
      slug: "radar",
      title: "Radar",
      kicker: "Presence without a feed",
      image: "../assets/faces/bench-14-radar.png",
      summary:
        "A BLE peer radar for asking who or what is nearby in the Castalia instrument field.",
      inquiry: "What presence is around me right now?",
      questions: [
        "Which nearby devices or anchors are visible?",
        "Is the household constellation changing?",
        "What is near, what is far, and what is stable?",
      ],
      use: [
        "Read approximate peer positions and distance rings.",
        "Use bearing as a relational cue, not as survey-grade location.",
        "Tap to re-center attention on the presence field.",
      ],
      interactions: "Swipe left or right to navigate. Tap the radar center to focus.",
    },
    {
      slug: "faculty",
      title: "Faculty",
      kicker: "Ask from a named perspective",
      image: "../assets/faces/bench-15-faculty.png",
      summary:
        "A face for recent ask-faculty conversations, keeping a chosen voice or lens close at hand.",
      inquiry: "Which perspective should help me think about this?",
      questions: [
        "Who should I ask this as?",
        "What did this faculty voice last help me notice?",
        "Do I need a practical answer, a poetic answer, or a sharper question?",
      ],
      use: [
        "Swipe through recent faculty voices.",
        "Hold PWR to ask in context when voice input is enabled.",
        "Use the bust and name as a prompt for angle of inquiry.",
      ],
      interactions: "Swipe up or down cycles recent faculty. Swipe left exits.",
    },
    {
      slug: "weather",
      title: "Weather",
      kicker: "Conditions as a 24-hour ring",
      image: "../assets/faces/bench-16-weather.png",
      summary:
        "A radial weather face for temperature, humidity, and current conditions across the day.",
      inquiry: "How will the day’s conditions unfold?",
      questions: [
        "When does the day warm, cool, dry, or turn damp?",
        "What should change about clothing, errands, or outdoor plans?",
        "Is the center condition enough, or do I need the whole arc?",
      ],
      use: [
        "Glance for current condition in the center.",
        "Read rings for the day’s shape.",
        "Press BOOT for a spoken brief when supported.",
      ],
      interactions: "Swipe left or right to navigate. Tap centers the weather question.",
    },
    {
      slug: "quotes",
      title: "Quotes",
      kicker: "A sentence to think with",
      image: "../assets/faces/bench-17-quotes.png",
      summary:
        "A quote-of-the-day face that turns a small passage into an object of attention.",
      inquiry: "What sentence is worth carrying today?",
      questions: [
        "What phrase changes how I see the next hour?",
        "Which faculty voice does this quote invite?",
        "What would I ask if this sentence were the doorway?",
      ],
      use: [
        "Read slowly rather than scrolling for more.",
        "Let the quote seed a spoken question.",
        "Use the face as a daily commonplace prompt.",
      ],
      interactions: "Tap to refresh or focus when supported. Swipe left or right to navigate.",
    },
    {
      slug: "transits",
      title: "Live Transits",
      kicker: "Now and next in the sky",
      image: "../assets/faces/bench-18-transits.png",
      summary:
        "A live transit face focused on present sky motion and the next Moon sign ingress.",
      inquiry: "What is moving now, and what changes next?",
      questions: [
        "What is the current sky emphasis?",
        "When does the Moon shift tone?",
        "What timing question should I ask before the next ingress?",
      ],
      use: [
        "Read the paired celestial spheres as now-and-next.",
        "Use it when the full transit wheel is more detail than you need.",
        "Ask Castalia about timing, transition, and tone.",
      ],
      interactions: "Tap to focus the current transit. Swipe left or right to navigate.",
    },
    {
      slug: "tarot",
      title: "Tarot",
      kicker: "A daily card as inquiry",
      image: "../assets/faces/bench-19-tarot.png",
      summary:
        "A Major Arcana face for using symbolic ambiguity to ask a better question.",
      inquiry: "What archetype is speaking to the question today?",
      questions: [
        "What card appears when I stop and ask?",
        "What does this symbol illuminate or complicate?",
        "What would change if I treated the card as a mirror?",
      ],
      use: [
        "Tap for the day’s card or a focused draw.",
        "Swipe through the deck when browsing is useful.",
        "Ask aloud for interpretation only after forming your own read.",
      ],
      interactions: "Tap the center to draw or focus. Swipe up and down browses cards.",
    },
    {
      slug: "notes",
      title: "Notes",
      kicker: "Commonplace capture without a phone",
      image: "../assets/faces/bench-20-notes.png",
      summary:
        "An offline-first voice notes face for catching thoughts and queueing them for Commonplace.",
      inquiry: "What thought should be preserved before the day moves on?",
      questions: [
        "What did I just notice?",
        "What belongs in the commonplace, not in a chat thread?",
        "Can I capture the thought without opening a larger device?",
      ],
      use: [
        "Hold PWR to record a note.",
        "Let the watch queue notes while offline.",
        "Return later to richer reflection in Castalia or Commonplace.",
      ],
      interactions: "PWR hold records. Tap checks the note surface. Swipe left or right navigates.",
    },
    {
      slug: "ocarina",
      title: "Ocarina",
      kicker: "A small playable wind shape",
      image: "../assets/faces/bench-21-ocarina.png",
      summary:
        "A touch-playable ocarina face for turning the watch into a tiny musical object.",
      inquiry: "What happens if the answer starts as a tone instead of a sentence?",
      questions: [
        "Which note wants to start the phrase?",
        "How does a key change alter the feeling?",
        "Can play loosen the question before language arrives?",
      ],
      use: [
        "Tap holes to play notes.",
        "Swipe up or down to change key.",
        "Use it for short musical gestures, not a full composition workspace.",
      ],
      interactions: "Tap note holes. Swipe up or down changes key. Swipe left exits.",
    },
    {
      slug: "bongo",
      title: "Bongo",
      kicker: "Rhythm under the fingers",
      image: "../assets/faces/bench-22-bongo.png",
      summary:
        "A touch drum face where position changes pitch, built for quick embodied rhythm.",
      inquiry: "What rhythm brings the body back into the question?",
      questions: [
        "Is the moment asking for pulse before analysis?",
        "What pattern settles or energizes attention?",
        "How does touch change the answer?",
      ],
      use: [
        "Tap near the center for lower tones.",
        "Tap nearer the rim for higher tones.",
        "Use it as a playful reset between inquiry surfaces.",
      ],
      interactions: "Tap left or right drum zones. Swipe left exits.",
    },
    {
      slug: "piano",
      title: "Piano",
      kicker: "One octave around the circle",
      image: "../assets/faces/bench-23-piano.png",
      summary:
        "A circular one-octave piano with white keys outside and black keys inside.",
      inquiry: "What melody appears when the circle becomes a keyboard?",
      questions: [
        "Which interval changes the mood?",
        "Can I sketch a phrase in a few touches?",
        "What musical answer arrives before explanation?",
      ],
      use: [
        "Tap outer keys for white notes.",
        "Tap inner keys for black notes.",
        "Keep the interaction immediate and tactile.",
      ],
      interactions: "Tap keys around the ring. Swipe left exits.",
    },
    {
      slug: "level",
      title: "Level",
      kicker: "Orientation as feedback",
      image: "../assets/faces/bench-24-level.png",
      summary:
        "An IMU bubble level that makes tilt visible and can speak a correction.",
      inquiry: "Am I aligned with the surface, or visibly off center?",
      questions: [
        "Which direction is high?",
        "How far from level am I?",
        "Can the instrument guide the correction without a separate tool?",
      ],
      use: [
        "Set the watch on or against a surface.",
        "Read the bubble as top-of-display-forward orientation.",
        "Press BOOT for spoken correction when supported.",
      ],
      interactions: "Tilt the device. BOOT speaks the correction. Swipe left or right navigates.",
    },
    {
      slug: "tuning",
      title: "Tuning",
      kicker: "Pitch as a moving staff",
      summary:
        "A live microphone tuner where detected notes move across a treble staff.",
      inquiry: "What note is actually sounding?",
      questions: [
        "Is the instrument sharp, flat, or centered?",
        "Which pitch is the room hearing?",
        "What adjustment gets me closer?",
      ],
      use: [
        "Use the microphone to detect live pitch.",
        "Watch the note move across the staff.",
        "Treat it as a utility face; voice input is intentionally disabled here.",
      ],
      interactions: "Play or sing into the microphone. Swipe left or right to leave the face.",
    },
    {
      slug: "pandrum",
      title: "Pan Drum",
      kicker: "Fourteen notes in a round instrument",
      image: "../assets/faces/bench-25-pandrum.png",
      summary:
        "A playable handpan-style face that maps notes to touch regions on the circular display.",
      inquiry: "What pattern emerges from a round instrument under one thumb?",
      questions: [
        "Which note wants to answer the last note?",
        "Can a small phrase reset attention?",
        "What if inquiry begins as play?",
      ],
      use: [
        "Tap note zones across the face.",
        "Use the center and upper regions for different tones.",
        "Keep phrases short enough to remain tactile.",
      ],
      interactions: "Tap note regions around the pan. Swipe left exits.",
    },
    {
      slug: "alethiometer",
      title: "Alethiometer",
      kicker: "A compass for symbolic questions",
      summary:
        "A golden compass face with 36 symbols, three question needles, and one answer needle.",
      inquiry: "What symbols frame the question before an answer appears?",
      questions: [
        "Which three symbols define the question?",
        "What does the answer needle point toward?",
        "What do I need to interpret rather than merely receive?",
      ],
      use: [
        "Form a question before touching the answer state.",
        "Read the needles as a symbolic composition.",
        "Ask Castalia only after you have noticed your own associations.",
      ],
      interactions: "Tap to set or advance the compass reading when supported. Swipe left or right to navigate.",
    },
    {
      slug: "runes",
      title: "Runes",
      kicker: "Past, present, future",
      summary:
        "A three-rune spread for short symbolic reflection and spoken fortune prompts.",
      inquiry: "What changes when the question is read as past, present, and future?",
      questions: [
        "What is behind the question?",
        "What is active now?",
        "What direction is beginning to open?",
      ],
      use: [
        "Tap to cast a three-rune spread.",
        "Read each position before asking for interpretation.",
        "Use it as a prompt for reflection, not prediction certainty.",
      ],
      interactions: "Tap casts the spread and can trigger a spoken fortune. Swipe left or right navigates.",
    },
  ];

  const variantBySlug = {
    classic: "pocket",
    digital: "pocket",
    calcifer: "pocket",
    apocalypso: "pocket",
    weather: "pocket",

    moon: "lunasay",
    astro: "lunasay",
    transits: "lunasay",
    synastry: "lunasay",
    cycle: "lunasay",
    tarot: "lunasay",

    spotify: "ocarina",
    spectrum: "ocarina",
    tuning: "ocarina",
    ocarina: "ocarina",
    bongo: "ocarina",
    piano: "ocarina",
    pandrum: "ocarina",

    faculty: "cameo",
    quotes: "cameo",
    notes: "cameo",

    chakra: "enso",
    bowl: "enso",
    runes: "enso",

    radar: "luopan",
    level: "luopan",
    rocket: "luopan",
    alethiometer: "luopan",

    settings: "core",
    castalia: "core",
  };

  window.astrolabeFaces.forEach((face) => {
    face.variant = variantBySlug[face.slug];
  });
})();
