const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");

const path = require("node:path");
const index = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
const sw = fs.readFileSync(path.join(__dirname, "sw.js"), "utf8");

test("service worker cache tracks the current stylesheet version", () => {
  const stylesheet = index.match(/app\.css\?v=(\d+)/)?.[1];
  const cachedStylesheet = sw.match(/app\.css\?v=(\d+)/)?.[1];
  assert.ok(stylesheet);
  assert.equal(cachedStylesheet, stylesheet);
  assert.match(sw, /lunasay-app-v11/);
});
