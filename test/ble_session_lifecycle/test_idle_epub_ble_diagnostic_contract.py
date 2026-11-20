from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BLE = (ROOT / "src" / "BleInput.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
EPUB = (ROOT / "src" / "activities" / "reader" / "EpubReaderActivity.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src" / "util" / "BleMemoryPolicy.h").read_text(encoding="utf-8")


class IdleEpubBleDiagnosticContract(unittest.TestCase):
    def test_idle_budget_is_separate_from_simultaneous_render_budget(self):
        self.assertIn("kReaderBleHostStartFreeReserve + kReaderBleEpubPageAwareRecoveryFreeFloor;", POLICY)
        self.assertIn("kReaderBleEpubIdleOnlyRequiredLargest =\n    kReaderBleStartMinLargestBlock;", POLICY)
        self.assertIn("readerBleEpubIdleOnlyPostStartSafe", POLICY)
        self.assertNotRegex(POLICY.split("kReaderBleEpubIdleOnlyRequiredFree =", 1)[1].split(";", 1)[0],
                            r"PageAwareFreeMargin|cachedRender|RenderMin")
        self.assertNotRegex(POLICY.split("kReaderBleEpubIdleOnlyRequiredLargest =", 1)[1].split(";", 1)[0],
                            r"PageAwareLargestMargin|cachedRender|RenderMin")
        self.assertIn("freeHeap >= kReaderBleEpubIdleOnlyRequiredFree", POLICY)
        self.assertIn("largestBlock >= kReaderBleEpubIdleOnlyRequiredLargest", POLICY)

    def test_sequential_mode_arms_automatically_after_a_stable_epub_page(self):
        loop = EPUB.split("void EpubReaderActivity::loop()", 1)[1].split(
            "void EpubReaderActivity::", 1)[0]
        self.assertIn("armEpubIdleBleDiagnostic()", loop)
        self.assertIn("firstStableRenderComplete.load", loop)
        self.assertIn("!bluetoothResourceSensitive()", loop)
        self.assertIn("No serial command is required", loop)
        self.assertIn('cmd == "EPUB_IDLE_BLE_DISARM"', MAIN)
        self.assertIn('cmd == "EPUB_IDLE_BLE_STATUS"', MAIN)
        self.assertIn("!SETTINGS.bluetoothEnabled", BLE)
        self.assertIn("activityManager.bluetoothResourceSensitive() || RenderLock::peek()", BLE)
        self.assertIn("consumeEpubIdleBleDiagnosticPermit()", BLE)
        self.assertIn("g_epubIdleBleDiagnosticFault.load", BLE)
        self.assertIn("signalReaderBleReturnLifecycleEvent(\"idle_diag_arm\")", BLE)

    def test_fault_does_not_fall_back_to_strict_admission(self):
        gate = BLE.split("bool connectionAdmissionAllowed()", 1)[1].split("bool setBluetoothEnabled", 1)[0]
        self.assertLess(gate.index("epubIdleBleDiagnosticFaulted()"), gate.index("if (BleHid.isRunning()) return true;"))
        self.assertIn("return admitted;", gate)
        self.assertIn("return false;", gate)

    def test_runtime_floor_fault_is_separate_and_rearmed_only_after_safe_render(self):
        self.assertIn("g_epubIdleBleRuntimeFloorFault{false}", BLE)
        self.assertIn("g_epubIdleBleDiagnosticFault.load(std::memory_order_acquire) ||", BLE)
        self.assertIn("g_epubIdleBleRuntimeFloorFault.compare_exchange_strong", BLE)
        self.assertIn("g_epubIdleBleDiagnosticFault.store(true", BLE)  # host-start/post-start failures stay hard faults
        report = EPUB.split("void EpubReaderActivity::reportReaderQuiescent(", 1)[1].split(
            "void EpubReaderActivity::", 1)[0]
        self.assertIn("clearEpubIdleBleRuntimeFloorFaultAfterRender(true, readerQuiescent)", report)
        self.assertLess(report.index("clearEpubIdleBleRuntimeFloorFaultAfterRender"),
                        report.index("signalReaderBleReturnLifecycleEvent(eventReason)"))
        self.assertIn("BleHid.isRunning(), stopPending()", BLE.split(
            "bool clearEpubIdleBleRuntimeFloorFaultAfterRender", 1)[1].split("bool epubIdleBleDiagnosticPermitPending", 1)[0])

    def test_new_render_generation_rearms_one_shot_cleanup_after_runtime_fault(self):
        cleanup = EPUB.split("void EpubReaderActivity::runBleReturnCleanupIfNeeded()", 1)[1].split(
            "bool EpubReaderActivity::processPendingPageTurn()", 1)[0]
        self.assertIn("epubIdleBleDiagnosticArmed() && bleinput::epubIdleBleDiagnosticFaulted()", cleanup)
        self.assertIn("returnGeneration != readerBleReturnCleanupGeneration", cleanup)
        self.assertIn("readerBleReturnCleanupAttempted = false", cleanup)
        self.assertIn("attempt_limit=one_per_generation", cleanup)
        self.assertIn("stage=runtime_recovery_rearmed", EPUB)

    def test_progressive_cleanup_uses_existing_tiers_and_rechecks_between(self):
        cleanup = EPUB.split("void EpubReaderActivity::runBleReturnCleanupIfNeeded()", 1)[1].split(
            "bool EpubReaderActivity::processPendingPageTurn()", 1)[0]
        self.assertIn("if (bleinput::epubIdleBleDiagnosticArmed())", cleanup)
        self.assertIn("readerBleEpubIdleOnlyAdmissionAdmitted", cleanup)
        self.assertIn("releaseUiFallbackCaches", cleanup)
        self.assertIn("releaseCssParserRetainedStorage", cleanup)
        self.assertIn("releaseReaderScanStorage", cleanup)
        self.assertIn("releaseGrayscaleStripScratch", cleanup)
        self.assertIn("releaseReaderCaches", cleanup)
        self.assertIn("admitReaderBleIfReady", cleanup)
        self.assertIn("attempt_limit=one_per_generation", cleanup)

    def test_readmission_log_reports_the_selected_diagnostic_budget(self):
        cleanup = EPUB.split("void EpubReaderActivity::runBleReturnCleanupIfNeeded()", 1)[1].split(
            "bool EpubReaderActivity::processPendingPageTurn()", 1)[0]
        self.assertIn('idleDiagnosticMode ? "idle_sequential"', cleanup)
        self.assertIn("static_cast<unsigned>(requiredFree)", cleanup)
        self.assertIn("static_cast<unsigned>(requiredLargest)", cleanup)
        self.assertNotIn("predictionRequiredFree", cleanup)
        self.assertNotIn("predictionRequiredLargest", cleanup)

    def test_host_start_and_connected_idle_have_fail_closed_floors(self):
        self.assertIn("stage=idle_start_host_ready", BLE)
        self.assertIn("heap_caps_monitor_local_minimum_free_size_start()", BLE)
        self.assertIn("min_free_during_start=%u", BLE)
        self.assertIn("stage=runtime_floor_fault", BLE)
        self.assertIn("stopForReaderResourcePressure();", BLE)
        self.assertIn("kReaderBleEpubPageAwareRecoveryFreeFloor", BLE)
        self.assertIn("kReaderBleEpubPageAwareRecoveryLargestFloor", BLE)
        self.assertIn("readerBleEpubIdleOnlyPostStartSafe(afterFree, afterLargest)", BLE)
        self.assertIn("readerBleEpubIdleOnlyPostStartSafe(freeHeap, largest)", BLE)
        self.assertIn("stage=stop_ack", BLE)
        self.assertIn("stage=stop_begin", BLE)
        self.assertIn("recovered=%u/%u", BLE)
        self.assertIn("stage=hid_ready", BLE)

    def test_received_command_is_drained_before_diagnostic_shutdown(self):
        loop = MAIN.split("void loop()", 1)[1]
        self.assertLess(loop.index("mappedInputManager.pollBle();"), loop.index("updateBluetoothLifecycle();"))
        self.assertIn("bleInputDrainedBeforeLifecycle", loop)
        self.assertIn("stage=turn_capture", EPUB)
        self.assertIn("stage=turn_execute", EPUB)
        self.assertIn("stage=turn_render_complete", EPUB)
        self.assertIn("fontCache->resetStats()", EPUB)
        self.assertIn("font_largest_request=%u font_failed_allocations=%u", EPUB)
        self.assertIn('fontCache->logStats("epub_idle_turn")', EPUB)
        self.assertIn('stage=turn_font_measure_begin', EPUB)

    def test_heavy_render_waits_for_start_or_confirmed_stop(self):
        self.assertIn("epubIdleBleDiagnosticStarting()", EPUB)
        self.assertIn("readerRenderWaitingForBleStart", EPUB)
        self.assertIn("if (readerRenderWaitingForBleStart && !bleinput::epubIdleBleDiagnosticStarting())", EPUB)
        self.assertIn("if (readerRenderWaitingForBleStop && !BleHid.isRunning())", EPUB)

    def test_unmeasured_page_preflight_yields_before_page_allocations(self):
        preflight = EPUB.split("bool EpubReaderActivity::cachedRenderPreflight", 1)[1].split(
            "bool EpubReaderActivity::cachedRenderFinalPassPreflight", 1)[0]
        exact = EPUB.split("bool EpubReaderActivity::hasExactRecentCachedPageDemand() const", 1)[1].split(
            "bool EpubReaderActivity::hasKnownNextCachedPage() const", 1)[0]
        self.assertIn("cachedRenderRecentPage.load(std::memory_order_acquire) == section->currentPage", exact)
        self.assertNotIn("return distance <= 1", exact)
        self.assertLess(preflight.index("!hasExactRecentCachedPageDemand()"), preflight.index("cachedRenderBudget("))
        self.assertIn('requestReaderSurvivalStop("unmeasured_page", true)', preflight)
        self.assertIn("return false;", preflight)
        self.assertIn("stage=turn_capture", EPUB)
        self.assertIn("stage=turn_execute", EPUB)
        self.assertIn("readerRenderWaitingForBleStop = true", EPUB)
        self.assertIn("!BleHid.isRunning()", EPUB.split("void EpubReaderActivity::runBleReturnCleanupIfNeeded()", 1)[1])

    def test_ble_off_keeps_existing_reader_path(self):
        cleanup = EPUB.split("void EpubReaderActivity::runBleReturnCleanupIfNeeded()", 1)[1].split(
            "bool EpubReaderActivity::processPendingPageTurn()", 1)[0]
        cache_preflight = EPUB.split("bool EpubReaderActivity::cachedRenderPreflight", 1)[1].split(
            "void EpubReaderActivity::reportReaderQuiescent", 1)[0]
        render = EPUB.split("void EpubReaderActivity::render(RenderLock&& lock)", 1)[1]
        self.assertIn("if (!bleinput::bluetoothEnabled() || WiFi.getMode() != WIFI_MODE_NULL) return;", cleanup)
        self.assertIn("bleinput::bluetoothEnabled() && bleinput::epubIdleBleDiagnosticStarting()", render)
        self.assertIn("if (!bleinput::bluetoothEnabled()) return true;", cache_preflight)
        self.assertIn("const bool idlePrewarmBleSafe", EPUB)
        self.assertIn("!bleinput::bluetoothEnabled() ||", EPUB)
        self.assertIn("!BleHid.isRunning() && !bleinput::stopPending()", EPUB)
        self.assertIn("#else\n  if (BleHid.isRunning()) BleHid.poll();\n#endif", MAIN)
        self.assertIn("#else\n  mappedInputManager.pollBle();\n#endif", MAIN)
        disable = BLE.split("bool setBluetoothEnabled(const bool enabled)", 1)[1].split("void toggleBluetooth", 1)[0]
        self.assertIn('disarmEpubIdleBleDiagnostic("bluetooth_off")', disable)
        self.assertIn("stop();", disable)

    def test_xtc_streaming_admission_is_format_scoped(self):
        self.assertIn('strcmp(activityName, "XtcReader") == 0', BLE)
        self.assertIn("readerBleXtcStartMemoryAdmitted", BLE)
        self.assertIn("kXtcBleStartMinFreeHeap = kReaderBleStartMinFreeHeap - 144", POLICY)
        self.assertIn("kReaderBleStartMinLargestBlock", BLE)
        ensure = BLE.split("bool ensureStarted()", 1)[1].split("void stopForReaderResourcePressure()", 1)[0]
        self.assertIn('rule=%s host_running=%d', ensure)
        self.assertIn('xtcStreamingAdmission ? "xtc_stream" : "default"', ensure)
        self.assertIn('const size_t coldMinFree = xtcStreamingAdmission', ensure)

    def test_idle_return_retry_occurs_only_after_budget_passes(self):
        report = EPUB.split("if (completedRender && bleinput::bluetoothEnabled() && bleinput::readerBleReturnPending())", 1)[1]
        self.assertIn("readerBleEpubIdleOnlyAdmissionAdmitted", report)
        self.assertIn("idleDiagnosticMode || knownNextPage", report)
        self.assertIn("admissionPasses", report)


if __name__ == "__main__":
    unittest.main()
