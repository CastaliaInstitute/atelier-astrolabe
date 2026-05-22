(function () {
  const faces = document.querySelectorAll("[data-face]");
  const pills = document.querySelectorAll("[data-face-pill]");
  const label = document.querySelector("[data-face-label]");

  const names = {
    analog: "Classic analog",
    moon: "Moon",
    astro: "Transits",
    digital: "Digital",
  };

  function setFace(id) {
    faces.forEach((el) => el.classList.toggle("is-active", el.dataset.face === id));
    pills.forEach((el) => el.classList.toggle("is-active", el.dataset.facePill === id));
    if (label && names[id]) label.textContent = names[id];
  }

  pills.forEach((pill) => {
    pill.addEventListener("click", () => setFace(pill.dataset.facePill));
  });

  if (pills.length) setFace("moon");

  const faceDocs = window.astrolabeFaces || [];
  const variants = window.astrolabeVariants || [];

  function escapeHtml(value) {
    return String(value)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function renderList(items) {
    return items.map((item) => `<li>${escapeHtml(item)}</li>`).join("");
  }

  function renderFaceVisual(face, className) {
    if (face.image) {
      return `
        <div class="${className}">
          <img src="${face.image}" alt="${escapeHtml(face.title)} face screenshot" loading="lazy" />
        </div>
      `;
    }

    return `
      <div class="${className} face-capture--missing" aria-label="${escapeHtml(face.title)} screenshot pending">
        <span>${escapeHtml(face.title)}</span>
        <small>Screenshot pending</small>
      </div>
    `;
  }

  function sortedFacesForVariants() {
    const knownVariantIds = new Set(variants.map((variant) => variant.id));
    const grouped = variants.flatMap((variant) =>
      faceDocs.filter((face) => face.variant === variant.id)
    );
    return grouped.concat(faceDocs.filter((face) => !knownVariantIds.has(face.variant)));
  }

  function faceGesture(face, direction) {
    const gestures = {
      classic: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      apocalypso: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      digital: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      spotify: { up: "Music control: move upward through the active control state.", down: "Music control: move downward through the active control state." },
      astro: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      moon: { up: "Moon face gesture: move through moon-focused modes.", down: "Moon face gesture: return through moon-focused modes." },
      calcifer: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      cycle: { up: "Cycle face gesture: increase the cycle-length preset.", down: "Cycle face gesture: decrease the cycle-length preset." },
      castalia: { up: "Settings gesture: return from the settings surface.", down: "Settings gesture: move deeper into setup when available." },
      settings: { up: "Settings gesture: return from Settings.", down: "Settings gesture: open or move deeper into setup." },
      synastry: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      spectrum: { up: "Spectrum gesture: switch visualization mode upward.", down: "Spectrum gesture: switch visualization mode downward." },
      chakra: { up: "Chakra gesture: move to the next center.", down: "Chakra gesture: move to the previous center." },
      bowl: { up: "Bowl gesture: move to the next preset.", down: "Bowl gesture: move to the previous preset." },
      rocket: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      radar: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      faculty: { up: "Faculty gesture: cycle to the next recent faculty.", down: "Faculty gesture: cycle to the previous recent faculty." },
      weather: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      quotes: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      transits: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      tarot: { up: "Tarot gesture: browse upward through the deck.", down: "Tarot gesture: browse downward through the deck." },
      notes: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      ocarina: { up: "Ocarina gesture: raise the key.", down: "Ocarina gesture: lower the key." },
      bongo: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      piano: { up: "No vertical gesture on this face.", down: "No vertical gesture on this face." },
      level: { up: "Level gesture: tilt is read by the sensor rather than swipe.", down: "Level gesture: tilt is read by the sensor rather than swipe." },
      tuning: { up: "Tuning gesture: listen to pitch; vertical swipe is not used.", down: "Tuning gesture: listen to pitch; vertical swipe is not used." },
      pandrum: { up: "Pan drum gesture: play the upper note fields by touch.", down: "Pan drum gesture: play the lower note fields by touch." },
      alethiometer: { up: "Alethiometer gesture: symbolic reading remains on the compass.", down: "Alethiometer gesture: symbolic reading remains on the compass." },
      runes: { up: "Runes gesture: cast and read the spread by touch.", down: "Runes gesture: cast and read the spread by touch." },
    };

    return gestures[face.slug]?.[direction] || `Device gesture: swipe ${direction}.`;
  }

  function renderFaceDevice(face, previousFace, nextFace) {
    const prevHref = `${previousFace.slug}.html`;
    const nextHref = `${nextFace.slug}.html`;

    return `
      <div class="face-device-panel">
        <div
          class="face-device"
          data-face-device
          data-prev="${escapeHtml(prevHref)}"
          data-next="${escapeHtml(nextHref)}"
          aria-label="${escapeHtml(face.title)} device preview. Swipe left or right to change faces."
          role="img"
          tabindex="0"
        >
          <span class="face-device__crown face-device__crown--left"></span>
          <span class="face-device__crown face-device__crown--right"></span>
          <div class="face-device__case">
            ${renderFaceVisual(face, "face-capture face-capture--large face-device__screen")}
            <div class="face-device__gesture" data-gesture-status>Swipe on the device</div>
          </div>
        </div>
        <div class="face-device-nav" aria-label="Face navigation">
          <a href="${escapeHtml(prevHref)}">Previous: ${escapeHtml(previousFace.title)}</a>
          <a href="${escapeHtml(nextHref)}">Next: ${escapeHtml(nextFace.title)}</a>
        </div>
        <p class="face-device-note" data-gesture-note>
          Swipe left or right to change face. Swipe up or down to mirror the device gesture here.
        </p>
      </div>
    `;
  }

  function initFaceDevice(face) {
    const device = document.querySelector("[data-face-device]");
    if (!device) return;

    const status = document.querySelector("[data-gesture-status]");
    const note = document.querySelector("[data-gesture-note]");
    let startX = 0;
    let startY = 0;

    function showGesture(direction) {
      device.classList.remove("is-swipe-left", "is-swipe-right", "is-swipe-up", "is-swipe-down");
      void device.offsetWidth;
      if (direction !== "tap") {
        device.classList.add(`is-swipe-${direction}`);
      }

      const label = direction === "tap" ? "Tap" : `Swipe ${direction}`;
      if (status) status.textContent = label;
      if (note) note.textContent = faceGesture(face, direction);
    }

    function navigate(direction) {
      window.setTimeout(() => {
        window.location.href = direction === "left" ? device.dataset.next : device.dataset.prev;
      }, 140);
    }

    function finishGesture(endX, endY) {
      const dx = endX - startX;
      const dy = endY - startY;
      if (Math.max(Math.abs(dx), Math.abs(dy)) < 36) {
        showGesture("tap");
        if (note) note.textContent = "Tap mirrors the on-device center touch.";
        return;
      }

      if (Math.abs(dx) > Math.abs(dy)) {
        const direction = dx < 0 ? "left" : "right";
        showGesture(direction);
        navigate(direction);
        return;
      }

      showGesture(dy < 0 ? "up" : "down");
    }

    device.addEventListener("pointerdown", (event) => {
      startX = event.clientX;
      startY = event.clientY;
      device.setPointerCapture?.(event.pointerId);
    });

    device.addEventListener("pointerup", (event) => {
      finishGesture(event.clientX, event.clientY);
    });

    device.addEventListener("keydown", (event) => {
      const keyDirections = {
        ArrowLeft: "right",
        ArrowRight: "left",
        ArrowUp: "up",
        ArrowDown: "down",
      };
      const direction = keyDirections[event.key];
      if (!direction) return;
      event.preventDefault();
      showGesture(direction);
      if (direction === "left" || direction === "right") {
        navigate(direction);
      }
    });
  }

  const index = document.querySelector("[data-faces-index]");
  if (index && faceDocs.length) {
    const knownVariantIds = new Set(variants.map((variant) => variant.id));
    const ungroupedFaces = faceDocs.filter((face) => !knownVariantIds.has(face.variant));
    const visibleVariants = ungroupedFaces.length
      ? variants.concat([
          {
            id: "unsorted",
            title: "Unsorted",
            subtitle: "Needs edition assignment",
            promise: "Review before publishing",
            accessory: "These faces need an explicit edition or core-firmware home.",
          },
        ])
      : variants;

    index.innerHTML = visibleVariants
      .map((variant) => {
        const variantFaces =
          variant.id === "unsorted"
            ? ungroupedFaces
            : faceDocs.filter((face) => face.variant === variant.id);
        if (!variantFaces.length) return "";

        return `
          <section class="variant-section" aria-labelledby="variant-${escapeHtml(variant.id)}">
            <div class="variant-section__header">
              <p class="eyebrow">${escapeHtml(variant.subtitle)}</p>
              <h2 id="variant-${escapeHtml(variant.id)}">${escapeHtml(variant.title)}</h2>
              <p>${escapeHtml(variant.promise)}</p>
              <p class="variant-section__meta">${escapeHtml(variant.accessory)}</p>
            </div>
            <div class="variant-section__faces">
              ${variantFaces
                .map(
                  (face) => `
          <article class="face-card">
            <a class="face-card__image" href="${face.slug}.html" aria-label="${escapeHtml(face.title)} face">
              ${renderFaceVisual(face, "face-capture face-capture--small")}
            </a>
            <div class="face-card__body">
              <p class="eyebrow">${escapeHtml(face.kicker)}</p>
              <h3><a href="${face.slug}.html">${escapeHtml(face.title)}</a></h3>
              <p>${escapeHtml(face.summary)}</p>
              <p class="face-card__question">${escapeHtml(face.inquiry)}</p>
            </div>
          </article>
        `
                )
                .join("")}
            </div>
          </section>
        `;
      })
      .join("");
  }

  const pageSlug = document.body.dataset.facePage;
  if (pageSlug && faceDocs.length) {
    const orderedFaces = sortedFacesForVariants();
    const face = faceDocs.find((item) => item.slug === pageSlug);
    if (!face) return;
    const variant = variants.find((item) => item.id === face.variant);
    const currentIndex = orderedFaces.findIndex((item) => item.slug === face.slug);
    const previousFace =
      orderedFaces[(currentIndex - 1 + orderedFaces.length) % orderedFaces.length] || face;
    const nextFace = orderedFaces[(currentIndex + 1) % orderedFaces.length] || face;

    document.title = `${face.title} - Astrolabe Face`;
    document.body.innerHTML = `
      <div class="wrap">
        <nav class="nav" aria-label="Primary">
          <a class="nav__brand" href="../">Astrolabe</a>
          <div class="nav__links">
            <a href="./">Faces</a>
            <a href="../#how">How it works</a>
            <a href="../#founding">Founder Bundle</a>
          </div>
        </nav>
      </div>

      <main class="wrap face-page">
        <a class="back-link" href="./">All faces</a>
        <section class="face-page__hero">
          <div>
            <p class="eyebrow">${escapeHtml(face.kicker)}</p>
            <h1>${escapeHtml(face.title)}</h1>
            ${
              variant
                ? `<p class="variant-chip">${escapeHtml(variant.title)} default focus · ${escapeHtml(variant.promise)}</p>`
                : ""
            }
            <p class="lede">${escapeHtml(face.summary)}</p>
            <div class="inquiry-callout">
              <span>Inquiry</span>
              <p>${escapeHtml(face.inquiry)}</p>
            </div>
          </div>
          ${renderFaceDevice(face, previousFace, nextFace)}
        </section>

        <section class="face-detail-grid" aria-label="${escapeHtml(face.title)} details">
          <article>
            <h2>Questions Users Ask</h2>
            <ul>${renderList(face.questions)}</ul>
          </article>
          <article>
            <h2>How It Helps</h2>
            <ul>${renderList(face.use)}</ul>
          </article>
          <article>
            <h2>Interaction</h2>
            <p>${escapeHtml(face.interactions)}</p>
          </article>
        </section>
      </main>

      <footer class="footer wrap">
        <p class="footer__brand">Astrolabe</p>
        <p><a href="./">Faces</a> · <a href="../">Home</a> · <a href="https://github.com/CastaliaInstitute/astrolabe">GitHub</a></p>
      </footer>
    `;
    initFaceDevice(face);
  }
})();
