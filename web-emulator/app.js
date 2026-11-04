const RELEASE_API = "https://api.github.com/repos/toshio2011/folio-nooir/releases/latest";
const RELEASES_PAGE = "https://github.com/toshio2011/folio-nooir/releases";
const RELEASE_DOWNLOAD_PREFIX = "https://github.com/toshio2011/folio-nooir/releases/download/";
const UPSTREAM_EINK_QEMU = "https://serialx.github.io/eink-qemu/";
const EXPECTED_DEV_RELEASE = Object.freeze({tag: "1.6.6", sha256: "badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108", size: 6082160});
const DEVICE_LABELS = Object.freeze({x3: "XTEINK X3", x4: "original XTEINK X4"});
const $ = (selector) => document.querySelector(selector);
const engine = $("#emulator-engine");
const firmwareStatus = $("#firmware-status");
const uploadNote = $("#upload-note");
const releasePill = $("#release-version");
let selectedRelease = null;
let selectedFirmware = null;
const activeObjectUrls = [];

function selectedDevice() { return $("#device-model").value; }
function deviceLabel(device = selectedDevice()) { return DEVICE_LABELS[device] || device; }
function updateUpstreamFallback() {
  const target = new URL(UPSTREAM_EINK_QEMU);
  target.searchParams.set("device", selectedDevice());
  const link = $("#upstream-fallback");
  link.href = target.href;
  link.textContent = `Open ${deviceLabel()} profile`;
}
function engineBase() {
  return new URL("./engine/", document.baseURI).href;
}
function setStatus(message, kind = "normal") {
  firmwareStatus.textContent = message;
  firmwareStatus.dataset.kind = kind;
}
function setRuntime(message) { $("#runtime-note").textContent = message; }
function versionLabel(release) {
  const name = (release.name || "").replace(/^#\s*/, "").trim();
  return `${name || release.tag_name} · stable`;
}
function startEngine(flashUrl, displayName) {
  const device = selectedDevice();
  const target = new URL(engineBase());
  target.searchParams.set("device", device);
  target.searchParams.set("turbo", "0");
  target.searchParams.set("flash", flashUrl);
  engine.title = `Folio Nooir ${displayName} on ${deviceLabel(device)}`;
  engine.src = target.href;
  setRuntime(`${deviceLabel(device)} · ${displayName} · accurate timing`);
}
function showNoCompatibleFirmware() {
  engine.removeAttribute("src");
  setRuntime(`${deviceLabel()} · choose firmware for this hardware profile`);
  setStatus(`Choose a firmware image verified for ${deviceLabel()}. Firmware images are not assumed interchangeable between models.`, "warning");
}
function loadSelectedFirmware() {
  if (!selectedFirmware || !selectedFirmware.models.includes(selectedDevice())) {
    showNoCompatibleFirmware();
    return;
  }
  startEngine(selectedFirmware.url, selectedFirmware.displayName);
}
function readHexDigest(digest) {
  const match = /^sha256:([a-f0-9]{64})$/i.exec(digest || "");
  return match ? match[1].toLowerCase() : null;
}
async function sha256(bytes) {
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return Array.from(new Uint8Array(hash), byte => byte.toString(16).padStart(2, "0")).join("");
}
function validateReleaseAsset(release, asset) {
  if (!asset || asset.state !== "uploaded" || asset.name !== "firmware.bin") throw new Error(`Stable release ${release.tag_name} has no uploaded firmware.bin asset.`);
  if (!Number.isSafeInteger(asset.size) || asset.size <= 0 || asset.size > 16 * 1024 * 1024) throw new Error("Stable release firmware has an unexpected size.");
  const download = new URL(asset.browser_download_url || "");
  if (download.protocol !== "https:" || download.hostname !== "github.com" || !download.href.startsWith(RELEASE_DOWNLOAD_PREFIX) || !download.pathname.endsWith("/firmware.bin")) {
    throw new Error("GitHub returned a firmware link outside the official Folio Nooir release assets.");
  }
  return download.href;
}
function isVerifiedShared166(release, asset) {
  return release.tag_name === EXPECTED_DEV_RELEASE.tag && asset.size === EXPECTED_DEV_RELEASE.size && readHexDigest(asset.digest) === EXPECTED_DEV_RELEASE.sha256;
}
async function getLatestStable() {
  setStatus("Checking the latest stable Nooir release…");
  releasePill.textContent = "Checking releases";
  const response = await fetch(RELEASE_API, {
    headers: {Accept: "application/vnd.github+json"},
    cache: "no-store",
    credentials: "omit"
  });
  if (!response.ok) throw new Error(`GitHub Releases returned ${response.status}`);
  const release = await response.json();
  if (release.draft || release.prerelease || !release.tag_name) throw new Error("GitHub did not return a stable release.");
  const asset = (release.assets || []).find(item => item.name === "firmware.bin");
  const downloadUrl = validateReleaseAsset(release, asset);
  selectedRelease = {release, asset, downloadUrl};
  releasePill.textContent = versionLabel(release);
  const link = $("#release-link");
  link.href = downloadUrl;
  link.textContent = `Download v${release.tag_name} firmware`;

  selectedFirmware = null;
  showNoCompatibleFirmware();
  const crossDeviceNote = isVerifiedShared166(release, asset)
    ? "The v1.6.6 release is the verified shared X3/X4 image."
    : "This release has no verified device manifest in the emulator, so select only a model-matched image.";
  setStatus(`Stable ${release.tag_name} found. ${crossDeviceNote} Download it from GitHub, then choose the saved .bin; this site cannot read the release asset directly because GitHub's redirect blocks browser access. The browser checks the published SHA-256 when available. No firmware mirror is used.`, "warning");
}
async function handFileToEngine(file) {
  if (!file.name.toLowerCase().endsWith(".bin")) throw new Error("Choose a firmware .bin file.");
  if (!file.size || file.size > 16 * 1024 * 1024) throw new Error("Choose a non-empty .bin no larger than 16 MiB.");
  const target = new URL(engineBase());
  if (target.origin !== location.origin) throw new Error("Firmware selection must stay on this same-origin Nooir emulator package.");
  let digest = null;
  try { digest = await sha256(await file.arrayBuffer()); } catch { /* Keep local custom images usable when WebCrypto is unavailable. */ }
  const releaseDigest = selectedRelease && readHexDigest(selectedRelease.asset.digest);
  const exactRelease = Boolean(selectedRelease && releaseDigest && file.size === selectedRelease.asset.size && digest === releaseDigest);
  const shared166 = exactRelease && isVerifiedShared166(selectedRelease.release, selectedRelease.asset);
  const models = shared166 ? ["x3", "x4"] : [selectedDevice()];
  const browserUrl = URL.createObjectURL(file);
  activeObjectUrls.push(browserUrl);
  selectedFirmware = {
    url: browserUrl,
    displayName: exactRelease ? `stable v${selectedRelease.release.tag_name}` : `custom ${file.name}`,
    models,
    verifiedStable: exactRelease
  };
  loadSelectedFirmware();
  const hashLabel = digest ? `SHA-256 ${digest}` : "SHA-256 unavailable in this browser";
  const verification = exactRelease ? `matches GitHub's v${selectedRelease.release.tag_name} asset digest` : "not matched to the selected stable release";
  uploadNote.textContent = `${file.name} selected locally (${file.size.toLocaleString()} bytes; ${hashLabel}; ${verification}). It remains in browser memory and is not uploaded.`;
  uploadNote.dataset.kind = exactRelease ? "success" : "normal";
  if (!shared166) setStatus(`Running this file only on ${deviceLabel()}. The emulator cannot infer firmware compatibility for other device models.`, "warning");
}
$("#refresh-release").addEventListener("click", () => getLatestStable().catch(error => {
  releasePill.textContent = "Stable release lookup failed";
  setStatus(`${error.message} You can use the official releases page or choose a local .bin.`, "warning");
}));
$("#custom-bin").addEventListener("change", event => {
  const input = event.currentTarget;
  const file = input.files?.[0];
  input.value = "";
  if (!file) return;
  handFileToEngine(file).catch(error => {
    uploadNote.textContent = error.message;
    uploadNote.dataset.kind = "warning";
  });
});
$("#device-model").addEventListener("change", () => {
  updateUpstreamFallback();
  if (selectedFirmware && selectedFirmware.models.includes(selectedDevice())) {
    loadSelectedFirmware();
    return;
  }
  selectedFirmware = null;
  showNoCompatibleFirmware();
});
updateUpstreamFallback();
function showView(view) {
  const isAbout = view === "about";
  $("#emulator-view").hidden = isAbout;
  $("#about-view").hidden = !isAbout;
  $("#nav-emulator").classList.toggle("active", !isAbout);
  $("#nav-about").classList.toggle("active", isAbout);
  $("#nav-emulator").setAttribute("aria-pressed", String(!isAbout));
  $("#nav-about").setAttribute("aria-pressed", String(isAbout));
}
$("#nav-emulator").addEventListener("click", () => showView("emulator"));
$("#nav-about").addEventListener("click", () => showView("about"));
engine.addEventListener("load", () => {
  try {
    if (engine.contentWindow.location.origin === location.origin) {
      uploadNote.textContent = "Selected .bin files are handed to the local emulator in this browser; they are not uploaded to a server.";
      uploadNote.dataset.kind = "normal";
    }
  } catch { /* A same-origin package is required for the outer file picker handoff. */ }
});
for (const link of document.querySelectorAll("[data-license-file]")) {
  link.addEventListener("click", event => {
    if (location.protocol !== "file:") return;
    event.preventDefault();
    window.open("https://github.com/serialx/eink-qemu/tree/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc", "_blank", "noopener");
  });
}
getLatestStable().catch(error => {
  releasePill.textContent = "Stable release lookup failed";
  setStatus(`${error.message} Use the official releases page or choose a local .bin.`, "warning");
});
