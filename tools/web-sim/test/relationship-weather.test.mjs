import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import test from "node:test";

const root = resolve(import.meta.dirname, "../../..");

test("relationship weather time travel is bounded, deterministic, and date-correct", () => {
  const work = mkdtempSync(join(tmpdir(), "astrolabe-relationship-"));
  const runner = join(work, "runner.c");
  const executable = join(work, "runner");
  writeFileSync(
    runner,
    `
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "astrolabe_time.h"
#include "faculty175_astro_math.h"
#include "faculty175_relationship_weather.h"

bool astrolabe_time_valid(void) { return true; }
time_t astrolabe_time_now(void) { return (time_t)1784890800; }
void astrolabe_time_local(struct tm *out) {
    memset(out, 0, sizeof(*out));
    out->tm_year = 2026 - 1900;
    out->tm_mon = 7 - 1;
    out->tm_mday = 24;
    out->tm_hour = 12;
}
void faculty175_charts_ensure_family_seed(void) {}
bool faculty175_charts_primary(faculty175_birth_chart_t *out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->name, "Daniel");
    out->valid = true;
    return true;
}
bool faculty175_charts_active(faculty175_birth_chart_t *out) {
    memset(out, 0, sizeof(*out));
    strcpy(out->name, "Finn");
    out->valid = true;
    return true;
}
bool faculty175_charts_birth_positions(const faculty175_birth_chart_t *birth,
                                       faculty175_chart_positions_t *out) {
    const time_t epoch = strcmp(birth->name, "Daniel") == 0
        ? (time_t)458827200
        : (time_t)1420113600;
    return faculty175_astro_positions_at_epoch(epoch, out);
}
bool faculty175_charts_positions_at(time_t epoch,
                                    faculty175_chart_positions_t *out) {
    return faculty175_astro_positions_at_epoch(epoch, out);
}
int faculty175_charts_active_slot(void) { return 2; }

int main(void) {
    printf("%d\\n", faculty175_relationship_weather_select_date("2026-02-30"));
    printf("%d\\n", faculty175_relationship_weather_select_date("2037-07-25"));
    if (faculty175_relationship_weather_select_date("2026-07-26") != ESP_OK) {
        return 1;
    }
    faculty175_relationship_weather_snapshot_t snapshot = {0};
    if (!faculty175_relationship_weather_snapshot(&snapshot)) {
        return 2;
    }
    printf("%s\\n%d\\n%s\\n%s\\n%d\\n",
           snapshot.selected_date,
           snapshot.offset_days,
           snapshot.primary_name,
           snapshot.target_name,
           snapshot.active_slot);
    for (int day = 0; day < FACULTY175_RELATIONSHIP_ARC_DAYS; ++day) {
        printf("%d", snapshot.arc[day]);
    }
    printf("\\n");
    faculty175_relationship_weather_select_today();
    if (!faculty175_relationship_weather_snapshot(&snapshot)) {
        return 3;
    }
    printf("%s\\n%d\\n", snapshot.selected_date, snapshot.offset_days);
    return 0;
}
`,
  );
  execFileSync(process.env.CC || "cc", [
    "-std=c11",
    "-D_POSIX_C_SOURCE=200809L",
    "-I",
    join(root, "astrolabe175c/main"),
    "-I",
    join(root, "tools/web-sim/faculty175_compat"),
    "-I",
    join(root, "lib/astrolabe_time/include"),
    runner,
    join(root, "astrolabe175c/main/faculty175_relationship_weather.c"),
    join(root, "astrolabe175c/main/faculty175_astro_math.c"),
    "-lm",
    "-o",
    executable,
  ]);
  const output = execFileSync(executable, { encoding: "utf8" })
    .trim()
    .split("\n");
  assert.notEqual(Number(output[0]), 0, "invalid civil date must fail");
  assert.notEqual(Number(output[1]), 0, "selection beyond ten years must fail");
  assert.deepEqual(output.slice(2, 7), [
    "2026-07-26",
    "2",
    "Daniel",
    "Finn",
    "2",
  ]);
  assert.match(output[7], /^[0-4]{10}$/);
  assert.equal(output[8], "2026-07-24");
  assert.equal(output[9], "0");
});
