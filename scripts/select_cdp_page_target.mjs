export const MAX_CDP_TARGETS_TO_SCAN = 200;

function parseHttpUrl(value) {
  if (typeof value !== "string" || value.trim() === "") return null;
  try {
    const parsed = new URL(value);
    return parsed.protocol === "http:" || parsed.protocol === "https:" ? parsed : null;
  } catch {
    return null;
  }
}

function isWebSocketDebuggerUrl(value) {
  if (typeof value !== "string" || value.trim() === "") return false;
  try {
    const parsed = new URL(value);
    return (parsed.protocol === "ws:" || parsed.protocol === "wss:") && Boolean(parsed.hostname);
  } catch {
    return false;
  }
}

export function selectCdpPageTarget(targets, requestedPageUrl) {
  const requested = parseHttpUrl(requestedPageUrl);
  if (!requested) throw new TypeError("Requested smoke page URL must be an absolute HTTP(S) URL.");

  const diagnostics = {
    scanned: 0,
    pageTargets: 0,
    emptyUrl: 0,
    malformedUrl: 0,
    unrelatedOrigin: 0,
    invalidWebSocketUrl: 0,
    invalidTargetList: !Array.isArray(targets),
    truncated: false,
  };
  if (!Array.isArray(targets)) return {target: null, diagnostics};

  const limit = Math.min(targets.length, MAX_CDP_TARGETS_TO_SCAN);
  diagnostics.truncated = targets.length > limit;
  for (let i = 0; i < limit; i += 1) {
    diagnostics.scanned += 1;
    const item = targets[i];
    if (!item || item.type !== "page") continue;
    diagnostics.pageTargets += 1;

    if (typeof item.url !== "string" || item.url.trim() === "") {
      diagnostics.emptyUrl += 1;
      continue;
    }
    const candidateUrl = parseHttpUrl(item.url);
    if (!candidateUrl) {
      diagnostics.malformedUrl += 1;
      continue;
    }
    if (candidateUrl.origin !== requested.origin) {
      diagnostics.unrelatedOrigin += 1;
      continue;
    }
    if (!isWebSocketDebuggerUrl(item.webSocketDebuggerUrl)) {
      diagnostics.invalidWebSocketUrl += 1;
      continue;
    }
    return {target: item, diagnostics};
  }
  return {target: null, diagnostics};
}

export function formatCdpTargetDiagnostics(diagnostics) {
  const count = value => Number.isSafeInteger(value) ? Math.max(0, Math.min(value, MAX_CDP_TARGETS_TO_SCAN)) : 0;
  return "No matching Chrome page target. scanned=" + count(diagnostics?.scanned) +
    "; pages=" + count(diagnostics?.pageTargets) +
    "; empty_url=" + count(diagnostics?.emptyUrl) +
    "; malformed_url=" + count(diagnostics?.malformedUrl) +
    "; unrelated_origin=" + count(diagnostics?.unrelatedOrigin) +
    "; invalid_websocket_url=" + count(diagnostics?.invalidWebSocketUrl) +
    "; truncated=" + Boolean(diagnostics?.truncated) +
    "; raw target URLs and IDs omitted.";
}
