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
#include "faculty175_human_design_math.h"

int main(void) {
    faculty175_chart_positions_t positions = {0};
    if (!faculty175_astro_positions_at_epoch((time_t)1785585600, &positions)) {
        return 1;
    }
    for (int i = 0; i < FACULTY175_CHART_BODY_COUNT; ++i) {
        printf("%.9f\\n", positions.lon[i]);
    }
    time_t design_epoch = 0;
    uint8_t sun_gate = 0, sun_line = 0, moon_gate = 0, moon_line = 0;
    uint8_t design_sun_gate = 0, design_sun_line = 0, design_moon_gate = 0, design_moon_line = 0;
    faculty175_chart_positions_t design = {0};
    if (!faculty175_human_design_design_epoch((time_t)1785585600, &design_epoch) ||
        !faculty175_human_design_gate_line(positions.lon[0], &sun_gate, &sun_line) ||
        !faculty175_human_design_gate_line(positions.lon[1], &moon_gate, &moon_line) ||
        !faculty175_astro_positions_at_epoch(design_epoch, &design) ||
        !faculty175_human_design_gate_line(design.lon[0], &design_sun_gate, &design_sun_line) ||
        !faculty175_human_design_gate_line(design.lon[1], &design_moon_gate, &design_moon_line)) {
        return 2;
    }
    printf("%lld\\n%u.%u\\n%u.%u\\n%u.%u\\n%u.%u\\n", (long long)design_epoch,
           sun_gate, sun_line, moon_gate, moon_line,
           design_sun_gate, design_sun_line, design_moon_gate, design_moon_line);
    double uranus = 0, neptune = 0, pluto = 0, node = 0;
    if (!faculty175_astro_slow_positions_at_epoch((time_t)74016000,
                                                   &uranus, &neptune, &pluto, &node)) {
        return 3;
    }
    printf("%.9f\\n%.9f\\n%.9f\\n", uranus, neptune, pluto);
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
    join(root, "astrolabe175c/main/faculty175_human_design_math.c"),
    "-lm",
    "-o", executable,
  ]);
  const output = execFileSync(executable, { encoding: "utf8" })
    .trim()
    .split("\n");
  const actual = output.slice(0, fixture.length).map(Number);
  assert.equal(actual.length, fixture.length);
  actual.forEach((longitude, body) => {
    assert.ok(
      angularDifference(longitude, fixture[body]) <= 1,
      `body ${body}: ${longitude}° is too far from JPL ${fixture[body]}°`,
    );
  });
  assert.ok(Math.abs(Number(output[7]) - 1777649061) <= 7200, "Design epoch must follow the 88° solar arc");
  assert.equal(output[8], "33.2", "Personality Sun gate/line");
  assert.equal(output[9], "63.2", "Personality Moon gate/line");
  assert.equal(output[10], "24.4", "Design Sun gate/line");
  assert.equal(output[11], "44.3", "Design Moon gate/line");
  const slowFixture = [195.0564731, 244.3739272, 179.6045570];
  output.slice(12, 15).map(Number).forEach((longitude, body) => {
    assert.ok(
      angularDifference(longitude, slowFixture[body]) <= 1,
      `slow body ${body}: ${longitude}° is too far from JPL ${slowFixture[body]}°`,
    );
  });
});
