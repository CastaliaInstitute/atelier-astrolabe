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
})();
