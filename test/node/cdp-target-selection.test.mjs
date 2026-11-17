import test from "node:test";
import assert from "node:assert/strict";
import {formatCdpTargetDiagnostics, selectCdpPageTarget} from "../../scripts/select_cdp_page_target.mjs";

const requested = "http://127.0.0.1:8178/";
const goodTarget = (url = requested) => ({
  type: "page",
  url,
  webSocketDebuggerUrl: "ws://127.0.0.1:9224/devtools/page/test-id",
});

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
  assert.ok(message.length < 300);
  assert.doesNotMatch(message, /private-target|127\.0\.0\.1|devtools/);
});

test("rejects an invalid requested page URL with a clear error", () => {
  assert.throws(() => selectCdpPageTarget([goodTarget()], ""), /absolute HTTP\(S\) URL/);
});
