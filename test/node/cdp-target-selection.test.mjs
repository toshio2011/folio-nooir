import test from "node:test";
import assert from "node:assert/strict";
import {
  formatCdpTargetDiagnostics,
  selectCdpPageTarget,
  waitForCdpPageTarget,
} from "../../scripts/select_cdp_page_target.mjs";

const requested = "http://127.0.0.1:8178/";
const goodTarget = (url = requested) => ({
  type: "page",
  url,
  webSocketDebuggerUrl: "ws://127.0.0.1:9224/devtools/page/test-id",
});

const noDelay = async () => {};

test("selects a valid same-origin page after an empty initial Chrome target", () => {
  const expected = goodTarget();
  const result = selectCdpPageTarget([
    {type: "page", url: "", webSocketDebuggerUrl: "ws://127.0.0.1:9224/devtools/page/blank"},
    expected,
  ], requested);
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.emptyUrl, 1);
});

test("ignores missing and malformed page URLs without throwing", () => {
  const expected = goodTarget();
  const result = selectCdpPageTarget([
    {type: "page", webSocketDebuggerUrl: "ws://127.0.0.1:9224/devtools/page/missing"},
    goodTarget("http://["),
    expected,
  ], requested);
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.emptyUrl, 1);
  assert.equal(result.diagnostics.malformedUrl, 1);
});

test("ignores unrelated origins and non-page targets", () => {
  const expected = goodTarget();
  const result = selectCdpPageTarget([
    goodTarget("https://example.invalid/"),
    {...goodTarget(), type: "service_worker"},
    expected,
  ], requested);
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.unrelatedOrigin, 1);
  assert.equal(result.diagnostics.pageTargets, 2);
});

test("requires a valid WebSocket debugger endpoint", () => {
  const expected = goodTarget();
  const result = selectCdpPageTarget([
    {...goodTarget(), webSocketDebuggerUrl: "http://127.0.0.1:9224/devtools/page/bad"},
    expected,
  ], requested);
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.invalidWebSocketUrl, 1);
});

test("no-match diagnostics are bounded and omit target URLs and IDs", () => {
  const targets = Array.from({length: 1000}, (_, i) => ({
    type: "page", url: "", id: "private-target-" + i,
    webSocketDebuggerUrl: "ws://127.0.0.1:9224/devtools/page/" + i,
  }));
  const result = selectCdpPageTarget(targets, requested);
  assert.equal(result.target, null);
  assert.equal(result.diagnostics.scanned, 200);
  assert.equal(result.diagnostics.truncated, true);
  const message = formatCdpTargetDiagnostics(result.diagnostics);
  assert.ok(message.length < 400);
  assert.doesNotMatch(message, /private-target|127\.0\.0\.1|devtools/);
});

test("rejects an invalid requested page URL with a clear error", () => {
  assert.throws(() => selectCdpPageTarget([goodTarget()], ""), /absolute HTTP\(S\) URL/);
});

test("polls past empty initial URLs until the same-origin page appears", async () => {
  let polls = 0;
  let sleeps = 0;
  const expected = goodTarget();
  const result = await waitForCdpPageTarget(async () => {
    polls += 1;
    if (polls === 1) return [{type: "page", url: ""}];
    if (polls === 2) return [{type: "page", url: "about:blank"}];
    return [expected];
  }, requested, {maxAttempts: 4, pollIntervalMs: 0, sleep: async () => { sleeps += 1; }});
  assert.equal(result.target, expected);
  assert.equal(polls, 3);
  assert.equal(sleeps, 2);
  assert.equal(result.diagnostics.emptyUrl, 1);
  assert.equal(result.diagnostics.malformedUrl, 1);
});

test("bounded permanent empty-target failure reports counts, not raw data", async () => {
  let polls = 0;
  let sleeps = 0;
  const result = await waitForCdpPageTarget(async () => {
    polls += 1;
    return [{type: "page", url: "", id: "private-id"}];
  }, requested, {maxAttempts: 3, pollIntervalMs: 0, sleep: async () => { sleeps += 1; }});
  assert.equal(result.target, null);
  assert.equal(polls, 3);
  assert.equal(sleeps, 2);
  assert.equal(result.diagnostics.attempts, 3);
  assert.equal(result.diagnostics.emptyUrl, 3);
  const message = formatCdpTargetDiagnostics(result.diagnostics);
  assert.match(message, /attempts=3/);
  assert.doesNotMatch(message, /private-id|127\.0\.0\.1|devtools/);
});

test("polling never accepts a wrong-origin target before a valid target appears", async () => {
  let polls = 0;
  const expected = goodTarget();
  const result = await waitForCdpPageTarget(async () => {
    polls += 1;
    return polls === 1 ? [goodTarget("https://example.invalid/")] : [expected];
  }, requested, {maxAttempts: 3, pollIntervalMs: 0, sleep: noDelay});
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.unrelatedOrigin, 1);
});

test("polling permanently rejects wrong-origin targets", async () => {
  const result = await waitForCdpPageTarget(async () => [goodTarget("https://example.invalid/")], requested,
    {maxAttempts: 2, pollIntervalMs: 0, sleep: noDelay});
  assert.equal(result.target, null);
  assert.equal(result.diagnostics.unrelatedOrigin, 2);
});

test("polling tolerates malformed targets and selects only a valid URL", async () => {
  let polls = 0;
  const expected = goodTarget();
  const result = await waitForCdpPageTarget(async () => {
    polls += 1;
    return polls === 1 ? [goodTarget("http://[")] : [expected];
  }, requested, {maxAttempts: 2, pollIntervalMs: 0, sleep: noDelay});
  assert.equal(result.target, expected);
  assert.equal(result.diagnostics.malformedUrl, 1);
});

test("polling bounds attempts and keeps fetch errors redacted", async () => {
  let polls = 0;
  const result = await waitForCdpPageTarget(async () => {
    polls += 1;
    throw new Error("secret-target-url");
  }, requested, {maxAttempts: 2, pollIntervalMs: 0, sleep: noDelay});
  assert.equal(result.target, null);
  assert.equal(polls, 2);
  assert.equal(result.diagnostics.fetchErrors, 2);
  assert.doesNotMatch(formatCdpTargetDiagnostics(result.diagnostics), /secret-target-url/);
});
