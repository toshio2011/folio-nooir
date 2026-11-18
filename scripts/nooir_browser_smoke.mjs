/* Browser acceptance test for the local Nooir shell and pinned eink-qemu web
 * engine. It uses Chrome DevTools Protocol and needs no npm dependencies.
 * Start a loopback COOP/COEP preview and a dedicated Chrome profile with
 * --remote-debugging-port=9224, then pass the local firmware and test SD paths.
 * Firmware bytes are selected into the browser and never uploaded. */
import fs from "node:fs";
import path from "node:path";
import {createHash} from "node:crypto";
import {formatCdpTargetDiagnostics, waitForCdpPageTarget} from "./select_cdp_page_target.mjs";
import {inspectPng, waitForScreenshotDownload} from "./verify_nooir_screenshot_download.mjs";

const [url, device, firmwareArg, sdArg, evidenceArg] = process.argv.slice(2);
const requestedHoldMs = Number(process.env.NOOIR_BUTTON_HOLD_MS);
const buttonHoldMs = Number.isFinite(requestedHoldMs) ? Math.min(600, Math.max(170, requestedHoldMs)) : 170;
const requestedBackHoldMs = Number(process.env.NOOIR_BACK_HOLD_MS);
function holdFor(label) {
  return label === 'Back' && Number.isFinite(requestedBackHoldMs)
    ? Math.min(600, Math.max(50, requestedBackHoldMs))
    : buttonHoldMs;
}
if (!url || !["x3", "x4"].includes(device) || !firmwareArg || !sdArg || !evidenceArg) {
  throw new Error("Usage: node scripts/nooir_browser_smoke.mjs <local-url> <x3|x4> <firmware.bin> <test-sd.img> <new-evidence-dir>");
}
const firmware = path.resolve(firmwareArg), sd = path.resolve(sdArg), evidence = path.resolve(evidenceArg);
const sdAsset = process.env.NOOIR_SD_ASSET || `nooir-test-${device}.img`;
if (fs.existsSync(evidence)) throw new Error(`Evidence directory already exists: ${evidence}`);
for (const file of [firmware, sd]) if (!fs.statSync(file).isFile()) throw new Error(`Not a file: ${file}`);
fs.mkdirSync(evidence, {recursive: true});
const {target, diagnostics: targetDiagnostics} = await waitForCdpPageTarget(async signal => {
  const response = await fetch(`http://127.0.0.1:${process.env.CDP_PORT || 9224}/json/list`, {signal});
  if (!response.ok) throw new Error("CDP /json/list returned HTTP " + response.status);
  return response.json();
}, url);
if (!target) throw new Error(formatCdpTargetDiagnostics(targetDiagnostics));
const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => { ws.addEventListener("open", resolve, {once: true}); ws.addEventListener("error", reject, {once: true}); });
const pending = new Map(); let seq = 0; const errors = [];
ws.addEventListener("message", event => {
  const message = JSON.parse(event.data);
  if (message.id) { const p = pending.get(message.id); if (!p) return; pending.delete(message.id); message.error ? p.reject(new Error(message.error.message)) : p.resolve(message.result); }
  else if (message.method === "Runtime.exceptionThrown") errors.push(message.params.exceptionDetails?.text || "page exception");
});
function cdp(method, params = {}) { return new Promise((resolve, reject) => { const id = ++seq; pending.set(id, {resolve, reject}); ws.send(JSON.stringify({id, method, params})); }); }
async function evalPage(expression, byValue = true) {
  const r = await cdp("Runtime.evaluate", {expression, returnByValue: byValue, awaitPromise: true});
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.text || "browser evaluation failed");
  return r.result;
}
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function waitFor(expression, label, ms = 120_000) {
  const until = Date.now() + ms;
  while (Date.now() < until) {
    if (errors.length) throw new Error(`Browser exception: ${errors.at(-1)}`);
    try { const value = (await evalPage(expression)).value; if (value) return value; }
    catch (error) { if (!/Execution context was destroyed|Cannot find context/.test(error.message)) throw error; }
    await pause(250);
  }
  throw new Error(`Timed out waiting for ${label}`);
}
async function setFile(selector, file, dispatchChange = false) {
  const {root} = await cdp("DOM.getDocument", {depth: 0});
  const {nodeId} = await cdp("DOM.querySelector", {nodeId: root.nodeId, selector});
  if (!nodeId) throw new Error(`File input not found: ${selector}`);
  await cdp("DOM.setFileInputFiles", {nodeId, files: [file]});
  let selection = {selector, name: path.basename(file), event: "not-requested"};
  if (dispatchChange) {
    // The Nooir handler clears input.value immediately and starts loading the
    // firmware asynchronously; the later engine-load event also resets the
    // helper note. Treat the exact v1.6.6 build+render markers as confirmation
    // that the browser consumed the selected file after a page reload.
    const uploadSelected = `(() => {
      const note=document.querySelector('#upload-note')?.textContent||'';
      const serial=document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||'';
      return /selected locally/i.test(note) || (/app=1\\.6\\.6\\+b961c1f7/.test(serial) && /render_ready/.test(serial));
    })()`;
    try {
      await waitFor(uploadSelected, "firmware selection or verified guest startup", 5_000);
      selection.event = "automatic-or-boot-confirmed";
    } catch {
      const result = await evalPage(`(() => {
        const input=document.querySelector(${JSON.stringify(selector)});
        if(!input?.files?.length) return {found:Boolean(input),files:0};
        const file={name:input.files[0].name,size:input.files[0].size};
        input.dispatchEvent(new Event('input',{bubbles:true}));
        input.dispatchEvent(new Event('change',{bubbles:true}));
        return {found:true,files:1,...file};
      })()`);
      if (!result.value?.found) throw new Error(`The browser did not handle ${selector} selection: ${JSON.stringify(result.value)}`);
      if (result.value.files === 1) {
        await waitFor(uploadSelected, "manual firmware selection or verified guest startup", 120_000);
        selection = {...selection, ...result.value, event: "manual-or-boot-confirmed"};
      } else {
        await waitFor(uploadSelected, "firmware selection consumed by the wrapper", 120_000);
        selection = {...selection, event: "automatic-consumed"};
      }
    }
  }
  return selection;
}
const firmwareSha256 = createHash('sha256').update(fs.readFileSync(firmware)).digest('hex');
const state = {device, firmwareSha256, url, firmware: path.basename(firmware), sd: path.basename(sd), actions: [], screenshots: {}, acceptance: {
  home: false, screenshotDownload: false, library: false, epubOpen: false,
  forwardPageTurn: false, backwardPageTurn: false, readerExit: false,
  sdPersistenceAfterReload: false, progressPersistenceAfterReload: false
}, started: new Date().toISOString()};
const stage = message => console.log(`[${device}] ${message}`);
async function screenshot(name) {
  const {data} = await cdp("Page.captureScreenshot", {format: "png", captureBeyondViewport: false});
  const bytes = Buffer.from(data, "base64");
  const pageImage = inspectPng(bytes);
  fs.writeFileSync(path.join(evidence, name), bytes);
  const panelUrl = await canvasPng();
  if (!panelUrl?.startsWith("data:image/png;base64,")) throw new Error("The Nooir e-paper canvas could not be captured.");
  const panelBytes = Buffer.from(panelUrl.slice("data:image/png;base64,".length), "base64");
  const panelImage = inspectPng(panelBytes);
  const panelName = name.replace(/\.png$/i, "-panel.png");
  fs.writeFileSync(path.join(evidence, panelName), panelBytes);
  return {
    pageSha256: createHash("sha256").update(bytes).digest("hex"),
    pageWidth: pageImage.width,
    pageHeight: pageImage.height,
    panelFile: panelName,
    panelSha256: createHash("sha256").update(panelBytes).digest("hex"),
    panelWidth: panelImage.width,
    panelHeight: panelImage.height
  };
}
async function canvasPng() {
  return (await evalPage(`(() => { const f=document.querySelector('#emulator-engine'); const c=f?.contentDocument?.querySelector('iframe')?.contentDocument?.querySelector('#canvas'); return c?.width ? c.toDataURL('image/png') : null; })()`)).value;
}
async function stableCanvas(timeout = 20_000, maximumChangedFraction = 0.005) {
  // E-paper refreshes can rewrite a few pixels (for example dithering or a
  // small status mark) after the visible page is already stable. Compare
  // decoded pixels with a strict 0.5% ceiling instead of requiring identical
  // PNG encodings; expected navigation still has its own activity and visible
  // screen-change assertions below.
  let previous = await canvasPng(), stableIntervals = 0, lastDifference = null;
  if (!previous) throw new Error("The nested e-paper canvas is unavailable.");
  const until = Date.now() + timeout;
  while (Date.now() < until) {
    await pause(500);
    const now = await canvasPng();
    const difference = await canvasDifference(previous);
    if (!difference.available) throw new Error("The nested e-paper canvas became unavailable while settling.");
    if (difference.passed === false) throw new Error(`The e-paper canvas geometry changed while settling: ${difference.reason || "unknown geometry change"}.`);
    lastDifference = difference.differentFraction;
    if (lastDifference <= maximumChangedFraction) {
      if (++stableIntervals >= 3) return now;
    } else {
      stableIntervals = 0;
    }
    previous = now;
  }
  throw new Error(`The e-paper canvas did not settle: last frame changed ${(lastDifference ?? 1).toFixed(4)} of pixels; allowed=${maximumChangedFraction.toFixed(4)}; timeout=${timeout}ms.`);
}
async function installInputTrace() {
  await evalPage(`(()=>{
    const f=document.querySelector('#emulator-engine'),d=f.contentDocument,w=f.contentWindow;
    const trace=w.__nooirInputTrace=[];
    const record=(event,data={})=>{trace.push({event,...data,time:Math.round(performance.now())});if(trace.length>300)trace.shift()};
    const keyOf=e=>e.target?.closest?.('#keys button[data-key]')?.dataset?.key||'';
    for(const type of ['pointerdown','pointerup','pointercancel','lostpointercapture'])
      d.addEventListener(type,e=>record(type,{key:keyOf(e),trusted:e.isTrusted,pointerId:e.pointerId}),true);
    d.addEventListener('click',e=>{const key=keyOf(e);if(key)record('key-click',{key,trusted:e.isTrusted,detail:e.detail})},true);
    for(const button of d.querySelectorAll('#keys button[data-key]')){
      new MutationObserver(()=>record('key-held-class',{key:button.dataset.key,held:button.classList.contains('held')}))
        .observe(button,{attributes:true,attributeFilter:['class']});
    }
    const runtime=[...d.querySelectorAll('iframe')].find(frame=>{try{return Boolean(frame.contentDocument?.querySelector('#canvas'))}catch{return false}});
    if(runtime){
      const rw=runtime.contentWindow,rd=runtime.contentDocument;
      rw.addEventListener('message',e=>{if(e.data?.type==='key')record('runtime-key-message',{code:e.data.code,down:e.data.down})},true);
      for(const type of ['keydown','keyup'])rd.addEventListener(type,e=>record('runtime-'+type,{code:e.code,key:e.key,trusted:e.isTrusted}),true);
      const originalMonitorLine = rw.monitorLine;
      if (typeof originalMonitorLine === 'function' && typeof rw.command === 'function') {
        rw.__nooirHmpTrace = [];
        rw.monitorLine = function(line) {
          rw.__nooirHmpTrace.push(String(line));
          if (rw.__nooirHmpTrace.length > 200) rw.__nooirHmpTrace.shift();
          return originalMonitorLine.call(this, line);
        };
        record('runtime-hmp-probe-ready');
      } else {
        record('runtime-hmp-probe-unavailable', {monitorLine: typeof originalMonitorLine, command: typeof rw.command, eval: typeof rw.eval});
      }
    }else record('runtime-frame-missing');
  })()`);
}
async function currentInputTrace() {
  const value = await evalPage("JSON.stringify(document.querySelector('#emulator-engine')?.contentWindow?.__nooirInputTrace||[])");
  return JSON.parse(value.value || "[]");
}
async function qomGetKey(label) {
  const property = ({Back: 'back', Confirm: 'confirm', Left: 'left', Right: 'right', Up: 'up', Down: 'down'})[label];
  if (!property) throw new Error(`No QEMU key property for ${label}.`);
  const result = await evalPage(`(() => {
    const engine = document.querySelector('#emulator-engine');
    const runtime = [...(engine?.contentDocument?.querySelectorAll('iframe') || [])]
      .find(frame => { try { return Boolean(frame.contentDocument?.querySelector('#canvas')); } catch { return false; } });
    const w = runtime?.contentWindow;
    if (!w || typeof w.command !== 'function' || !Array.isArray(w.__nooirHmpTrace)) {
      return Promise.reject(new Error('QEMU HMP key probe is unavailable: command=' + typeof w?.command + ', trace=' + Array.isArray(w?.__nooirHmpTrace)));
    }
    const start = w.__nooirHmpTrace.length;
    w.command('qom-get /machine/x3-keys ${property}');
    return new Promise((resolve, reject) => {
      const until = Date.now() + 3_000;
      const poll = () => {
        const lines = w.__nooirHmpTrace.slice(start);
        const value = [...lines].reverse().map(line => String(line).trim()).find(line => line === 'true' || line === 'false');
        if (value) { resolve({value: value === 'true', lines}); return; }
        if (Date.now() >= until) { reject(new Error('QEMU HMP did not return x3-keys.' + '${property}' + ': ' + JSON.stringify(lines))); return; }
        setTimeout(poll, 50);
      };
      poll();
    });
  })()`);
  return result.value;
}
async function buttonPoint(label) {
  const point = (await evalPage(`(async() => {
    const f=document.querySelector('#emulator-engine'), d=f?.contentDocument;
    const norm=s=>(s||'').replace(/\\s+/g,' ').trim().toLowerCase();
    const aliases={Left:['left','arrowleft'],Right:['right','arrowright'],Confirm:['confirm','enter','ok'],Back:['back','escape','backspace']}[${JSON.stringify(label)}];
    const b=[...(d?.querySelectorAll('#keys button')||[])].find(x=>aliases.some(a=>norm(x.textContent)===a||norm(x.dataset.key)===a||norm(x.dataset.key).endsWith(a)));
    if(!b)return null;
    b.scrollIntoView({block:'center',inline:'center',behavior:'instant'});
    f.scrollIntoView({block:'center',inline:'center',behavior:'instant'});
    await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const a=f.getBoundingClientRect(),r=b.getBoundingClientRect();
    const x=a.left+r.x*a.width/f.clientWidth+r.width*a.width/f.clientWidth/2;
    const y=a.top+r.y*a.height/f.clientHeight+r.height*a.height/f.clientHeight/2;
    return {x,y,text:b.textContent.trim(),code:b.dataset.key,kbd:b.dataset.kbd,disabled:b.disabled,
      viewport:{width:innerWidth,height:innerHeight,scrollX,scrollY},inViewport:x>=0&&y>=0&&x<innerWidth&&y<innerHeight,
      hitFrame:document.elementFromPoint(x,y)===f,
      iframe:{x:a.x,y:a.y,width:a.width,height:a.height,clientWidth:f.clientWidth,clientHeight:f.clientHeight,engineScrollY:d.defaultView.scrollY},
      button:{x:r.x,y:r.y,width:r.width,height:r.height}};
  })()`)).value;
  if (!point) throw new Error(`No ${label} virtual key found.`);
  if (!point.inViewport || !point.hitFrame) throw new Error(`${label} virtual key is outside the visible emulator frame: ${JSON.stringify(point)}`);
  return point;
}
async function canvasDifference(reference) {
  const result = await evalPage(`(async()=>{
    const reference=${JSON.stringify(reference)};
    const f=document.querySelector('#emulator-engine');
    const c=f?.contentDocument?.querySelector('iframe')?.contentDocument?.querySelector('#canvas');
    if(!reference||!c)return {available:false};
    const image=new Image();image.src=reference;await image.decode();
    if(image.width!==c.width||image.height!==c.height)return {available:true,passed:false,reason:'panel geometry changed'};
    const a=document.createElement('canvas'),b=document.createElement('canvas');a.width=b.width=c.width;a.height=b.height=c.height;
    const ac=a.getContext('2d'),bc=b.getContext('2d');ac.drawImage(image,0,0);bc.drawImage(c,0,0);
    const x=ac.getImageData(0,0,c.width,c.height).data,y=bc.getImageData(0,0,c.width,c.height).data;
    let different=0,total=c.width*c.height;
    for(let i=0;i<x.length;i+=4)if(Math.max(Math.abs(x[i]-y[i]),Math.abs(x[i+1]-y[i+1]),Math.abs(x[i+2]-y[i+2]))>8)different++;
    const fraction=different/total;return {available:true,differentPixels:different,totalPixels:total,differentFraction:fraction};
  })()`);
  return result.value;
}
async function activityLogLength() {
  return (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent?.length||0")).value;
}
async function waitForActivityAfter(activity, offset, label, timeout = 60_000) {
  const needle = JSON.stringify(`activity=${activity}`);
  await waitFor(`(document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||'').slice(${offset}).includes(${needle})`, label, timeout);
  return stableCanvas();
}
async function readerPosition() {
  const serial = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value || '';
  const matches = [...serial.matchAll(/reader_quiescent reason=([^ ]+) spine=(-?[0-9]+) page=(-?[0-9]+)/g)];
  const match = matches.at(-1);
  return match ? {reason: match[1], spine: Number(match[2]), page: Number(match[3])} : null;
}
async function waitForReaderQuiescent(label, previous = null, timeout = 60_000) {
  const until = Date.now() + timeout;
  while (Date.now() < until) {
    const current = await readerPosition();
    if (current && (!previous || current.spine !== previous.spine || current.page !== previous.page)) {
      await stableCanvas();
      return current;
    }
    await pause(250);
  }
  throw new Error(`Timed out waiting for a completed EPUB render (${label}); previous=${JSON.stringify(previous)} current=${JSON.stringify(await readerPosition())}`);
}
async function press(label, expectation = {}, timeout = 30_000) {
  const baseline = await stableCanvas();
  const serialOffset = await activityLogLength();
  const b = await buttonPoint(label);
  const holdMs = holdFor(label);
  const action = {key: label, button: b.text, dataKey: b.code, keyboardCode: b.kbd, heldMs: holdMs, point: b};
  action.readerPositionBefore = await readerPosition();
  if (expectation.readerPositionChange) {
    if (!action.readerPositionBefore) throw new Error(`No EPUB quiescent-position marker exists before ${label}.`);
  }
  if (label === "Back") action.displayFrameTrace = [];
  state.actions.push(action);
  await cdp("Input.dispatchMouseEvent", {type: "mouseMoved", x: b.x, y: b.y});
  await cdp("Input.dispatchMouseEvent", {type: "mousePressed", button: "left", clickCount: 1, x: b.x, y: b.y});
  const downAt = Date.now();
  if (['Back', 'Left', 'Confirm'].includes(label)) {
    action.qomProperty = label.toLowerCase();
    action.qomStateBefore = await qomGetKey(label);
    await pause(Math.max(50, Math.floor(holdMs / 2)));
    action.qomStateWhilePressed = await qomGetKey(label);
    await pause(Math.max(0, holdMs - (Date.now() - downAt)));
  } else await pause(holdMs);
  await cdp("Input.dispatchMouseEvent", {type: "mouseReleased", button: "left", clickCount: 1, x: b.x, y: b.y});
  await pause(50);
  if (['Back', 'Left', 'Confirm'].includes(label)) {
    action.qomStateAfter = await qomGetKey(label);
  }
  const trace = await currentInputTrace();
  action.inputEvents = trace.slice(-24);
  const matchesButton = event => event.key === b.code || event.key === b.text || event.key === label;
  const pointerDown = [...trace].reverse().find(event => event.event === 'pointerdown' && matchesButton(event));
  const pointerUp = pointerDown ? trace.slice(trace.lastIndexOf(pointerDown) + 1).find(event => event.event === 'pointerup' && matchesButton(event)) : null;
  if (pointerDown && pointerUp && Number.isFinite(pointerDown.time) && Number.isFinite(pointerUp.time)) {
    action.actualHeldMs = Math.max(0, Math.round(pointerUp.time - pointerDown.time));
  }
  const activityNeedle = expectation.activity ? `activity=${expectation.activity}` : null;
  const until = Date.now() + timeout;
  let lastDisplaySampleAt = 0;
  while (Date.now() < until) {
    const now = await canvasPng();
    const sampleAt = Date.now();
    if (label === "Back" && sampleAt - lastDisplaySampleAt >= 1_000 && action.displayFrameTrace.length < 32) {
      const difference = await canvasDifference(baseline);
      action.displayFrameTrace.push({
        elapsedMs: Math.max(0, sampleAt - downAt),
        available: difference.available === true,
        differentPixels: difference.differentPixels,
        totalPixels: difference.totalPixels,
        differentFraction: difference.differentFraction,
      });
      lastDisplaySampleAt = sampleAt;
    }
    if (expectation.activity) {
      const consoleText = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value;
      if (consoleText.slice(serialOffset).includes(activityNeedle)) {
        action.observedActivity = expectation.activity;
        action.readerPositionAfter = await readerPosition();
        return stableCanvas();
      }
    } else if (expectation.readerPositionChange) {
      const current = await readerPosition();
      if (current && (current.spine !== action.readerPositionBefore.spine || current.page !== action.readerPositionBefore.page)) {
        action.readerPositionAfter = current;
        const difference = await canvasDifference(baseline);
        action.screenDifference = difference;
        if (difference.available && difference.differentFraction >= (expectation.minimumDifference ?? 0.02)) return stableCanvas();
      }
    } else if (expectation.screenChange) {
      const difference = await canvasDifference(baseline);
      action.screenDifference = difference;
      if (difference.available && difference.differentFraction >= (expectation.minimumDifference ?? 0.02)) return stableCanvas();
    }
    await pause(250);
  }
  action.finalInputEvents = (await currentInputTrace()).slice(-24);
  action.readerPositionAfter = await readerPosition();
  action.finalSerial = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value.slice(-5000);
  action.finalScreenDifference = await canvasDifference(baseline);
  if (action.displayFrameTrace?.length > 16) {
    action.displayFrameTrace = [...action.displayFrameTrace.slice(0, 8), ...action.displayFrameTrace.slice(-8)];
  }
  throw new Error(`${label} virtual-button action did not meet its expected state (${expectation.activity || `screen change >= ${expectation.minimumDifference ?? 0.02}`}).`);
}
async function pressWhenSettled(label, expectation = {}, timeout = 30_000) {
  await pause(1800);
  await stableCanvas();
  return press(label, expectation, timeout);
}
async function ready(label) {
  await waitFor(`(document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||'').includes('render_ready')`, `${label} render_ready`);
  await waitFor(`/\\[SHELF\\] stage=on_enter_view/.test(document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||'')`, `${label} Nooir Home view`);
  await waitFor(`(() => { const f=document.querySelector('#emulator-engine'),c=f?.contentDocument?.querySelector('iframe')?.contentDocument?.querySelector('#canvas'); if(!c?.width)return false; const p=c.getContext('2d').getImageData(0,0,c.width,c.height).data; let ink=0; for(let i=0;i<p.length;i+=32) if(p[i]<245)ink++; return ink>100?c.toDataURL('image/png'):false; })()`, `${label} nonblank screen`);
  return stableCanvas();
}
async function waitForReader(label) {
  return waitFor("/(?:Entering activity:\\s*\\w*Reader|activity=EpubReader|\\[MEM\\] reader enter:)/i.test(document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||'')", label, 60_000);
}
try {
  await cdp("Runtime.enable"); await cdp("Page.enable"); await cdp("DOM.enable");
  await cdp("Browser.setDownloadBehavior", {behavior: "allow", downloadPath: evidence});
  await cdp("Page.navigate", {url});
  await waitFor("document.readyState === 'complete' && crossOriginIsolated && document.querySelector('#device-model') && document.querySelector('#custom-bin')", "isolated Nooir wrapper");
  await waitFor("/stable|failed/i.test(document.querySelector('#release-version')?.textContent || '')", "stable release lookup settle", 30_000);
  stage("wrapper ready; preparing virtual SD");
  const localSdHash = createHash('sha256').update(fs.readFileSync(sd)).digest('hex');
  await evalPage(`(() => { const s=document.querySelector('#device-model'); s.value=${JSON.stringify(device)}; s.dispatchEvent(new Event('change',{bubbles:true})); const f=document.querySelector('#emulator-engine'); const u=new URL('./engine/?device=${device}&turbo=0&sd=${encodeURIComponent(sdAsset)}',location.href); f.src=u.href; })()`);
  await waitFor("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#sd-info')?.textContent?.includes('FAT32')", "mounted FAT32 SD", 60_000);
  await waitFor("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#sd-rows')?.textContent", "virtual SD directory", 60_000);
  let card = (await evalPage(`(() => { const d=document.querySelector('#emulator-engine').contentDocument; return {fs:d.querySelector('#sd-info')?.textContent,files:[...d.querySelectorAll('#sd-rows tr[data-name]')].map(r=>r.dataset.name)}; })()`)).value;
  if (!/FAT32/i.test(card.fs || "")) throw new Error(`SD image did not mount as FAT32: ${card.fs}`);
  if (!card.files.some(n=>/\.epub$/i.test(n)) && !card.files.includes('books')) throw new Error(`No EPUB/books directory in test SD root: ${card.files.join(', ')}`);
  const servedSdHash = (await evalPage(`fetch(new URL(${JSON.stringify(sdAsset)},document.querySelector('#emulator-engine').contentWindow.location.href)).then(r=>r.arrayBuffer()).then(b=>crypto.subtle.digest('SHA-256',b)).then(h=>Array.from(new Uint8Array(h),x=>x.toString(16).padStart(2,'0')).join(''))`)).value;
  if (servedSdHash !== localSdHash) throw new Error("The loopback preview SD asset does not match the supplied test image.");
  state.sdMount = {...card, sourceAsset: sdAsset, sha256: localSdHash};

  stage("FAT32 SD mounted; selecting unchanged local firmware");
  await setFile("#custom-bin", firmware);
  await waitFor("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('iframe')?.contentDocument?.querySelector('#canvas')", "firmware canvas", 120_000);
  let screen = await ready("initial boot");
  state.acceptance.home = true;
  await installInputTrace();
  stage("Nooir Home is rendering");
  state.screenshots.home = await screenshot(`${device}-home.png`);
  const probeKey = process.env.NOOIR_INPUT_PROBE_KEY;
  if (probeKey) {
    const probeActivity = process.env.NOOIR_INPUT_PROBE_ACTIVITY;
    await pressWhenSettled(probeKey, probeActivity ? {activity: probeActivity} : {screenChange: true, minimumDifference: 0.02});
    if (probeActivity === "EpubReader") await waitForReader("diagnostic Reader activity");
    state.firmwareSha256 = createHash('sha256').update(fs.readFileSync(firmware)).digest('hex');
    state.serial = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value;
    state.inputTrace = await currentInputTrace();
    state.result = "PASS";
    fs.writeFileSync(path.join(evidence, 'result.json'), JSON.stringify(state, null, 2));
    fs.writeFileSync(path.join(evidence, 'serial.log'), state.serial);
    console.log(JSON.stringify({result: state.result, device, probeKey, probeActivity: probeActivity || null, firmwareSha256: state.firmwareSha256, evidence}, null, 2));
    ws.close();
    process.exit(0);
  }

  await evalPage("document.querySelector('#capture-screen').click()");
  await waitFor("document.querySelector('#screen-capture-dialog')?.open && document.querySelector('#download-screen-image')?.href?.startsWith('blob:')", "screenshot preview");
  const png = (await evalPage(`(async()=>{
    const link=document.querySelector('#download-screen-image');
    const response=await fetch(link.href);
    if(!response.ok)throw new Error('screenshot blob fetch failed');
    const blob=await response.blob();
    const bitmap=await createImageBitmap(blob);
    const digest=await crypto.subtle.digest('SHA-256',await blob.arrayBuffer());
    const sha256=[...new Uint8Array(digest)].map(x=>x.toString(16).padStart(2,'0')).join('');
    const value={type:blob.type,size:blob.size,name:link.download,width:bitmap.width,height:bitmap.height,sha256};
    bitmap.close();
    return value;
  })()`)).value;
  if (png.type !== 'image/png' || png.size < 1000 || !png.width || !png.height || !png.sha256) {
    throw new Error(`Screenshot preview did not decode as a valid PNG: ${JSON.stringify(png)}`);
  }
  state.screenshotExport = png;
  await evalPage("document.querySelector('#download-screen-image').click()");
  state.screenshotExport.downloadedFile = await waitForScreenshotDownload({
    directory: evidence,
    name: png.name,
    sha256: png.sha256,
    size: png.size,
    width: png.width,
    height: png.height,
  });
  state.acceptance.screenshotDownload = true;
  await evalPage("document.querySelector('#close-screen-capture').click()");

  screen = await pressWhenSettled('Left', {activity: 'FolioLibrary'});
  state.acceptance.library = true;
  stage("virtual Left opened Library");
  state.screenshots.library = await screenshot(`${device}-library.png`);
  if (state.sdMount.files.includes('books')) {
    screen = await pressWhenSettled('Confirm', {screenChange: true, minimumDifference: 0.02});
    state.screenshots.libraryBookFolder = await screenshot(`${device}-library-book-folder.png`);
    const folderActivityOffset = await activityLogLength();
    await waitForActivityAfter('FolioLibrary', folderActivityOffset, 'Library idle after opening book folder');
    screen = await pressWhenSettled('Confirm', {activity: 'EpubReader'});
  } else screen = await pressWhenSettled('Confirm', {activity: 'EpubReader'});
  stage("virtual Confirm opened Reader");
  await waitForReader("reader activity");
  state.acceptance.epubOpen = true;
  state.readerFirstPosition = await waitForReaderQuiescent("initial Reader page");
  screen = await stableCanvas();
  state.screenshots.readerFirst = await screenshot(`${device}-reader-first.png`);
  const firstPage = screen;
  screen = await pressWhenSettled('Right', {readerPositionChange: true, minimumDifference: 0.02}, 60_000);
  stage("forward page turn passed");
  state.acceptance.forwardPageTurn = true;
  state.screenshots.readerForward = await screenshot(`${device}-reader-forward.png`);
  let forwardPage = screen;
  screen = await pressWhenSettled('Left', {readerPositionChange: true, minimumDifference: 0.02}, 60_000);
  state.pageBackwardDifference = await canvasDifference(firstPage);
  state.screenshots.readerBackward = await screenshot(`${device}-reader-backward.png`);
  state.pageBackwardRestoredFirst = state.pageBackwardDifference.available && state.pageBackwardDifference.differentFraction < 0.02;
  if (!state.pageBackwardRestoredFirst) throw new Error(`Backward turn did not restore the first EPUB page: ${JSON.stringify(state.pageBackwardDifference)}`);
  state.acceptance.backwardPageTurn = true;
  screen = await pressWhenSettled('Right', {readerPositionChange: true, minimumDifference: 0.02}, 60_000);
  state.pageForwardDifference = await canvasDifference(forwardPage);
  state.pageForwardAfterBackDifference = await canvasDifference(firstPage);
  state.pageForwardAfterBackConfirmed = state.pageForwardAfterBackDifference.available && state.pageForwardAfterBackDifference.differentFraction >= 0.02;
  state.screenshots.readerForwardAfterBack = await screenshot(`${device}-reader-forward-after-back.png`);
  if (!state.pageForwardAfterBackConfirmed) throw new Error(`Forward turn after the verified backward turn did not change the first EPUB page: ${JSON.stringify(state.pageForwardAfterBackDifference)}`);
  forwardPage = screen;
  screen = await pressWhenSettled('Back', {activity: 'RecentBooks'});
  state.screenshots.readerExit = await screenshot(`${device}-reader-exit.png`);
  state.readerExit = true;
  state.acceptance.readerExit = true;
  state.inputTraceBeforeReload = await currentInputTrace();
  await pause(1800);
  const recentProgressCanvas = await canvasPng();
  await evalPage(`sessionStorage.setItem('nooir-browser-smoke-progress-reference',${JSON.stringify(recentProgressCanvas)})`);
  state.screenshots.recentProgressBeforeReload = await screenshot(`${device}-recent-progress-before-reload.png`);
  await waitFor("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#sd-save')?.textContent?.toLowerCase().includes('saved')", "browser SD save", 60_000).catch(()=>null);

  const previousWrapperTimeOrigin = (await evalPage("performance.timeOrigin")).value;
  await cdp("Page.reload", {ignoreCache: true});
  stage("saved card; reloading wrapper");
  // Page.reload returns before the old document is replaced. Require a new
  // navigation time origin first so we cannot mistake the old complete page
  // for the reloaded document while the new page is still loading.
  await waitFor("performance.timeOrigin !== " + JSON.stringify(previousWrapperTimeOrigin), "new wrapper document after reload", 30_000);
  await waitFor("document.readyState === 'complete' && crossOriginIsolated && document.querySelector('#device-model') && document.querySelector('#custom-bin')", "complete isolated wrapper reload");
  await evalPage(`(() => { const s=document.querySelector('#device-model'); s.value=${JSON.stringify(device)}; s.dispatchEvent(new Event('change',{bubbles:true})); })()`);
  const reloadFile = await setFile("#custom-bin", firmware, true);
  state.reloadFileSelection = reloadFile;
  await waitFor("(() => { const c=document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''; return /app=1\\.6\\.6\\+b961c1f7/.test(c) && /render_ready/.test(c); })()", "verified Nooir firmware boot after reload", 120_000);
  await waitFor("(() => { const c=document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''; return c.length>0 && /ESP-ROM|render_ready|Guru Meditation|panic/i.test(c); })()", "fresh engine serial output after reload", 30_000);
  await waitFor("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('iframe')?.contentDocument?.querySelector('#canvas')", "reboot after reload", 120_000);
  screen = await ready("browser reload");
  await installInputTrace();
  stage("emulator restarted after reload; checking SD and progress");
  card = (await evalPage(`(() => { const d=document.querySelector('#emulator-engine').contentDocument; return {fs:d.querySelector('#sd-info')?.textContent,files:[...d.querySelectorAll('#sd-rows tr[data-name]')].map(r=>r.dataset.name)}; })()`)).value;
  state.sdRestoredAfterReload = /FAT32/i.test(card.fs||'') && (card.files.some(n=>/\.epub$/i.test(n))||card.files.includes('books'));
  state.acceptance.sdPersistenceAfterReload = state.sdRestoredAfterReload;
  if (!state.sdRestoredAfterReload) throw new Error(`SD contents did not restore after wrapper reload: ${JSON.stringify(card)}`);
  const progressComparison = await evalPage(`(async()=>{
    const reference=sessionStorage.getItem('nooir-browser-smoke-progress-reference');
    const c=document.querySelector('#emulator-engine').contentDocument.querySelector('iframe').contentDocument.querySelector('#canvas');
    if(!reference||!c)return {available:false};
    const image=new Image(); image.src=reference; await image.decode();
    if(image.width!==c.width||image.height!==c.height)return {available:true,passed:false,reason:'panel geometry changed'};
    const a=document.createElement('canvas'),b=document.createElement('canvas'); a.width=b.width=c.width; a.height=b.height=c.height;
    const ac=a.getContext('2d'),bc=b.getContext('2d'); ac.drawImage(image,0,0); bc.drawImage(c,0,0);
    const x=ac.getImageData(0,0,c.width,c.height).data,y=bc.getImageData(0,0,c.width,c.height).data;
    let different=0,total=c.width*c.height;
    for(let i=0;i<x.length;i+=4)if(Math.max(Math.abs(x[i]-y[i]),Math.abs(x[i+1]-y[i+1]),Math.abs(x[i+2]-y[i+2]))>8)different++;
    const fraction=different/total; return {available:true,differentPixels:different,totalPixels:total,differentFraction:fraction,passed:fraction<0.02};
  })()`);
  state.progressVisualComparison = progressComparison.value;
  state.screenshots.recentProgressAfterReload = await screenshot(`${device}-recent-progress-after-reload.png`);
  state.firmwareSha256 = createHash('sha256').update(fs.readFileSync(firmware)).digest('hex');
  state.serial = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value;
  state.inputTrace = await currentInputTrace();
  screen = await pressWhenSettled('Confirm', {activity: 'EpubReader'});
  await waitForReader("reader reopened after reload");
  state.readerRestoredPosition = await waitForReaderQuiescent("Reader progress restoration after reload");
  screen = await stableCanvas();
  state.pageRestoreAfterReloadDifference = await canvasDifference(forwardPage);
  state.pageRestoredAfterReload = state.pageRestoreAfterReloadDifference.available && state.pageRestoreAfterReloadDifference.differentFraction < 0.02;
  state.screenshots.readerRestored = await screenshot(`${device}-reader-restored.png`);
  state.progressRestoredAfterReload = Boolean(state.pageRestoredAfterReload && progressComparison.value.passed && /boot_load=valid count=1/.test(state.serial));
  state.acceptance.progressPersistenceAfterReload = state.progressRestoredAfterReload;
  if (!state.progressRestoredAfterReload) throw new Error(`The saved Reader page did not return after browser reload: ${JSON.stringify(state.pageRestoreAfterReloadDifference)}`);
  await pressWhenSettled('Back', {activity: 'RecentBooks'});
  state.screenshots.readerRestoredExit = await screenshot(`${device}-reader-restored-exit.png`);
  state.serial = (await evalPage("document.querySelector('#emulator-engine')?.contentDocument?.querySelector('#console')?.textContent||''")).value;
  fs.writeFileSync(path.join(evidence, 'serial.log'), state.serial);
  state.result = Object.values(state.acceptance).every(Boolean) && state.progressRestoredAfterReload && state.sdRestoredAfterReload && state.pageBackwardRestoredFirst && state.pageForwardAfterBackConfirmed ? 'PASS' : 'FAIL';
  fs.writeFileSync(path.join(evidence, 'result.json'), JSON.stringify(state, null, 2));
  console.log(JSON.stringify({result: state.result, device, sdRestoredAfterReload: state.sdRestoredAfterReload, progressRestoredAfterReload: state.progressRestoredAfterReload, screenshotExport: state.screenshotExport, firmwareSha256: state.firmwareSha256, evidence}, null, 2));
  ws.close();
  if (state.result !== 'PASS') process.exitCode = 1;
} catch (error) {
  let pageState = null;
  try {
    const current = await evalPage(`JSON.stringify((()=>{
      const e=document.querySelector('#emulator-engine'); let innerFrame=null,engineSerial='';
      try { const d=e?.contentDocument; innerFrame=d?.querySelector('iframe')?.src||null; engineSerial=d?.querySelector('#console')?.textContent||''; } catch {}
      const file=document.querySelector('#custom-bin');
      return {
        input:e?.contentWindow?.__nooirInputTrace||[],
        serial:engineSerial,
        wrapper:{url:location.href,readyState:document.readyState,crossOriginIsolated,device:document.querySelector('#device-model')?.value||null,
          releaseVersion:document.querySelector('#release-version')?.textContent||null,firmwareStatus:document.querySelector('#firmware-status')?.textContent||null,
          uploadNote:document.querySelector('#upload-note')?.textContent||null,runtimeNote:document.querySelector('#runtime-note')?.textContent||null,
          customFiles:[...(file?.files||[])].map(f=>({name:f.name,size:f.size})),engineSrc:e?.getAttribute('src')||null,engineLoaded:Boolean(e?.contentDocument),
          innerFrame,consoleLength:engineSerial.length}
      };
    })())`);
    pageState = JSON.parse(current.value);
  } catch {}
  fs.writeFileSync(path.join(evidence, 'failure-context.json'), JSON.stringify({state, pageState}, null, 2));
  fs.writeFileSync(path.join(evidence, 'failure.txt'), String(error?.stack || error));
  try { ws.close(); } catch {}
  throw error;
}
