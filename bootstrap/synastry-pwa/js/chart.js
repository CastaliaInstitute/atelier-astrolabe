import { BODY_COUNT } from "./bodies.js";
import { BODIES } from "./bodies.js";
import { aspectColor, rebuildAspects } from "./synastry-math.js";
import { GLYPHS, norm360, zodiacGlyph } from "./zodiac.js";

const PI = Math.PI;
const TWO_PI = PI * 2;

function angleForLon(lon) {
  return PI + norm360(lon) * (PI / 180);
}

export class SynastryChart {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext("2d");
    this.userLon = [];
    this.targetLon = [];
    this.aspects = [];
    this.targetName = "";
    this.resize();
  }

  resize() {
    const rect = this.canvas.parentElement.getBoundingClientRect();
    const size = Math.min(rect.width, rect.height, 466);
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.canvas.width = size * dpr;
    this.canvas.height = size * dpr;
    this.canvas.style.width = `${size}px`;
    this.canvas.style.height = `${size}px`;
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.size = size;
    this.cx = size / 2;
    this.cy = size / 2;
    this.R = size / 2;
  }

  setData(userLon, targetLon, targetName) {
    this.userLon = userLon;
    this.targetLon = targetLon;
    this.targetName = targetName || "";
    this.aspects = rebuildAspects(userLon, targetLon);
    this.draw();
    return this.aspects;
  }

  draw() {
    const { ctx, cx, cy, R, size } = this;
    ctx.clearRect(0, 0, size, size);

    const rOuter = R - 12;
    const rTarget = rOuter - 42;
    const rUser = rOuter - 82;
    const rInner = rUser - 24;

    ctx.fillStyle = "#0a0c16";
    ctx.beginPath();
    ctx.arc(cx, cy, R - 2, 0, TWO_PI);
    ctx.fill();

    ctx.strokeStyle = "#3e485c";
    ctx.lineWidth = 1;
    for (let s = 0; s < 12; s++) {
      const a = s * (TWO_PI / 12) - PI * 0.5;
      const x0 = cx + Math.cos(a) * rInner;
      const y0 = cy + Math.sin(a) * rInner;
      const x1 = cx + Math.cos(a) * rOuter;
      const y1 = cy + Math.sin(a) * rOuter;
      ctx.beginPath();
      ctx.moveTo(x0, y0);
      ctx.lineTo(x1, y1);
      ctx.stroke();
    }

    for (const r of [rOuter, rTarget + 18, rUser + 18, rInner]) {
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, TWO_PI);
      ctx.stroke();
    }

    const drawLines = Math.min(this.aspects.length, 7);
    for (let i = 0; i < drawLines; i++) {
      const a = this.aspects[i];
      const au = angleForLon(this.userLon[a.userBody]);
      const at = angleForLon(this.targetLon[a.targetBody]);
      ctx.strokeStyle = aspectColor(a.aspectDeg);
      ctx.globalAlpha = 0.85;
      ctx.beginPath();
      ctx.moveTo(cx + Math.cos(au) * rUser, cy + Math.sin(au) * rUser);
      ctx.lineTo(cx + Math.cos(at) * rTarget, cy + Math.sin(at) * rTarget);
      ctx.stroke();
      ctx.globalAlpha = 1;
    }

    this.drawBodyRing(rTarget, this.targetLon, true);
    this.drawBodyRing(rUser, this.userLon, false);
    this.drawSignRing(rOuter - 22);
  }

  drawBodyRing(r, lons, isTarget) {
    const { ctx, cx, cy } = this;
    for (let bi = 0; bi < BODY_COUNT; bi++) {
      const ang = angleForLon(lons[bi]);
      const x = cx + Math.cos(ang) * r;
      const y = cy + Math.sin(ang) * r;
      let color = isTarget ? "#ebd0ff" : "#87d7ff";
      if (bi === 0) color = isTarget ? "#ffdc6e" : "#73cdff";
      ctx.fillStyle = color;
      ctx.font = "bold 11px system-ui, sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(BODIES[bi].label, x, y);
    }
  }

  drawSignRing(r) {
    const { ctx, cx, cy } = this;
    for (let s = 0; s < 12; s++) {
      const lon = s * 30 + 15;
      const ang = angleForLon(lon);
      const x = cx + Math.cos(ang) * r;
      const y = cy + Math.sin(ang) * r;
      ctx.fillStyle = "#aab4cd";
      ctx.font = "12px system-ui, sans-serif";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(GLYPHS[s], x, y);
    }
  }

  topAspectLine() {
    if (!this.aspects.length) {
      return `dual wheel: you + ${this.targetName}`;
    }
    const a = this.aspects[0];
    return `you ${BODIES[a.userBody].label} ${a.label} ${BODIES[a.targetBody].label} ${a.orb.toFixed(1)}`;
  }
}
