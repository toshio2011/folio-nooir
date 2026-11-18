import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import {createHash} from "node:crypto";
import {deflateSync} from "node:zlib";
import test from "node:test";
import {inspectPng, waitForScreenshotDownload} from "../../scripts/verify_nooir_screenshot_download.mjs";

const signature = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function chunk(name, data) {
  const type = Buffer.from(name, "ascii");
  const header = Buffer.alloc(4);
  header.writeUInt32BE(data.length);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([type, data])));
  return Buffer.concat([header, type, data, crc]);
}
function samplePng() {
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(32, 0);
  ihdr.writeUInt32BE(32, 4);
  ihdr[8] = 8;
  ihdr[9] = 6;
  const pixels = Buffer.alloc(32 * (1 + 32 * 4));
  let offset = 0;
  for (let y = 0; y < 32; y++) {
    pixels[offset++] = 0;
    for (let x = 0; x < 32; x++) {
      pixels[offset++] = (x * 31 + y * 17) & 0xff;
      pixels[offset++] = (x * 13 + y * 43) & 0xff;
      pixels[offset++] = (x * 59 + y * 7) & 0xff;
      pixels[offset++] = 255;
    }
  }
  const imageData = deflateSync(pixels);
  return Buffer.concat([signature, chunk("IHDR", ihdr), chunk("IDAT", imageData), chunk("IEND", Buffer.alloc(0))]);
}

test("accepts a complete PNG with valid chunks and pixel data", () => {
  assert.deepEqual(inspectPng(samplePng()), {width: 32, height: 32, bytes: samplePng().length});
});

test("rejects malformed PNG signature, CRC, and truncated chunks", () => {
  const image = samplePng();
  const badSignature = Buffer.from(image);
  badSignature[0] = 0;
  assert.throws(() => inspectPng(badSignature), /signature/);
  const badCrc = Buffer.from(image);
  badCrc[badCrc.length - 5] ^= 1;
  assert.throws(() => inspectPng(badCrc), /CRC/);
  assert.throws(() => inspectPng(image.subarray(0, image.length - 4)), /truncated|IEND/);
});

test("waits for the named download and checks bytes against the screenshot preview", async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "nooir-shot-"));
  try {
    const image = samplePng();
    const name = "folio-nooir-x4-screen.png";
    const digest = createHash("sha256").update(image).digest("hex");
    fs.writeFileSync(path.join(directory, `${name}.crdownload`), image);
    const promise = waitForScreenshotDownload({directory, name, sha256: digest, size: image.length, width: 32, height: 32, timeoutMs: 1000});
    await new Promise(resolve => setTimeout(resolve, 180));
    fs.renameSync(path.join(directory, `${name}.crdownload`), path.join(directory, name));
    assert.deepEqual(await promise, {name, bytes: image.length, width: 32, height: 32, sha256: digest});
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});

test("rejects a different or path-like screenshot download", async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "nooir-shot-"));
  try {
    const image = samplePng();
    const name = "folio-nooir-x3-screen.png";
    fs.writeFileSync(path.join(directory, name), image);
    const digest = createHash("sha256").update(Buffer.from("different screenshot")).digest("hex");
    await assert.rejects(
      waitForScreenshotDownload({directory, name, sha256: digest, size: image.length, width: 32, height: 32, timeoutMs: 100}),
      /differs from the screenshot preview/,
    );
    await assert.rejects(
      waitForScreenshotDownload({directory, name: "../folio-nooir-x3-screen.png", sha256: digest, size: image.length, width: 32, height: 32}),
      /path/,
    );
  } finally {
    fs.rmSync(directory, {recursive: true, force: true});
  }
});
