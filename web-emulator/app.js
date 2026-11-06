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
const screenCaptureDialog = $("#screen-capture-dialog");
let screenCaptureUrl = null;

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
function getEngineCanvas() {
  try {
    const doc = engine.contentDocument;
    if (!doc) return null;
    const direct = doc.querySelector("#canvas");
    if (direct) return direct;
    for (const frame of doc.querySelectorAll("iframe")) {
      const canvas = frame.contentDocument?.querySelector("#canvas");
      if (canvas) return canvas;
    }
  } catch { /* The engine must be same-origin for local firmware handoff. */ }
  return null;
}
function captureEmulatorScreen() {
  const status = $("#screen-capture-status");
  const canvas = getEngineCanvas();
  if (!canvas || !canvas.width || !canvas.height) {
    status.textContent = "The emulator screen is not available yet. Start the device and try again.";
    status.dataset.kind = "warning";
    return;
  }
  const showCaptureFailure = () => {
    status.textContent = "This browser could not capture the emulator canvas. You can still use your browser's built-in screenshot tool.";
    status.dataset.kind = "warning";
    screenCaptureDialog.showModal();
  };
  try { canvas.toBlob(blob => {
    if (!blob) {
      showCaptureFailure();
      return;
    }
    if (screenCaptureUrl) URL.revokeObjectURL(screenCaptureUrl);
    screenCaptureUrl = URL.createObjectURL(blob);
    $("#captured-screen").src = screenCaptureUrl;
    const filename = `folio-nooir-${selectedDevice()}-screen.png`;
    const download = $("#download-screen-image");
    download.href = screenCaptureUrl;
    download.download = filename;
    const openImage = $("#open-screen-image");
    openImage.href = screenCaptureUrl;
    status.textContent = `PNG ready (${canvas.width} × ${canvas.height}). Download it, or open it in a new tab if this browser does not support downloads.`;
    status.dataset.kind = "success";
    screenCaptureDialog.showModal();
  }, "image/png"); }
  catch { showCaptureFailure(); }
}
$("#capture-screen").addEventListener("click", captureEmulatorScreen);
$("#close-screen-capture").addEventListener("click", () => screenCaptureDialog.close());
screenCaptureDialog.addEventListener("close", () => {
  $("#captured-screen").removeAttribute("src");
  $("#download-screen-image").removeAttribute("href");
  $("#open-screen-image").removeAttribute("href");
  if (screenCaptureUrl) URL.revokeObjectURL(screenCaptureUrl);
  screenCaptureUrl = null;
});
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

// eink-qemu's scripted GPIO presses hold a key for 150 ms. A browser pointer
// click can produce pointerdown and pointerup in the same task, before the
// guest's ADC key scan observes the press. Keep this adapter in the Nooir
// wrapper: it extends only short taps on the pinned engine's virtual keys and
// leaves longer holds (including Power) unchanged.
function installMinimumPointerKeyPress(iframe) {
  let doc, win;
  try {
    doc = iframe.contentDocument;
    win = iframe.contentWindow;
  } catch { return; }
  if (!doc || !win || doc.documentElement.dataset.nooirMinimumKeyPress === "installed") return;
  doc.documentElement.dataset.nooirMinimumKeyPress = "installed";

  const minimumMs = 150;
  const starts = new Map();
  const pending = new Map();
  function keyButton(event) {
    const button = event.target?.closest?.("#keys button[data-key]");
    if (!button) return null;
    return {button, token: `${event.pointerId}:${button.dataset.key}`};
  }
  function clearPending() {
    for (const item of pending.values()) win.clearTimeout(item.timer);
    pending.clear();
    starts.clear();
  }

  doc.addEventListener("pointerdown", event => {
    const match = keyButton(event);
    if (!match) return;
    const previous = pending.get(match.token);
    if (previous) {
      win.clearTimeout(previous.timer);
      pending.delete(match.token);
      starts.set(match.token, {button: match.button, started: previous.started});
    } else {
      starts.set(match.token, {button: match.button, started: win.performance.now()});
    }
  }, true);

  doc.addEventListener("pointerup", event => {
    const match = keyButton(event);
    const started = match && starts.get(match.token);
    if (!match || !started) return;
    starts.delete(match.token);
    const delay = Math.ceil(minimumMs - (win.performance.now() - started.started));
    if (delay <= 0) return;

    // Let the click itself finish, but hold the engine's key-up until the
    // minimum guest-visible press has elapsed. Suppress the matching capture
    // loss because the engine also treats that event as an immediate release.
    event.stopImmediatePropagation();
    const entry = {button: started.button, started: started.started, timer: 0};
    const pointer = {
      bubbles: true,
      cancelable: true,
      pointerId: event.pointerId,
      pointerType: event.pointerType || "mouse",
      button: event.button,
      buttons: 0,
      isPrimary: event.isPrimary,
    };
    entry.timer = win.setTimeout(() => {
      pending.delete(match.token);
      entry.button.dispatchEvent(new win.PointerEvent("pointerup", pointer));
    }, delay);
    pending.set(match.token, entry);
  }, true);

  doc.addEventListener("lostpointercapture", event => {
    const match = keyButton(event);
    if (match && pending.has(match.token)) event.stopImmediatePropagation();
  }, true);
  doc.addEventListener("pointercancel", event => {
    const match = keyButton(event);
    if (match) starts.delete(match.token);
  }, true);
  win.addEventListener("blur", clearPending, true);
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
      installMinimumPointerKeyPress(engine);
      $("#capture-screen").disabled = false;
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
