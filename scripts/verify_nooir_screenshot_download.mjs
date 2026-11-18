import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";
import {inflateSync} from "node:zlib";

const PNG_SIGNATURE = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]);
const MAX_PNG_BYTES = 16 * 1024 * 1024;
const MAX_PNG_PIXELS = 5_000_000;

function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1));
  }
  return (crc ^ 0xffffffff) >>> 0;
}

export function inspectPng(input) {
  const bytes = Buffer.from(input);
  if (bytes.length < PNG_SIGNATURE.length + 12 || bytes.length > MAX_PNG_BYTES) {
    throw new Error(`PNG byte length outside accepted range: ${bytes.length}`);
  }
  if (!bytes.subarray(0, 8).equals(PNG_SIGNATURE)) throw new Error("PNG signature is invalid");

  let offset = 8;
  let width = 0;
  let height = 0;
  let colorType = -1;
  let seenHeader = false;
  let seenData = false;
  let seenEnd = false;
  const dataChunks = [];
  while (offset < bytes.length) {
    if (offset + 12 > bytes.length) throw new Error("PNG chunk header is truncated");
    const length = bytes.readUInt32BE(offset);
    const end = offset + 12 + length;
    if (end > bytes.length) throw new Error("PNG chunk body is truncated");
    const type = bytes.toString("ascii", offset + 4, offset + 8);
    const data = bytes.subarray(offset + 8, offset + 8 + length);
    const expectedCrc = bytes.readUInt32BE(offset + 8 + length);
    const actualCrc = crc32(bytes.subarray(offset + 4, offset + 8 + length));
    if (actualCrc !== expectedCrc) throw new Error(`PNG ${type} chunk CRC is invalid`);

    if (!seenHeader) {
      if (type !== "IHDR" || length !== 13) throw new Error("PNG must begin with one 13-byte IHDR chunk");
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      const bitDepth = data[8];
      colorType = data[9];
      if (!width || !height || width * height > MAX_PNG_PIXELS) throw new Error("PNG dimensions are invalid or too large");
      if (bitDepth !== 8 || ![2, 6].includes(colorType) || data[10] || data[11] || data[12]) {
        throw new Error("PNG encoding is not a supported non-interlaced 8-bit RGB/RGBA image");
      }
      seenHeader = true;
    } else if (type === "IHDR") {
      throw new Error("PNG contains more than one IHDR chunk");
    }

    if (type === "IDAT") {
      if (seenEnd) throw new Error("PNG contains image data after IEND");
      seenData = true;
      dataChunks.push(data);
    } else if (type === "IEND") {
      if (length !== 0 || !seenData) throw new Error("PNG IEND or image-data structure is invalid");
      seenEnd = true;
      offset = end;
      break;
    }
    offset = end;
  }
  if (!seenEnd || offset !== bytes.length) throw new Error("PNG is missing IEND or has trailing bytes");

  const bytesPerPixel = colorType === 6 ? 4 : 3;
  const raw = inflateSync(Buffer.concat(dataChunks), {maxOutputLength: (width * bytesPerPixel + 1) * height});
  const rowBytes = width * bytesPerPixel + 1;
  if (raw.length !== rowBytes * height) throw new Error("PNG decompressed pixel data length is invalid");
  for (let row = 0; row < height; row++) {
    if (raw[row * rowBytes] > 4) throw new Error("PNG uses an invalid row filter");
  }
  return {width, height, bytes: bytes.length};
}

function validateDownloadName(name) {
  if (typeof name !== "string" || !name || path.basename(name) !== name || /[\\/\0]/.test(name)) {
    throw new Error("Screenshot download name is empty or contains a path");
  }
  if (!/^folio-nooir-x[34]-screen\.png$/i.test(name)) {
    throw new Error(`Unexpected screenshot download name: ${name}`);
  }
}

export async function waitForScreenshotDownload({directory, name, sha256, size, width, height, timeoutMs = 12_000}) {
  validateDownloadName(name);
  if (!/^[a-f0-9]{64}$/i.test(sha256 || "")) throw new Error("Expected screenshot SHA-256 is invalid");
  if (!Number.isSafeInteger(size) || size < 1000 || size > MAX_PNG_BYTES) throw new Error("Expected screenshot size is invalid");
  if (!Number.isSafeInteger(width) || !Number.isSafeInteger(height) || width <= 0 || height <= 0) {
    throw new Error("Expected screenshot dimensions are invalid");
  }

  const root = path.resolve(directory);
  const target = path.join(root, name);
  if (path.dirname(target) !== root) throw new Error("Screenshot path escaped evidence directory");
  const deadline = Date.now() + timeoutMs;
  let lastState = "download file not present";
  while (Date.now() <= deadline) {
    const partial = `${target}.crdownload`;
    if (fs.existsSync(partial)) {
      lastState = "Chrome download is still partial";
    } else if (fs.existsSync(target)) {
      const bytes = fs.readFileSync(target);
      lastState = `download size=${bytes.length}, expected=${size}`;
      if (bytes.length === size) {
        const png = inspectPng(bytes);
        const actualSha256 = createHash("sha256").update(bytes).digest("hex");
        if (actualSha256 !== sha256.toLowerCase()) throw new Error("Downloaded PNG differs from the screenshot preview bytes");
        if (png.width !== width || png.height !== height) throw new Error("Downloaded PNG dimensions differ from the screenshot preview");
        return {name, bytes: png.bytes, width: png.width, height: png.height, sha256: actualSha256};
      }
    }
    await new Promise(resolve => setTimeout(resolve, 150));
  }
  throw new Error(`Timed out waiting for completed screenshot download (${lastState})`);
}
