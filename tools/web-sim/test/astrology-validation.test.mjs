import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";

const root = resolve(import.meta.dirname, "../../..");
const fixture = [
  129.2671898, // Sun
  342.5007107, // Moon
  109.9336698, // Mercury
  174.5777489, // Venus
  83.4061674,  // Mars
  127.0723297, // Jupiter
  14.7224712,  // Saturn
];

function angularDifference(a, b) {
  const distance = Math.abs(a - b) % 360;
  return Math.min(distance, 360 - distance);
}

test("shared firmware astrology math stays within one degree of JPL Horizons", () => {
  const work = mkdtempSync(join(tmpdir(), "astrolabe-astro-"));
  const runner = join(work, "runner.c");
  const executable = join(work, "runner");
  writeFileSync(runner, `
#include <stdio.h>
#include "faculty175_astro_math.h"

int main(void) {
    faculty175_chart_positions_t positions = {0};
    if (!faculty175_astro_positions_at_epoch((time_t)1785585600, &positions)) {
        return 1;
    }
    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        printf("%.9f\\n", positions.lon[i]);
    }
    return 0;
}
`);
  execFileSync(process.env.CC || "cc", [
    "-std=c11",
    "-D_POSIX_C_SOURCE=200809L",
    "-I", join(root, "astrolabe175c/main"),
    "-I", join(root, "tools/web-sim/faculty175_compat"),
    runner,
    join(root, "astrolabe175c/main/faculty175_astro_math.c"),
    "-lm",
    "-o", executable,
  ]);
  const actual = execFileSync(executable, { encoding: "utf8" })
    .trim()
    .split("\n")
    .map(Number);
  assert.equal(actual.length, fixture.length);
  actual.forEach((longitude, body) => {
    assert.ok(
      angularDifference(longitude, fixture[body]) <= 1,
      `body ${body}: ${longitude}° is too far from JPL ${fixture[body]}°`,
    );
  });
});
