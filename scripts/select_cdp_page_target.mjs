export const MAX_CDP_TARGETS_TO_SCAN = 200;
export const MAX_CDP_TARGET_POLL_ATTEMPTS = 40;

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

const sleepFor = milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds));

function addCounts(total, current) {
  total.scanned += current.scanned;
  total.pageTargets += current.pageTargets;
  total.emptyUrl += current.emptyUrl;
  total.malformedUrl += current.malformedUrl;
  total.unrelatedOrigin += current.unrelatedOrigin;
  total.invalidWebSocketUrl += current.invalidWebSocketUrl;
  total.invalidTargetLists += Number(current.invalidTargetList);
  total.truncated ||= current.truncated;
}

/**
 * Poll a caller-provided CDP /json/list fetcher until a validated same-origin
 * page appears. The callback receives an AbortSignal and should pass it to
 * fetch(). Every request has a timeout and the number of attempts is capped.
 * Transport and JSON errors are counted but their messages are not retained.
 */
export async function waitForCdpPageTarget(fetchTargets, requestedPageUrl, options = {}) {
  if (typeof fetchTargets !== "function") throw new TypeError("CDP target fetcher must be a function.");
  if (!parseHttpUrl(requestedPageUrl)) {
    throw new TypeError("Requested smoke page URL must be an absolute HTTP(S) URL.");
  }
  const {
    maxAttempts = 20,
    pollIntervalMs = 200,
    requestTimeoutMs = 500,
    sleep = sleepFor,
  } = options;
  if (!Number.isSafeInteger(maxAttempts) || maxAttempts < 1 || maxAttempts > MAX_CDP_TARGET_POLL_ATTEMPTS) {
    throw new RangeError("CDP target poll attempts must be between 1 and " + MAX_CDP_TARGET_POLL_ATTEMPTS + ".");
  }
  if (!Number.isSafeInteger(pollIntervalMs) || pollIntervalMs < 0 || pollIntervalMs > 1000) {
    throw new RangeError("CDP target poll interval must be between 0 and 1000 ms.");
  }
  if (!Number.isSafeInteger(requestTimeoutMs) || requestTimeoutMs < 1 || requestTimeoutMs > 2000) {
    throw new RangeError("CDP target request timeout must be between 1 and 2000 ms.");
  }
  if (typeof sleep !== "function") throw new TypeError("CDP target poll sleep must be a function.");

  const diagnostics = {
    attempts: 0,
    fetchErrors: 0,
    scanned: 0,
    pageTargets: 0,
    emptyUrl: 0,
    malformedUrl: 0,
    unrelatedOrigin: 0,
    invalidWebSocketUrl: 0,
    invalidTargetLists: 0,
    truncated: false,
  };

  for (let attempt = 0; attempt < maxAttempts; attempt += 1) {
    diagnostics.attempts += 1;
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), requestTimeoutMs);
    try {
      const targets = await fetchTargets(controller.signal);
      const result = selectCdpPageTarget(targets, requestedPageUrl);
      addCounts(diagnostics, result.diagnostics);
      if (result.target) return {target: result.target, diagnostics};
    } catch {
      diagnostics.fetchErrors += 1;
    } finally {
      clearTimeout(timeout);
    }
    if (attempt + 1 < maxAttempts) await sleep(pollIntervalMs);
  }
  return {target: null, diagnostics};
}

export function formatCdpTargetDiagnostics(diagnostics) {
  const maxCount = MAX_CDP_TARGETS_TO_SCAN * MAX_CDP_TARGET_POLL_ATTEMPTS;
  const count = value => Number.isSafeInteger(value) ? Math.max(0, Math.min(value, maxCount)) : 0;
  return "No matching Chrome page target. attempts=" + count(diagnostics?.attempts) +
    "; fetch_errors=" + count(diagnostics?.fetchErrors) +
    "; scanned=" + count(diagnostics?.scanned) +
    "; pages=" + count(diagnostics?.pageTargets) +
    "; empty_url=" + count(diagnostics?.emptyUrl) +
    "; malformed_url=" + count(diagnostics?.malformedUrl) +
    "; unrelated_origin=" + count(diagnostics?.unrelatedOrigin) +
    "; invalid_websocket_url=" + count(diagnostics?.invalidWebSocketUrl) +
    "; invalid_target_lists=" + count(diagnostics?.invalidTargetLists) +
    "; truncated=" + Boolean(diagnostics?.truncated) +
    "; raw target URLs and IDs omitted.";
}
