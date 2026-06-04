import { assertEquals } from "jsr:@std/assert@1";
import {
  facultyBustPathCandidates,
  normalizeFacultyParam,
  resolveFacultyBustView,
  resolveFacultySlugFromSearchParams,
} from "./facultyBust.ts";

Deno.test("normalizeFacultyParam accepts dotted and dashed slugs", () => {
  assertEquals(normalizeFacultyParam("a.einstein"), "a.einstein");
  assertEquals(normalizeFacultyParam("a-einstein"), "a-einstein");
  assertEquals(normalizeFacultyParam("einstein"), "einstein");
});

Deno.test("resolveFacultySlugFromSearchParams prefers faculty then handle", () => {
  assertEquals(
    resolveFacultySlugFromSearchParams(
      new URLSearchParams("faculty=a.einstein"),
    ),
    "a.einstein",
  );
  assertEquals(
    resolveFacultySlugFromSearchParams(
      new URLSearchParams("handle=a-einstein"),
    ),
    "a-einstein",
  );
  assertEquals(
    resolveFacultySlugFromSearchParams(
      new URLSearchParams("faculty=a.einstein&handle=ignored"),
    ),
    "a.einstein",
  );
});

Deno.test("resolveFacultyBustView defaults to right and maps frontal aliases", () => {
  assertEquals(resolveFacultyBustView(new URLSearchParams("")), "right");
  assertEquals(
    resolveFacultyBustView(new URLSearchParams("view=right")),
    "right",
  );
  assertEquals(
    resolveFacultyBustView(new URLSearchParams("view=frontal")),
    "frontal",
  );
  assertEquals(
    resolveFacultyBustView(new URLSearchParams("view=forward")),
    "frontal",
  );
  assertEquals(
    resolveFacultyBustView(new URLSearchParams("view=line")),
    "line",
  );
  assertEquals(
    resolveFacultyBustView(new URLSearchParams("variant=line_bust")),
    "line",
  );
});

Deno.test("facultyBustPathCandidates uses view-specific stems", () => {
  const right = facultyBustPathCandidates("nabokov", "right");
  assertEquals(right.paths.some((p) => p.endsWith("nabokov/bust.png")), true);
  assertEquals(right.paths.some((p) => p.includes("bust_frontal")), false);

  const frontal = facultyBustPathCandidates("nabokov", "frontal");
  assertEquals(
    frontal.paths.some((p) => p.endsWith("nabokov/bust_frontal.png")),
    true,
  );
  assertEquals(
    frontal.paths.some((p) => p.endsWith("nabokov/bust.png")),
    false,
  );

  const line = facultyBustPathCandidates("nabokov", "line");
  assertEquals(
    line.paths.some((p) => p.endsWith("nabokov/line_bust.png")),
    true,
  );
  assertEquals(
    line.paths.some((p) => p.endsWith("nabokov/bust_line.png")),
    true,
  );
  assertEquals(line.paths.some((p) => p.endsWith("nabokov/bust.png")), false);
});
