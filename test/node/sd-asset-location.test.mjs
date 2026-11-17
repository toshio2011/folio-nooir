import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const workflow = fs.readFileSync(path.join(root, ".github/workflows/nooir-web-emulator-pages.yml"), "utf8");
const packager = fs.readFileSync(path.join(root, "scripts/package_nooir_web_emulator.py"), "utf8");
const smoke = fs.readFileSync(path.join(root, "scripts/nooir_browser_smoke.mjs"), "utf8");

test("CI stages each SD test image beside the engine page that fetches it", () => {
  assert.match(packager, /engine_output\s*=\s*output\s*\/\s*"engine"/);
  assert.ok(smoke.includes("new URL(${JSON.stringify(sdAsset)},document.querySelector('#emulator-engine').contentWindow.location.href)"));
  assert.match(workflow, /cp "\$sd" "build\/nooir-browser-smoke\/engine\/nooir-test-\$\{device\}\.img"/);
  for (const device of ["x3", "x4"]) {
    const image = `nooir-test-${device}.img`;
    const enginePage = new URL(`./engine/?device=${device}&sd=${image}`, "http://127.0.0.1:8178/");
    const asset = new URL(image, enginePage);
    assert.equal(asset.pathname, `/engine/${image}`);
    assert.notEqual(asset.pathname, `/${image}`);
  }
});

test("the previous root-level copy cannot satisfy the engine-relative SD request", () => {
  const image = "nooir-test-x4.img";
  const enginePage = new URL(`./engine/?device=x4&sd=${image}`, "http://127.0.0.1:8178/");
  assert.equal(new URL(image, enginePage).pathname, "/engine/nooir-test-x4.img");
  assert.notEqual(new URL(image, enginePage).pathname, "/nooir-test-x4.img");
});