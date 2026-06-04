import "jsr:@supabase/functions-js/edge-runtime.d.ts";
import { Image } from "https://deno.land/x/imagescript@1.2.15/mod.ts";

import {
  facultyBustPathCandidates,
  resolveFacultyBustView,
  resolveFacultySlugFromSearchParams,
  signedFacultyBustUrl,
  updateFacultyLineBustPath,
  uploadFacultyLineBustPng,
} from "../_shared/facultyBust.ts";

const corsHeaders = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers":
    "authorization, x-client-info, apikey, content-type",
  "Access-Control-Allow-Methods": "GET, OPTIONS",
};

/** Transparent RGBA — letterbox stays see-through on AMOLED clients. */
const TRANSPARENT_RGBA = 0x00000000;
const PAPER_BLUE = { r: 100, g: 64, b: 255 };

/** Matches Atom wand UI fill rgb(0,0,0) — JPEG letterbox only. */
const WAND_UI_BG = 0x000000ff;

function clampInt(
  value: string | null,
  fallback: number,
  lo: number,
  hi: number,
): number {
  const n = Number(value ?? "");
  if (!Number.isFinite(n)) return fallback;
  return Math.max(lo, Math.min(hi, Math.round(n)));
}

function clampFloat(
  value: string | null,
  fallback: number,
  lo: number,
  hi: number,
): number {
  if (value === null || value.trim() === "") return fallback;
  const n = Number(value ?? "");
  if (!Number.isFinite(n)) return fallback;
  return Math.max(lo, Math.min(hi, n));
}

/**
 * Castalia faculty avatar sprites are 128×128 (or 256×256) PNG sheets: four bust
 * variants in a 2×2 grid. The side-profile portrait used on wand/watch is lower-left.
 * Storage may contain these sheets as `bust.png`; extract one cell before resize.
 */
function extractSpritePortrait(image: Image): boolean {
  const w = image.width;
  const h = image.height;
  if (w === 128 && h === 128) {
    image.crop(0, 64, 64, 64);
    return true;
  }
  if (w === 256 && h === 256) {
    image.crop(0, 128, 128, 128);
    return true;
  }
  return false;
}

function transparentPixelCount(image: Image): number {
  const { width, height, bitmap } = image;
  let count = 0;
  for (let p = 0; p < width * height; p++) {
    if (bitmap[p * 4 + 3] < 128) count++;
  }
  return count;
}

function isNeutralCheckerPixel(
  r: number,
  g: number,
  b: number,
  a: number,
): boolean {
  if (a < 128) return true;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  return max >= 96 && max - min <= 32;
}

function isDarkCheckerPixel(
  r: number,
  g: number,
  b: number,
  a: number,
): boolean {
  if (a < 128) return true;
  return r <= 74 && g <= 74 && b <= 74;
}

function isCheckerBackgroundPixel(
  r: number,
  g: number,
  b: number,
  a: number,
): boolean {
  return isNeutralCheckerPixel(r, g, b, a) ||
    isDarkCheckerPixel(r, g, b, a);
}

function isRestorableLightCheckerPixel(
  r: number,
  g: number,
  b: number,
  a: number,
): boolean {
  if (a < 128) return false;
  const max = Math.max(r, g, b);
  const min = Math.min(r, g, b);
  return max >= 176 && max - min <= 24;
}

function removeBorderCheckerboard(image: Image): number {
  const { width, height, bitmap } = image;
  if (width <= 0 || height <= 0) return 0;

  const seen = new Uint8Array(width * height);
  const removedMask = new Uint8Array(width * height);
  const restorableMask = new Uint8Array(width * height);
  const original = bitmap.slice();
  const stack: number[] = [];
  const pushIfBackground = (x: number, y: number) => {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const p = y * width + x;
    if (seen[p]) return;
    const i = p * 4;
    if (
      !isCheckerBackgroundPixel(
        bitmap[i],
        bitmap[i + 1],
        bitmap[i + 2],
        bitmap[i + 3],
      )
    ) return;
    seen[p] = 1;
    stack.push(p);
  };

  for (let x = 0; x < width; x++) {
    pushIfBackground(x, 0);
    pushIfBackground(x, height - 1);
  }
  for (let y = 1; y < height - 1; y++) {
    pushIfBackground(0, y);
    pushIfBackground(width - 1, y);
  }

  let removed = 0;
  while (stack.length > 0) {
    const p = stack.pop()!;
    const i = p * 4;
    bitmap[i] = 0;
    bitmap[i + 1] = 0;
    bitmap[i + 2] = 0;
    bitmap[i + 3] = 0;
    removedMask[p] = 1;
    restorableMask[p] = isRestorableLightCheckerPixel(
      original[i],
      original[i + 1],
      original[i + 2],
      original[i + 3],
    ) ? 1 : 0;
    removed++;

    const x = p % width;
    const y = Math.floor(p / width);
    pushIfBackground(x - 1, y);
    pushIfBackground(x + 1, y);
    pushIfBackground(x, y - 1);
    pushIfBackground(x, y + 1);
  }

  let restored = 0;
  const restoreMask = new Uint8Array(width * height);
  const isForeground = (p: number): boolean => {
    const i = p * 4;
    return bitmap[i + 3] >= 128 &&
      !isCheckerBackgroundPixel(
        bitmap[i],
        bitmap[i + 1],
        bitmap[i + 2],
        bitmap[i + 3],
      );
  };

  for (let pass = 0; pass < 10; pass++) {
    let passRestored = 0;
    for (let p = 0; p < removedMask.length; p++) {
      if (!removedMask[p] || !restorableMask[p] || restoreMask[p]) continue;
      const x = p % width;
      const y = Math.floor(p / width);
      let touchesForeground = false;
      for (let dy = -1; dy <= 1 && !touchesForeground; dy++) {
        const yy = y + dy;
        if (yy < 0 || yy >= height) continue;
        for (let dx = -1; dx <= 1; dx++) {
          if (dx === 0 && dy === 0) continue;
          const xx = x + dx;
          if (xx < 0 || xx >= width) continue;
          const q = yy * width + xx;
          if (restoreMask[q] || isForeground(q)) {
            touchesForeground = true;
            break;
          }
        }
      }
      if (!touchesForeground) continue;
      restoreMask[p] = 1;
      passRestored++;
    }
    if (passRestored === 0) break;
  }

  for (let p = 0; p < restoreMask.length; p++) {
    if (!restoreMask[p]) continue;
    const i = p * 4;
    bitmap[i] = original[i];
    bitmap[i + 1] = original[i + 1];
    bitmap[i + 2] = original[i + 2];
    bitmap[i + 3] = original[i + 3];
    restored++;
  }

  return removed - restored;
}

function mirrorHorizontal(image: Image): void {
  const { width, height, bitmap } = image;
  if (width <= 1 || height <= 0) return;
  const tmp = new Uint8Array(4);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < Math.floor(width / 2); x++) {
      const left = (y * width + x) * 4;
      const right = (y * width + (width - 1 - x)) * 4;
      tmp[0] = bitmap[left];
      tmp[1] = bitmap[left + 1];
      tmp[2] = bitmap[left + 2];
      tmp[3] = bitmap[left + 3];
      bitmap[left] = bitmap[right];
      bitmap[left + 1] = bitmap[right + 1];
      bitmap[left + 2] = bitmap[right + 2];
      bitmap[left + 3] = bitmap[right + 3];
      bitmap[right] = tmp[0];
      bitmap[right + 1] = tmp[1];
      bitmap[right + 2] = tmp[2];
      bitmap[right + 3] = tmp[3];
    }
  }
}

function rotateCanvas(image: Image, degrees: number): void {
  const { width, height, bitmap } = image;
  if (width <= 1 || height <= 1 || Math.abs(degrees) < 0.01) return;

  const src = bitmap.slice();
  const radians = degrees * Math.PI / 180;
  const cos = Math.cos(radians);
  const sin = Math.sin(radians);
  const cx = (width - 1) / 2;
  const cy = (height - 1) / 2;

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const dx = x - cx;
      const dy = y - cy;
      const sx = cx + dx * cos - dy * sin;
      const sy = cy + dx * sin + dy * cos;
      const di = (y * width + x) * 4;

      if (sx < 0 || sx >= width - 1 || sy < 0 || sy >= height - 1) {
        bitmap[di] = 0;
        bitmap[di + 1] = 0;
        bitmap[di + 2] = 0;
        bitmap[di + 3] = 0;
        continue;
      }

      const x0 = Math.floor(sx);
      const y0 = Math.floor(sy);
      const fx = sx - x0;
      const fy = sy - y0;
      const w00 = (1 - fx) * (1 - fy);
      const w10 = fx * (1 - fy);
      const w01 = (1 - fx) * fy;
      const w11 = fx * fy;
      const i00 = (y0 * width + x0) * 4;
      const i10 = i00 + 4;
      const i01 = i00 + width * 4;
      const i11 = i01 + 4;

      for (let c = 0; c < 4; c++) {
        bitmap[di + c] = Math.round(
          src[i00 + c] * w00 +
            src[i10 + c] * w10 +
            src[i01 + c] * w01 +
            src[i11 + c] * w11,
        );
      }
    }
  }
}

function removeBorderPaperWhite(image: Image): number {
  const { width, height, bitmap } = image;
  if (width <= 0 || height <= 0) return 0;

  const seen = new Uint8Array(width * height);
  const stack: number[] = [];
  const isPaperWhite = (p: number): boolean => {
    const i = p * 4;
    const a = bitmap[i + 3];
    if (a < 128) return true;
    const r = bitmap[i];
    const g = bitmap[i + 1];
    const b = bitmap[i + 2];
    const max = Math.max(r, g, b);
    const min = Math.min(r, g, b);
    return max >= 235 && min >= 225 && max - min <= 32;
  };
  const push = (x: number, y: number) => {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const p = y * width + x;
    if (seen[p] || !isPaperWhite(p)) return;
    seen[p] = 1;
    stack.push(p);
  };

  for (let x = 0; x < width; x++) {
    push(x, 0);
    push(x, height - 1);
  }
  for (let y = 1; y < height - 1; y++) {
    push(0, y);
    push(width - 1, y);
  }

  let removed = 0;
  while (stack.length > 0) {
    const p = stack.pop()!;
    const i = p * 4;
    if (bitmap[i + 3] >= 128) {
      bitmap[i] = 0;
      bitmap[i + 1] = 0;
      bitmap[i + 2] = 0;
      bitmap[i + 3] = 0;
      removed++;
    }
    const x = p % width;
    const y = Math.floor(p / width);
    push(x - 1, y);
    push(x + 1, y);
    push(x, y - 1);
    push(x, y + 1);
  }
  return removed;
}

function removePeripheralOpaqueComponents(image: Image): number {
  const { width, height, bitmap } = image;
  if (width <= 0 || height <= 0) return 0;

  const count = width * height;
  const seen = new Uint8Array(count);
  const componentId = new Int32Array(count);
  componentId.fill(-1);
  const stack: number[] = [];
  const components: Array<{
    area: number;
    minX: number;
    minY: number;
    maxX: number;
    maxY: number;
  }> = [];

  const isOpaque = (p: number): boolean => bitmap[p * 4 + 3] >= 96;
  for (let p = 0; p < count; p++) {
    if (seen[p] || !isOpaque(p)) continue;
    const id = components.length;
    let area = 0;
    let minX = width;
    let minY = height;
    let maxX = -1;
    let maxY = -1;
    seen[p] = 1;
    stack.push(p);
    while (stack.length > 0) {
      const q = stack.pop()!;
      const x = q % width;
      const y = Math.floor(q / width);
      componentId[q] = id;
      area++;
      minX = Math.min(minX, x);
      minY = Math.min(minY, y);
      maxX = Math.max(maxX, x);
      maxY = Math.max(maxY, y);

      if (x > 0) {
        const n = q - 1;
        if (!seen[n] && isOpaque(n)) {
          seen[n] = 1;
          stack.push(n);
        }
      }
      if (x + 1 < width) {
        const n = q + 1;
        if (!seen[n] && isOpaque(n)) {
          seen[n] = 1;
          stack.push(n);
        }
      }
      if (y > 0) {
        const n = q - width;
        if (!seen[n] && isOpaque(n)) {
          seen[n] = 1;
          stack.push(n);
        }
      }
      if (y + 1 < height) {
        const n = q + width;
        if (!seen[n] && isOpaque(n)) {
          seen[n] = 1;
          stack.push(n);
        }
      }
    }
    components.push({ area, minX, minY, maxX, maxY });
  }

  if (components.length <= 1) return 0;
  let primary = 0;
  for (let i = 1; i < components.length; i++) {
    if (components[i].area > components[primary].area) primary = i;
  }
  if (components[primary].area < 500) return 0;

  const pad = 30;
  const keepMinX = Math.max(0, components[primary].minX - pad);
  const keepMinY = Math.max(0, components[primary].minY - pad);
  const keepMaxX = Math.min(width - 1, components[primary].maxX + pad);
  const keepMaxY = Math.min(height - 1, components[primary].maxY + pad);
  const keepComponent = (id: number): boolean => {
    if (id === primary) return true;
    const c = components[id];
    if (c.area < 500) return false;
    return c.maxX >= keepMinX && c.minX <= keepMaxX &&
      c.maxY >= keepMinY && c.minY <= keepMaxY;
  };

  let removed = 0;
  for (let p = 0; p < count; p++) {
    const id = componentId[p];
    if (id < 0 || keepComponent(id)) continue;
    const i = p * 4;
    bitmap[i] = 0;
    bitmap[i + 1] = 0;
    bitmap[i + 2] = 0;
    bitmap[i + 3] = 0;
    removed++;
  }
  return removed;
}

function removeBustPlinth(image: Image): number {
  const { width, height, bitmap } = image;
  if (width <= 0 || height <= 0) return 0;

  const startY = Math.floor(height * 0.68);
  const minOpaque = Math.max(8, Math.floor(width * 0.22));
  const minSpan = Math.max(10, Math.floor(width * 0.34));
  let cutY = -1;
  let runRows = 0;

  for (let y = height - 1; y >= startY; y--) {
    let opaque = 0;
    let minX = width;
    let maxX = -1;
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      const a = bitmap[p * 4 + 3];
      if (a < 96) continue;
      opaque++;
      minX = Math.min(minX, x);
      maxX = Math.max(maxX, x);
    }
    const span = maxX >= minX ? maxX - minX + 1 : 0;
    const baseLike = opaque >= minOpaque && span >= minSpan;
    if (baseLike) {
      cutY = y;
      runRows++;
      continue;
    }
    if (runRows >= 4) break;
    runRows = 0;
    cutY = -1;
  }

  if (cutY < 0) return 0;
  cutY = Math.max(cutY, Math.floor(height * 0.68));

  let removed = 0;
  for (let y = cutY; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      const i = p * 4;
      if (bitmap[i + 3] < 96) continue;
      bitmap[i] = 0;
      bitmap[i + 1] = 0;
      bitmap[i + 2] = 0;
      bitmap[i + 3] = 0;
      removed++;
    }
  }
  return removed;
}

function compositeBustOntoCanvas(
  canvas: Image,
  image: Image,
  width: number,
  height: number,
  fit: string,
): void {
  if (fit === "cover" || fit === "fill") {
    image.cover(width, height);
    canvas.composite(image, 0, 0);
    return;
  }
  image.fit(width, height);
  const x = Math.max(0, Math.floor((width - image.width) / 2));
  const y = Math.max(0, Math.floor((height - image.height) / 2));
  canvas.composite(image, x, y);
}

function luma(bitmap: Uint8Array | Uint8ClampedArray, p: number): number {
  const i = p * 4;
  return Math.round(
    bitmap[i] * 0.299 + bitmap[i + 1] * 0.587 + bitmap[i + 2] * 0.114,
  );
}

function normalizeLineCacheImage(image: Image): void {
  const { bitmap } = image;
  const count = image.width * image.height;
  for (let p = 0; p < count; p++) {
    const i = p * 4;
    const ink = bitmap[i + 3] >= 128 && luma(bitmap, p) < 128;
    bitmap[i] = ink ? 0 : 255;
    bitmap[i + 1] = ink ? 0 : 255;
    bitmap[i + 2] = ink ? 0 : 255;
    bitmap[i + 3] = 255;
  }
}

function applyPaperStencil(image: Image, glow: boolean): void {
  const { width, height, bitmap } = image;
  const count = width * height;
  const gray = new Uint8Array(count);
  const blur = new Uint8Array(count);
  const mask = new Uint8Array(count);
  const subject = new Uint8Array(count);

  let lo = 255;
  let hi = 0;
  for (let p = 0; p < count; p++) {
    const i = p * 4;
    subject[p] = bitmap[i + 3] >= 128 ? 1 : 0;
    const v = subject[p] ? luma(bitmap, p) : 255;
    gray[p] = v;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }

  const range = Math.max(1, hi - lo);
  for (let p = 0; p < count; p++) {
    gray[p] = Math.max(
      0,
      Math.min(255, Math.round(((gray[p] - lo) * 255) / range)),
    );
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let sum = 0;
      let n = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        if (yy < 0 || yy >= height) continue;
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx < 0 || xx >= width) continue;
          sum += gray[yy * width + xx];
          n++;
        }
      }
      blur[y * width + x] = Math.round(sum / n);
    }
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      const xl = Math.max(0, x - 1);
      const xr = Math.min(width - 1, x + 1);
      const yu = Math.max(0, y - 1);
      const yd = Math.min(height - 1, y + 1);
      const gradient = Math.abs(gray[y * width + xr] - gray[y * width + xl]) +
        Math.abs(gray[yd * width + x] - gray[yu * width + x]);
      mask[p] = blur[p] < 116 || (gradient > 44 && gray[p] < 225) ? 1 : 0;
    }
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      let neighbors = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        if (yy < 0 || yy >= height) continue;
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx < 0 || xx >= width) continue;
          neighbors += mask[yy * width + xx] ? 1 : 0;
        }
      }
      const ink = mask[p] ? neighbors >= 2 : neighbors >= 6;
      const i = p * 4;
      bitmap[i] = ink ? 0 : 255;
      bitmap[i + 1] = ink ? 0 : 255;
      bitmap[i + 2] = ink ? 0 : 255;
      bitmap[i + 3] = 255;
    }
  }

  if (!glow) return;

  const silhouette = new Uint8Array(subject);
  for (let y = 0; y < height; y++) {
    let minX = width;
    let maxX = -1;
    for (let x = 0; x < width; x++) {
      if (!subject[y * width + x]) continue;
      minX = Math.min(minX, x);
      maxX = Math.max(maxX, x);
    }
    if (maxX < minX) continue;
    for (let x = minX; x <= maxX; x++) {
      silhouette[y * width + x] = 1;
    }
  }

  const exterior = new Uint8Array(count);
  const stack: number[] = [];
  const pushExterior = (x: number, y: number) => {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    const p = y * width + x;
    if (exterior[p] || silhouette[p]) return;
    exterior[p] = 1;
    stack.push(p);
  };

  for (let x = 0; x < width; x++) {
    pushExterior(x, 0);
    pushExterior(x, height - 1);
  }
  for (let y = 1; y < height - 1; y++) {
    pushExterior(0, y);
    pushExterior(width - 1, y);
  }
  while (stack.length > 0) {
    const p = stack.pop()!;
    const x = p % width;
    const y = Math.floor(p / width);
    pushExterior(x - 1, y);
    pushExterior(x + 1, y);
    pushExterior(x, y - 1);
    pushExterior(x, y + 1);
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const p = y * width + x;
      if (silhouette[p] || !exterior[p]) continue;

      let nearSubject = false;
      for (let yy = y - 1; yy <= y + 1 && !nearSubject; yy++) {
        if (yy < 0 || yy >= height) continue;
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx < 0 || xx >= width) continue;
          if (silhouette[yy * width + xx]) {
            nearSubject = true;
            break;
          }
        }
      }
      if (!nearSubject) continue;

      const i = p * 4;
      bitmap[i] = PAPER_BLUE.r;
      bitmap[i + 1] = PAPER_BLUE.g;
      bitmap[i + 2] = PAPER_BLUE.b;
      bitmap[i + 3] = 255;
    }
  }
}

function applyPaperInk(image: Image): void {
  const { width, height, bitmap } = image;
  const count = width * height;
  const subject = new Uint8Array(count);
  const gray = new Uint8Array(count);
  const blur = new Uint8Array(count);
  const ink = new Uint8Array(count);

  let lo = 255;
  let hi = 0;
  for (let p = 0; p < count; p++) {
    const i = p * 4;
    subject[p] = bitmap[i + 3] >= 128 ? 1 : 0;
    const v = subject[p] ? luma(bitmap, p) : 255;
    gray[p] = v;
    if (subject[p]) {
      lo = Math.min(lo, v);
      hi = Math.max(hi, v);
    }
  }

  const range = Math.max(1, hi - lo);
  for (let p = 0; p < count; p++) {
    gray[p] = Math.max(
      0,
      Math.min(255, Math.round(((gray[p] - lo) * 255) / range)),
    );
  }

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let sum = 0;
      let n = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        if (yy < 0 || yy >= height) continue;
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx < 0 || xx >= width) continue;
          sum += gray[yy * width + xx];
          n++;
        }
      }
      blur[y * width + x] = Math.round(sum / n);
    }
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      if (!subject[p]) continue;

      const tl = blur[(y - 1) * width + x - 1];
      const tc = blur[(y - 1) * width + x];
      const tr = blur[(y - 1) * width + x + 1];
      const ml = blur[y * width + x - 1];
      const mr = blur[y * width + x + 1];
      const bl = blur[(y + 1) * width + x - 1];
      const bc = blur[(y + 1) * width + x];
      const br = blur[(y + 1) * width + x + 1];
      const gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
      const gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
      const edge = Math.abs(gx) + Math.abs(gy);
      const v = gray[p];
      const faceCore = x > width * 0.34 && x < width * 0.70 &&
        y > height * 0.18 && y < height * 0.58;

      const strongThreshold = faceCore ? 280 : 225;
      const midThreshold = faceCore ? 215 : 165;
      const valueThreshold = faceCore ? 95 : 120;
      const weakThreshold = faceCore ? 235 : 152;
      const weakValueThreshold = faceCore ? 95 : 140;
      const strong = edge > strongThreshold ||
        (edge > midThreshold && v < valueThreshold);
      const line = strong ||
        (edge > weakThreshold && v < weakValueThreshold);
      ink[p] = line ? (strong ? 2 : 1) : 0;
    }
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      if (!subject[p]) continue;
      if (
        subject[p - 1] && subject[p + 1] && subject[p - width] &&
        subject[p + width]
      ) continue;
      ink[p] = Math.max(ink[p], 3);
    }
  }

  for (let p = 0; p < count; p++) {
    const i = p * 4;
    bitmap[i] = 255;
    bitmap[i + 1] = 255;
    bitmap[i + 2] = 255;
    bitmap[i + 3] = 255;
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      if (!ink[p]) continue;
      let neighbors = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        for (let xx = x - 1; xx <= x + 1; xx++) {
          neighbors += ink[yy * width + xx] ? 1 : 0;
        }
      }
      if (ink[p] === 1 && neighbors > 4) continue;
      const i = p * 4;
      bitmap[i] = 0;
      bitmap[i + 1] = 0;
      bitmap[i + 2] = 0;
      if (ink[p] >= 3 && x + 1 < width) {
        const j = i + 4;
        bitmap[j] = 0;
        bitmap[j + 1] = 0;
        bitmap[j + 2] = 0;
      }
    }
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      const i = p * 4;
      if (bitmap[i] !== 0 || bitmap[i + 1] !== 0 || bitmap[i + 2] !== 0) {
        continue;
      }
      let neighbors = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx === x && yy === y) continue;
          const j = (yy * width + xx) * 4;
          if (bitmap[j] === 0 && bitmap[j + 1] === 0 && bitmap[j + 2] === 0) {
            neighbors++;
          }
        }
      }
      if (neighbors > 1) continue;
      bitmap[i] = 255;
      bitmap[i + 1] = 255;
      bitmap[i + 2] = 255;
    }
  }
}

function boxBlurGray(
  src: Uint8Array,
  dst: Uint8Array,
  width: number,
  height: number,
): void {
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      let sum = 0;
      let n = 0;
      for (let yy = y - 1; yy <= y + 1; yy++) {
        if (yy < 0 || yy >= height) continue;
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (xx < 0 || xx >= width) continue;
          sum += src[yy * width + xx];
          n++;
        }
      }
      dst[y * width + x] = Math.round(sum / n);
    }
  }
}

function applyPaperEtch(image: Image): void {
  const { width, height, bitmap } = image;
  const count = width * height;
  const subject = new Uint8Array(count);
  const gray = new Uint8Array(count);
  const smoothA = new Uint8Array(count);
  const smoothB = new Uint8Array(count);
  const edgeMap = new Uint16Array(count);
  const strongEdges = new Uint8Array(count);
  const ink = new Uint8Array(count);

  let lo = 255;
  let hi = 0;
  for (let p = 0; p < count; p++) {
    const i = p * 4;
    subject[p] = bitmap[i + 3] >= 128 ? 1 : 0;
    const v = subject[p] ? luma(bitmap, p) : 255;
    gray[p] = v;
    if (subject[p]) {
      lo = Math.min(lo, v);
      hi = Math.max(hi, v);
    }
  }

  const range = Math.max(1, hi - lo);
  for (let p = 0; p < count; p++) {
    gray[p] = subject[p]
      ? Math.max(0, Math.min(255, Math.round(((gray[p] - lo) * 255) / range)))
      : 255;
  }

  boxBlurGray(gray, smoothA, width, height);
  boxBlurGray(smoothA, smoothB, width, height);
  boxBlurGray(smoothB, smoothA, width, height);
  boxBlurGray(smoothA, smoothB, width, height);

  for (let y = 2; y < height - 2; y++) {
    for (let x = 2; x < width - 2; x++) {
      const p = y * width + x;
      if (!subject[p]) continue;
      const lowerBase = y > height * 0.84 && x > width * 0.20 &&
        x < width * 0.80;
      if (
        lowerBase && subject[p - 1] && subject[p + 1] && subject[p - width] &&
        subject[p + width]
      ) continue;

      const tl = smoothB[(y - 1) * width + x - 1];
      const tc = smoothB[(y - 1) * width + x];
      const tr = smoothB[(y - 1) * width + x + 1];
      const ml = smoothB[y * width + x - 1];
      const mr = smoothB[y * width + x + 1];
      const bl = smoothB[(y + 1) * width + x - 1];
      const bc = smoothB[(y + 1) * width + x];
      const br = smoothB[(y + 1) * width + x + 1];
      const gx = -tl - 2 * ml - bl + tr + 2 * mr + br;
      const gy = -tl - 2 * tc - tr + bl + 2 * bc + br;
      const edge = Math.abs(gx) + Math.abs(gy);
      edgeMap[p] = edge;
      const v = smoothB[p];
      const faceCore = x > width * 0.32 && x < width * 0.72 &&
        y > height * 0.15 && y < height * 0.64;
      const bustCore = y > height * 0.54;
      const shoulderNoise = y > height * 0.57 && x > width * 0.66;

      const high = faceCore ? 220 : 195;
      const low = faceCore ? 142 : 118;
      if (!shoulderNoise && edge > high && v < (faceCore ? 238 : 246)) {
        strongEdges[p] = 1;
      }
      if (!shoulderNoise && edge > low && v < (faceCore ? 214 : 230)) {
        ink[p] = 1;
      }
      if (bustCore && edge > 205 && v < 112) ink[p] = 1;
    }
  }

  for (let y = 2; y < height - 2; y++) {
    for (let x = 2; x < width - 2; x++) {
      const p = y * width + x;
      if (!ink[p] || strongEdges[p]) continue;
      let touchesStrong = false;
      for (let yy = y - 1; yy <= y + 1 && !touchesStrong; yy++) {
        for (let xx = x - 1; xx <= x + 1; xx++) {
          if (strongEdges[yy * width + xx]) {
            touchesStrong = true;
            break;
          }
        }
      }
      if (!touchesStrong) ink[p] = 0;
    }
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      if (!subject[p]) continue;
      if (
        subject[p - 1] && subject[p + 1] && subject[p - width] &&
        subject[p + width]
      ) continue;
      ink[p] = Math.max(ink[p], 2);
    }
  }

  for (let y = 2; y < height - 2; y++) {
    for (let x = 2; x < width - 2; x++) {
      const p = y * width + x;
      if (!subject[p]) continue;
      const v = smoothB[p];
      const faceAccent = x > width * 0.34 && x < width * 0.70 &&
        y > height * 0.24 && y < height * 0.62;
      const lineRhythm = (x + y * 3) % 19 === 0;
      if (faceAccent && v < 74 && edgeMap[p] > 86 && lineRhythm) {
        ink[p] = Math.max(ink[p], 1);
      }
    }
  }

  const cleaned = new Uint8Array(ink);
  for (let pass = 0; pass < 2; pass++) {
    for (let y = 1; y < height - 1; y++) {
      for (let x = 1; x < width - 1; x++) {
        const p = y * width + x;
        if (!ink[p]) continue;
        let neighbors = 0;
        for (let yy = y - 1; yy <= y + 1; yy++) {
          for (let xx = x - 1; xx <= x + 1; xx++) {
            if (xx === x && yy === y) continue;
            neighbors += ink[yy * width + xx] ? 1 : 0;
          }
        }
        if (neighbors <= (ink[p] >= 2 ? 0 : 1)) cleaned[p] = 0;
        if (ink[p] === 1 && neighbors > 6) cleaned[p] = 0;
      }
    }
    ink.set(cleaned);
  }

  for (let p = 0; p < count; p++) {
    const i = p * 4;
    bitmap[i] = 255;
    bitmap[i + 1] = 255;
    bitmap[i + 2] = 255;
    bitmap[i + 3] = 255;
  }

  for (let y = 1; y < height - 1; y++) {
    for (let x = 1; x < width - 1; x++) {
      const p = y * width + x;
      if (!ink[p]) continue;
      const i = p * 4;
      bitmap[i] = 0;
      bitmap[i + 1] = 0;
      bitmap[i + 2] = 0;
      if (ink[p] >= 3 && x + 1 < width && ink[p + 1]) {
        const j = i + 4;
        bitmap[j] = 0;
        bitmap[j + 1] = 0;
        bitmap[j + 2] = 0;
      }
    }
  }
}

async function resizeBustToPng(
  upstream: Response,
  width: number,
  height: number,
  fit: string,
  style: string,
  mirror: boolean,
  rotationDegrees: number,
  lineCache = false,
): Promise<
  { png: Uint8Array; spriteCell: string | null; transparentPixels: number }
> {
  const src = new Uint8Array(await upstream.arrayBuffer());
  const image = await Image.decode(src);
  const spriteCell = extractSpritePortrait(image) ? "lower-left" : null;
  if (lineCache) normalizeLineCacheImage(image);
  const sourceTransparentPixels = lineCache ? 0 : transparentPixelCount(image);
  const sourceHasAlpha = sourceTransparentPixels > (image.width * image.height) / 20;
  const transparentPixels = lineCache || sourceHasAlpha
    ? sourceTransparentPixels
    : removeBorderCheckerboard(image);
  const canvas = new Image(width, height);
  const paperStyle = style === "paper-stencil" ||
    style === "paper-stencil-glow" || style === "paper-ink" ||
    style === "paper-etch";
  canvas.fill(TRANSPARENT_RGBA);
  compositeBustOntoCanvas(canvas, image, width, height, fit);
  let canvasTransparentPixels = lineCache || sourceHasAlpha ? 0 : removeBorderCheckerboard(canvas);
  if (mirror) mirrorHorizontal(canvas);
  if (!lineCache && !sourceHasAlpha) canvasTransparentPixels += removeBorderPaperWhite(canvas);
  if (!lineCache && !sourceHasAlpha) canvasTransparentPixels += removePeripheralOpaqueComponents(canvas);
  rotateCanvas(canvas, rotationDegrees);
  if (!lineCache) canvasTransparentPixels += removeBustPlinth(canvas);
  if (!lineCache && !sourceHasAlpha) canvasTransparentPixels += removeBorderPaperWhite(canvas);
  if (style === "paper-etch") {
    applyPaperEtch(canvas);
  } else if (style === "paper-ink") {
    applyPaperInk(canvas);
  } else if (paperStyle) {
    applyPaperStencil(canvas, style === "paper-stencil-glow");
  }
  return {
    png: await canvas.encode(),
    spriteCell,
    transparentPixels: transparentPixels + canvasTransparentPixels,
  };
}

async function resizeBustToJpeg(
  upstream: Response,
  width: number,
  height: number,
  quality: number,
  fit: string,
  mirror: boolean,
  rotationDegrees: number,
): Promise<{ jpeg: Uint8Array; spriteCell: string | null }> {
  const src = new Uint8Array(await upstream.arrayBuffer());
  const image = await Image.decode(src);
  const spriteCell = extractSpritePortrait(image) ? "lower-left" : null;
  const canvas = new Image(width, height);
  canvas.fill(WAND_UI_BG);
  compositeBustOntoCanvas(canvas, image, width, height, fit);
  if (mirror) mirrorHorizontal(canvas);
  rotateCanvas(canvas, rotationDegrees);
  return { jpeg: await canvas.encodeJPEG(quality), spriteCell };
}

async function generateAndCacheLineBust(slug: string): Promise<
  {
    png: Uint8Array;
    slug: string;
    path: string;
    bucket: string;
    spriteCell: string | null;
    transparentPixels: number;
  }
> {
  const source = await signedFacultyBustUrl(slug, undefined, "right");
  if (!source) {
    throw new Error(`regular bust not found for lazy line bust: ${slug}`);
  }

  const upstream = await fetch(source.url, {
    headers: {
      Accept: "image/png,image/jpeg,image/webp,image/*;q=0.8,*/*;q=0.1",
    },
  });
  if (!upstream.ok || !upstream.body) {
    throw new Error(
      `regular bust fetch failed for lazy line bust: ${upstream.status}`,
    );
  }

  const { png, spriteCell, transparentPixels } = await resizeBustToPng(
    upstream,
    320,
    320,
    "cover",
    "paper-etch",
    false,
    0,
  );
  const uploaded = await uploadFacultyLineBustPng(slug, png);
  return {
    png,
    slug: source.slug,
    path: uploaded.path,
    bucket: uploaded.bucket,
    spriteCell,
    transparentPixels,
  };
}

function wantsJpeg(formatRaw: string, accept: string): boolean {
  const fmt = formatRaw.trim().toLowerCase();
  if (fmt === "jpeg" || fmt === "jpg") return true;
  if (fmt === "png") return false;
  if (accept.includes("image/png") && !accept.includes("image/jpeg")) {
    return false;
  }
  if (accept.includes("image/jpeg") && !accept.includes("image/png")) {
    return true;
  }
  return false;
}

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") {
    return new Response("ok", { headers: corsHeaders });
  }
  if (req.method !== "GET") {
    return Response.json({ error: "Use GET" }, {
      status: 405,
      headers: corsHeaders,
    });
  }

  const u = new URL(req.url);
  const rawParam = u.searchParams.get("faculty")?.trim() ||
    u.searchParams.get("handle")?.trim() ||
    u.searchParams.get("slug")?.trim() ||
    "";
  if (!rawParam) {
    return Response.json(
      {
        error: "faculty query parameter is required",
        hint:
          "Use ?faculty=a.einstein&w=128&h=128&format=png (handle= and slug= are aliases)",
      },
      { status: 400, headers: { ...corsHeaders, "Cache-Control": "no-store" } },
    );
  }

  const slug = resolveFacultySlugFromSearchParams(u.searchParams);
  const view = resolveFacultyBustView(u.searchParams);
  // Upper bound covers the largest client panel (Waveshare 1.75C round AMOLED, 466×466).
  const width = clampInt(
    u.searchParams.get("w") ?? u.searchParams.get("width"),
    192,
    48,
    512,
  );
  const height = clampInt(
    u.searchParams.get("h") ?? u.searchParams.get("height"),
    240,
    48,
    512,
  );
  const quality = clampInt(
    u.searchParams.get("q") ?? u.searchParams.get("quality"),
    72,
    35,
    90,
  );
  const resize = (u.searchParams.get("resize") ?? "contain").trim()
    .toLowerCase();
  const fit = resize === "cover" || resize === "fill" ? resize : "contain";
  const style = (u.searchParams.get("style") ?? "").trim().toLowerCase();
  const formatRaw = u.searchParams.get("format") ?? "png";
  const useJpeg = wantsJpeg(formatRaw, req.headers.get("Accept") ?? "");
  const facing = (u.searchParams.get("facing") ?? "").trim().toLowerCase();
  const mirrorForRightFacing =
    view === "right" && (facing === "" || facing === "right" || facing === "right-facing");
  const rotationDegrees = view === "right"
    ? clampFloat(u.searchParams.get("rotate"), 3, -8, 8)
    : 0;

  try {
    let signed = await signedFacultyBustUrl(slug, undefined, view);
    let generatedLineBust:
      | {
        png: Uint8Array;
        path: string;
        bucket: string;
        spriteCell: string | null;
        transparentPixels: number;
      }
      | null = null;
    if (view === "line" && signed?.slug !== slug) {
      signed = null;
    }
    if (!signed) {
      if (view === "line") {
        const generated = await generateAndCacheLineBust(slug);
        generatedLineBust = generated;
        signed = {
          url: "generated://line-bust",
          slug: generated.slug,
          path: generated.path,
          bucket: generated.bucket,
        };
      } else {
        const candidates = facultyBustPathCandidates(slug, view);
        return Response.json(
          {
            error: "faculty bust not found",
            faculty: slug,
            view,
            ...candidates,
          },
          {
            status: 404,
            headers: { ...corsHeaders, "Cache-Control": "no-store" },
          },
        );
      }
    } else if (view === "line") {
      await updateFacultyLineBustPath(slug, signed.path);
    }

    const upstream = generatedLineBust
      ? new Response(generatedLineBust.png.slice().buffer)
      : await fetch(signed.url, {
        headers: {
          Accept: "image/png,image/webp,image/jpeg,image/*;q=0.8,*/*;q=0.1",
        },
      });
    if (!generatedLineBust && (!upstream.ok || !upstream.body)) {
      return Response.json(
        {
          error: "faculty bust storage fetch failed",
          status: upstream.status,
          faculty: signed.slug,
        },
        {
          status: 502,
          headers: { ...corsHeaders, "Cache-Control": "no-store" },
        },
      );
    }

    const h = new Headers(corsHeaders);
    h.set(
      "Cache-Control",
      "public, max-age=86400, stale-while-revalidate=604800",
    );
    h.set("X-Faculty-Slug", signed.slug);
    h.set("X-Faculty-Bust-View", view);
    h.set("X-Faculty-Bust-Bucket", signed.bucket);
    h.set("X-Faculty-Bust-Path", signed.path);
    h.set("X-Faculty-Bust-Size", `${width}x${height}`);
    if (generatedLineBust) h.set("X-Faculty-Bust-Line-Cache", "generated");
    if (style) h.set("X-Faculty-Bust-Style", style);
    if (mirrorForRightFacing) h.set("X-Faculty-Bust-Facing", "right");
    if (rotationDegrees) {
      h.set("X-Faculty-Bust-Rotation-Degrees", String(rotationDegrees));
    }

    if (useJpeg) {
      const { jpeg, spriteCell } = await resizeBustToJpeg(
        upstream,
        width,
        height,
        quality,
        fit,
        mirrorForRightFacing,
        rotationDegrees,
      );
      h.set("Content-Type", "image/jpeg");
      h.set(
        "X-Faculty-Bust-Transform",
        spriteCell ? "sprite-crop-jpeg" : "imagescript-jpeg",
      );
      if (spriteCell) h.set("X-Faculty-Bust-Sprite-Cell", spriteCell);
      h.set("Content-Length", String(jpeg.byteLength));
      return new Response(jpeg.slice().buffer, { status: 200, headers: h });
    }

    const { png, spriteCell, transparentPixels } = await resizeBustToPng(
      upstream,
      width,
      height,
      fit,
      style,
      mirrorForRightFacing,
      rotationDegrees,
      view === "line" && !style,
    );
    h.set("Content-Type", "image/png");
    h.set(
      "X-Faculty-Bust-Transform",
      style === "paper-stencil-glow"
        ? (spriteCell
          ? "sprite-crop-paper-stencil-glow-png"
          : "paper-stencil-glow-png")
        : view === "line"
        ? (generatedLineBust
          ? "generated-line-bust-cache-png"
          : "line-bust-cache-png")
        : style === "paper-etch"
        ? (spriteCell ? "sprite-crop-paper-etch-png" : "paper-etch-png")
        : style === "paper-ink"
        ? (spriteCell ? "sprite-crop-paper-ink-png" : "paper-ink-png")
        : style === "paper-stencil"
        ? (spriteCell ? "sprite-crop-paper-stencil-png" : "paper-stencil-png")
        : (spriteCell ? "sprite-crop-png" : "imagescript-png"),
    );
    h.set("X-Faculty-Bust-Transparent-Pixels", String(transparentPixels));
    if (spriteCell) h.set("X-Faculty-Bust-Sprite-Cell", spriteCell);
    h.set("Content-Length", String(png.byteLength));
    return new Response(png.slice().buffer, { status: 200, headers: h });
  } catch (err) {
    console.error("faculty-bust failed", err);
    return Response.json(
      { error: "faculty bust failed", faculty: slug },
      { status: 500, headers: { ...corsHeaders, "Cache-Control": "no-store" } },
    );
  }
});
