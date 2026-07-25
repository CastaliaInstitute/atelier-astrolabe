import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";

const root = resolve(import.meta.dirname, "../../..");

test("BaZi uses solar-term year/month boundaries and the standard day-cycle phase", () => {
  const work = mkdtempSync(join(tmpdir(), "astrolabe-bazi-"));
  const runner = join(work, "runner.c");
  const executable = join(work, "runner");
  writeFileSync(runner, `
#include <stdio.h>
#include "faculty175_bazi_math.h"

int main(void) {
    faculty175_bazi_chart_t chart = {0};
    if (!faculty175_bazi_calculate(2000, 1, 1, 10, 24, 280.0, &chart)) return 1;
    for (int i = 0; i < 4; ++i) {
        printf("%u,%u", chart.stem[i], chart.branch[i]);
        putchar(10);
    }
    faculty175_bazi_chart_t before = {0}, after = {0};
    if (!faculty175_bazi_calculate(2000, 2, 4, 12, 0, 314.999, &before) ||
        !faculty175_bazi_calculate(2000, 2, 4, 12, 0, 315.001, &after)) return 2;
    printf("%u,%u,%u,%u", before.stem[0], before.branch[0], after.stem[0], after.branch[0]);
    putchar(10);
    return 0;
}
`);
  execFileSync(process.env.CC || "cc", [
    "-std=c11", "-Wall", "-Wextra", "-Werror",
    "-I", join(root, "astrolabe185b/main"), runner,
    join(root, "astrolabe185b/main/faculty175_bazi_math.c"), "-lm", "-o", executable,
  ]);
  const lines = execFileSync(executable, { encoding: "utf8" }).trim().split(String.fromCharCode(10));
  assert.deepEqual(lines.slice(0, 4), ["5,3", "2,0", "4,6", "3,5"]);
  assert.equal(lines[4], "5,3,6,4");
});
