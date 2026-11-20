"""Guard the isolated, EPUB-only page-aware BLE experiment."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
POLICY = (ROOT / "src/util/BleMemoryPolicy.h").read_text(encoding="utf-8")
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
READER = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class EpubPageAwarePolicyContract(unittest.TestCase):
    def test_experiment_is_a_separate_spiarray_profile(self):
        self.assertIn("[env:gh_release_spiarray_ble_epub_pageaware_20261106_01]", PLATFORMIO)
        self.assertIn("extends = env:gh_release_spiarray_ble_resourcediag_02", PLATFORMIO)
        self.assertIn("-DNOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT=1", PLATFORMIO)
        self.assertIn("-DUSE_SPI_ARRAY_TRANSFER=1", PLATFORMIO)

    def test_page_aware_admission_requires_quiescence_known_work_and_measured_reserves(self):
        body = function_body(POLICY, "constexpr bool readerBleEpubPageAwareReadmissionAdmitted(")
        self.assertIn("!quiescentEpubReturn || !nextCachedPageKnown", body)
        self.assertIn("kReaderBleHostStartFreeReserve", body)
        self.assertIn("kReaderBleHostStartLargestReserve", body)
        self.assertIn("kReaderBleEpubPageAwareRecoveryFreeFloor", body)
        self.assertIn("kReaderBleEpubPageAwareRecoveryLargestFloor", body)
        self.assertIn("kReaderBleEpubPageAwareFreeMargin", body)
        self.assertIn("kReaderBleEpubPageAwareLargestMargin", body)

    def test_connected_cached_render_uses_recent_measured_demand_only(self):
        body = function_body(READER, "reader_memory::RenderBudget EpubReaderActivity::cachedRenderBudget(")
        experimental = body.split("#endif", 1)[0]
        self.assertIn("bleinput::bluetoothEnabled() && BleHid.isRunning()", experimental)
        self.assertIn("canUseRecentCachedPageDemand()", experimental)
        self.assertIn("observedCachedPageTurnBudget", experimental)
        self.assertIn("kReaderBleEpubPageAwareRecoveryFreeFloor", experimental)
        self.assertIn("kReaderBleEpubPageAwareRecoveryLargestFloor", experimental)
        self.assertNotIn("FontCacheManager::MAX_SCAN_FONTS", experimental)
        self.assertIn("cachedRenderObservedPeakBytes", body)
        self.assertIn("readerLargestRenderRequestBytes", body)

    def test_cached_turn_budget_preserves_existing_recovery_floors(self):
        body = function_body((ROOT / "src/util/ReaderRenderBudget.h").read_text(encoding="utf-8"),
                             "constexpr RenderBudget observedCachedPageTurnBudget(")
        self.assertIn("recoveryFreeFloorBytes", body)
        self.assertIn("recoveryLargestFloorBytes", body)
        self.assertIn("saturatingAdd(recoveryFreeFloorBytes, observedPeakBytes)", body)
        self.assertIn("saturatingAdd(recoveryLargestFloorBytes, largestRequestBytes)", body)

    def test_readmission_uses_raw_measured_page_turn_and_not_static_cache_capacity(self):
        body = function_body(READER, "void EpubReaderActivity::reportReaderQuiescent(")
        self.assertIn("setReaderBlePageAwareRenderDemand(nextPeakBytes, nextLargestRequestBytes, knownNextPage)", body)
        self.assertIn("stage=next_page_estimate", body)
        cleanup = function_body(READER, "void EpubReaderActivity::runBleReturnCleanupIfNeeded()")
        self.assertIn("stage=readmission mode=%s", cleanup)
        self.assertIn("pageAwareRequiredFree", cleanup)
        self.assertIn("pageAwareRequiredLargest", cleanup)

    def test_runtime_exception_is_confined_to_quiescent_epub_return(self):
        body = function_body(BLE_INPUT, "bool connectionAdmissionAllowed()")
        branch = body.index("#if NOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT")
        end = body.index("#endif", branch)
        experimental = body[branch:end]
        self.assertIn("if (quiescentEpubReturn", experimental)
        self.assertIn("readerBleEpubPageAwareReadmissionAdmitted", experimental)
        self.assertIn("readerBleQuiescentReadmissionAdmitted", body[end:])

    def test_next_page_requires_completed_adjacent_page_and_recent_render(self):
        body = function_body(READER, "bool EpubReaderActivity::hasKnownNextCachedPage() const")
        self.assertIn("section->isBuilding()", body)
        self.assertIn("firstStableRenderComplete", body)
        self.assertIn("cachedRenderRecentValid", body)
        self.assertIn("section->pageCount < 2", body)
        self.assertIn("section->currentPage + 1 < section->pageCount", body)

    def test_successful_quiescent_page_can_bypass_only_with_full_projection(self):
        body = function_body(POLICY, "constexpr bool readerBleEpubImmediateReadmissionAllowed(")
        for condition in (
            "bluetoothEnabled", "returnPending", "!bleRunning", "readerQuiescent",
            "demandEstimateAvailable", "!pageTurnPending", "!bookReadmissionBlocked",
            "admissionPasses",
        ):
            self.assertIn(condition, body)
        self.assertIn("kReaderBleSurvivalBackoffInitialMs", POLICY)
        self.assertIn("kReaderBleSurvivalBackoffMaxMs", POLICY)
        reader_body = function_body(READER, "void EpubReaderActivity::reportReaderQuiescent(")
        self.assertNotIn("signalCooldownActive", reader_body)
        self.assertIn("stage=survival_backoff_bypassed reason=measured_headroom", reader_body)
        self.assertIn("activityManager.hasPendingRenderWork()", reader_body)
        cleanup = function_body(READER, "void EpubReaderActivity::runBleReturnCleanupIfNeeded()")
        self.assertIn("if (!immediateAdmission)", cleanup)
        self.assertIn("admissionBeforeCleanup", cleanup)
        self.assertIn("blockReaderBleReturnRetry()", cleanup)
        self.assertNotIn("releaseSdFontCaches()", cleanup.split("if (!immediateAdmission)", 1)[1].split("readerBleReturnCleanupAttempted = true", 1)[0])

    def test_page_aware_render_measurement_is_skipped_when_ble_is_off(self):
        body = function_body(READER, "void EpubReaderActivity::checkReaderSurvival()")
        gated = body.index("if (bleinput::bluetoothEnabled()) {")
        recent = body.index("cachedRenderRecentPeakBytes.store")
        self.assertLess(gated, recent)

    def test_existing_survival_and_preflight_paths_remain(self):
        self.assertIn('requestReaderSurvivalStop("font_prewarm", true)', READER)
        self.assertIn('requestReaderSurvivalStop("render_preflight", true)', READER)
        self.assertIn("pendingPageTurns.enqueue", READER)


if __name__ == "__main__":
    unittest.main()
