"""Contract tests for bounded BLE HID report parsing and raw diagnostics."""

from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
METADATA = (ROOT / "src/util/BleHidReportMetadata.h").read_text(encoding="utf-8")
HOST = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text(encoding="utf-8")
MAP_TEST = (ROOT / "test/ble_input_mapping/BleInputMappingTest.cpp").read_text(encoding="utf-8")
GENERIC = (ROOT / "src/util/BleHidGenericLearning.h").read_text(encoding="utf-8")


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


class BleHidReportContract(unittest.TestCase):
    def test_report_id_255_is_not_confused_with_missing_metadata(self):
        body = function_body(METADATA, "inline PayloadView payloadWithoutDeclaredReportId(")
        self.assertIn("!hasReportReference || reportId == 0", body)
        self.assertIn("if (data[0] != reportId)", body)
        self.assertNotIn("reportId == kUnknownReportId", body)
        self.assertIn("0xFF is a valid HID Report ID.", METADATA)

    def test_report_reference_uses_descriptor_length_for_generic_hogp_payloads(self):
        body = function_body(METADATA, "inline PayloadView payloadForDeclaredReport(")
        self.assertIn("if (length == expectedBodyLength)", body)
        self.assertIn("length == expectedBodyLength + 1 && data[0] == reportId", body)
        self.assertIn("if (!hasExpectedBodyLength) return {data, length, false}", body)
        self.assertIn("payloadForDeclaredReport", HOST)
        self.assertIn("expectedInputBytes", HOST)
        self.assertIn("DeclaredReportUsesExactGattBodyLengthBeforeOptionalIdPrefix", MAP_TEST)

    def test_report_map_parser_is_bounded_and_fails_closed(self):
        parser = function_body(METADATA, "inline bool parseReportMap(")
        self.assertIn("kGlobalPushDepth", parser)
        self.assertIn("kReportIdCapacity", METADATA)
        self.assertIn("if (itemSize > length - offset) return false", parser)
        self.assertIn("if (depth != 0", parser)
        self.assertIn("hints.overflow", parser)
        self.assertIn("reportIdHasUnsupportedUsage", METADATA)

    def test_unknown_short_and_malformed_reports_are_not_heuristically_decoded(self):
        decoder = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        self.assertIn("genericInputReportAllowed", decoder)
        self.assertIn("reportMapAllowsUsage", decoder)
        self.assertNotIn("|| n < 7", decoder)
        self.assertIn("n <= 8", decoder)
        self.assertIn("knownInputReportType(reportType, true)", decoder)
        self.assertIn("enqueueRawDiagnostic(raw)", decoder)
        self.assertIn("if (inputPreset_ == InputPreset::YiserJ6Ring && !genericLearningCapture_)", decoder)

    def test_raw_capture_remains_fixed_and_bounded(self):
        self.assertIn("kRawReportRingLen = 16", HOST)
        self.assertIn("RawReportDiagnostic::kMaxPayloadBytes", HOST)
        self.assertIn("raw.truncated = len > RawReportDiagnostic::kMaxPayloadBytes", HOST)
        self.assertIn("memcpy(raw.payload, data, raw.storedLength)", HOST)
        self.assertIn("struct ReportMapHints", METADATA)

    def test_synthetic_fixtures_cover_report_ids_overflow_and_action_targets(self):
        self.assertIn("ReportMapParserRoutesOnlyDeclaredSafeInputReports", MAP_TEST)
        self.assertIn("ReportMapParserRejectsMixedUnknownAndMalformedReports", MAP_TEST)
        self.assertIn("ReportMapParserBoundsReportIdTables", MAP_TEST)
        self.assertIn("EveryConfiguredQuickActionHasABoundedBleTarget", MAP_TEST)

    def test_generic_learning_is_descriptor_bound_and_device_name_independent(self):
        self.assertIn("constexpr uint8_t kMaxFields = 32", GENERIC)
        self.assertIn("kRawFallbackMaxBytes = 16", GENERIC)
        self.assertIn("FieldKind::ButtonBit", GENERIC)
        self.assertIn("FieldKind::Hat", GENERIC)
        self.assertIn("FieldKind::Axis", GENERIC)
        self.assertIn("FieldKind::TouchContact", GENERIC)
        self.assertIn("FieldKind::TouchX", GENERIC)
        self.assertIn("FieldKind::TouchY", GENERIC)
        self.assertIn("report->inputBits + 7) / 8 != length", GENERIC)
        self.assertIn("decodeRawBitFallbackCycle", GENERIC)
        self.assertIn("descriptorHash", GENERIC)
        self.assertIn("learnedIdentityValid", (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h").read_text(encoding="utf-8"))
        body = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        generic = body[body.index("// Generic controller learning"):body.index("// --- Standard keyboard report path")]
        self.assertIn("g_genericInputLayout.valid", generic)
        self.assertIn("hasReportReference", generic)
        self.assertIn("enqueueGenericLearnedIdentity", generic)
        self.assertNotIn("connName_", generic)
        self.assertIn("GenericLearningDecodesUnknownVendorButtonsByDescriptorField", MAP_TEST)
        self.assertIn("GenericLearnedIdentitySequenceIsBoundedAndPersistable", MAP_TEST)
        self.assertIn("GenericLearningAcceptsUnknownUsagePagesByFieldAndReportIdentity", MAP_TEST)
        self.assertIn("GenericTouchNeedsDeclaredTouchCollectionAndEmitsOneReleaseTimeGesture", MAP_TEST)
        self.assertIn("GenericTouchLearnsPhysicalTwentyByteReportsFromDescriptorWithoutDeviceName", MAP_TEST)
        self.assertIn("reportHasTouchGestureFields", GENERIC)

    def test_generic_unknown_descriptor_fallback_is_single_bit_and_bounded(self):
        body = function_body(GENERIC, "inline bool decodeRawBitFallbackCycle(")
        self.assertIn("changedCount != 1", body)
        self.assertIn("kRawFallbackMaxBytes", body)
        self.assertIn("activeBit", body)
        self.assertIn("kRawFallbackMaxGapMs", body)
        self.assertIn("baseline -> press is held pending a matching release", body)
        self.assertIn("GenericRawFallbackRequiresOneBoundedPressReleaseBit", MAP_TEST)

    def test_active_profile_refuses_unexplained_mapping_loss(self):
        mapping = (ROOT / "src/util/BleKeyMapping.h").read_text(encoding="utf-8")
        store = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
        activity = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        self.assertIn("preservesExistingMappings", mapping)
        self.assertIn("active map lost saved inputs; refusing stale profile overwrite", store)
        self.assertIn("saveActive(choice.remove)", activity)

    def test_empty_48_byte_profile_snapshot_is_blocked_but_explicit_removal_is_allowed(self):
        store = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
        activity = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        self.assertIn("EmptyFortyEightByteProfileSnapshotCannotReplacePopulatedMap", MAP_TEST)
        save_active = function_body(store, "bool BleMappingProfileStore::saveActive(")
        self.assertIn("!allowMappingRemoval &&", save_active)
        self.assertLess(save_active.index("!allowMappingRemoval &&"), save_active.index("return writeMap("))
        self.assertIn("saveActive(choice.remove)", activity)
        self.assertIn("{0xFF, 0xFF, StrId::STR_DELETE, true}", activity)

    def test_mapping_screen_reentry_reloads_profile_and_returns_without_stale_save(self):
        activity = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        parent = (ROOT / "src/activities/settings/BleMappingProfilesActivity.cpp").read_text(encoding="utf-8")
        store = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
        on_enter = function_body(activity, "void BleButtonMapActivity::onEnter()")
        on_exit = function_body(activity, "void BleButtonMapActivity::onExit()")
        edit_mappings = function_body(parent, "void BleMappingProfilesActivity::editMappings(")
        switch_to = function_body(store, "bool BleMappingProfileStore::switchTo(")
        self.assertIn("BleMappingProfileStore::loadActive()", on_enter)
        self.assertIn("Step::LoadError", on_enter)
        self.assertIn("mappedInput.setBleCaptureMode(false)", on_exit)
        self.assertIn("reload();", edit_mappings)
        self.assertIn("requestUpdate(true)", edit_mappings)
        self.assertNotIn("saveActive", edit_mappings)
        self.assertIn("readMap(profileId, candidate", switch_to)
        self.assertIn("Do not snapshot", switch_to)
        self.assertIn("No mappings saved yet.", activity)
        self.assertIn("Step::LoadError", activity)

    def test_legacy_preset_does_not_gate_learning_and_labels_are_separate_from_identity(self):
        host_header = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h").read_text(encoding="utf-8")
        manager = (ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")
        mapping = (ROOT / "src/util/BleKeyMapping.h").read_text(encoding="utf-8")
        store = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
        settings = (ROOT / "src/CrossPointSettings.cpp").read_text(encoding="utf-8")
        map_ui = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        self.assertIn("setGenericLearningCapture(enabled)", manager)
        self.assertIn("!genericLearningCapture_", HOST)
        self.assertIn("FieldKind::TouchContact", GENERIC)
        self.assertIn("sameCaptured", mapping)
        self.assertIn("char inputLabel[kInputLabelBytes]", mapping)
        self.assertIn('item[\"n\"]', store)
        self.assertIn('item[\"n\"]', settings)
        self.assertIn("Learned Button %u", map_ui)
        self.assertIn("Name this input", map_ui)

    def test_physical_iine_touch_routes_before_descriptor_length_rejection(self):
        host_body = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        metadata_start = host_body.index("const bool iineTouchMetadata =")
        report_definition = host_body[host_body.index("const bool iineTouchReport =", metadata_start):]
        report_definition = report_definition[:report_definition.index(";", report_definition.index("const bool iineTouchReport ="))]
        self.assertIn("iineDigitizerCoordinates", report_definition)
        self.assertIn("validatedTouchBody", report_definition)
        self.assertIn("touchCompanion", report_definition)
        validated_route = host_body.index("if (iineTouchReport &&")
        descriptor_length_check = host_body.index("payloadForDeclaredReport(")
        self.assertLess(validated_route, descriptor_length_check)
        self.assertIn("reportId == bleinput::iine_touch::kReportId", host_body)
        self.assertIn("reportType == bleinput::iine_touch::kInputReportType", host_body)
        self.assertIn("hasReportReference", host_body[metadata_start:validated_route])
        self.assertIn("enqueueGesturePair", host_body[validated_route:descriptor_length_check])
        self.assertIn("RAW_REASON_TOUCH_GESTURE_QUEUED", host_body[validated_route:descriptor_length_check])
        self.assertIn("PhysicalIineControlTapProducesOneLearnedIdentityAndAction", MAP_TEST)

    def test_iine_raw_fast_path_requires_descriptor_scope_and_keeps_other_hid_paths(self):
        host_body = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        # The exact Report Reference tuple and a payload that resembles IINE are
        # not enough: an unrelated controller can reuse ID 6 and send the same
        # bytes. The length exception must also have descriptor-confirmed
        # Digitizer coordinates before it bypasses normal descriptor validation.
        old_unscoped_guard = "if (iineTouchMetadata && (validatedTouchBody || touchCompanion))"
        self.assertNotIn(old_unscoped_guard, host_body)
        touch_report = host_body[host_body.index("const bool iineTouchReport ="):]
        touch_report = touch_report[:touch_report.index(";", touch_report.index("const bool iineTouchReport ="))]
        self.assertIn("iineTouchMetadata", touch_report)
        self.assertIn("iineDigitizerCoordinates", touch_report)
        self.assertRegex(touch_report, re.compile(r"iineDigitizerCoordinates\s*&&\s*\(validatedTouchBody\s*\|\|\s*touchCompanion\)"))
        fast_path = host_body.index("if (iineTouchReport &&")
        guard_end = host_body.index(" {", fast_path)
        guard = host_body[fast_path:guard_end]
        self.assertRegex(guard, re.compile(r"validatedTouchBody\s*\|\|\s*touchCompanion"))

        # Positive byte-for-byte IINE recognition and a matching unrelated
        # descriptor are exercised in the host-side fixtures. The unrelated
        # descriptor fails the same Digitizer/X-Y metadata gate.
        self.assertIn("PhysicalIineControlTapProducesOneLearnedIdentityAndAction", MAP_TEST)
        self.assertIn("SameMetadataAndBodyAreRejectedWithoutDescriptorDigitizerCoordinates", MAP_TEST)
        self.assertIn("IineControlNameStillRequiresAValidatedDigitizerReport", MAP_TEST)
        self.assertIn("reportMapAllowsDigitizerCoordinates(hints, kReportId, true, kInputReportType)", MAP_TEST)

        # Standard keyboard, Consumer Control and generic gamepad learning stay
        # downstream of strict declared-length validation.
        self.assertLess(host_body.index("payloadForDeclaredReport("), host_body.index("// --- Standard keyboard report path"))
        normalized_host = re.sub(r"\s+", " ", host_body)
        self.assertIn("reportMapAllowsUsage( g_reportMapHints, reportId, hasReportReference, reportType, 0x07)", normalized_host)
        self.assertIn("reportType, 0x0C)", normalized_host)
        self.assertIn("GenericLearningIgnoresMouseAxesAndTracksGamepadHatRelease", MAP_TEST)
        self.assertIn("FieldKind::Hat", GENERIC)

    def test_capture_rejection_reason_reaches_ui_and_diagnostic_log(self):
        header = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h").read_text(encoding="utf-8")
        host_body = function_body(HOST, "void BleKeyboardHost::onReportIngest(")
        manager = (ROOT / "src/MappedInputManager.cpp").read_text(encoding="utf-8")
        logger = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
        activity = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        self.assertIn("uint16_t expectedLength", header)
        self.assertIn("uint8_t reason", header)
        self.assertIn("raw.reason = RAW_REASON_DESCRIPTOR_LENGTH", host_body)
        self.assertIn("RAW_STATE_REPORT_ID_MISMATCH = 1 << 5", header)
        self.assertIn("report.expectedLength", logger)
        self.assertIn("event.captureRejectReason", manager)
        self.assertIn("HID report length mismatch", activity)

    def test_map_edit_portrait_layout_uses_x4_page_bounds(self):
        activity = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
        lyra = (ROOT / "src/components/themes/lyra/LyraTheme.cpp").read_text(encoding="utf-8")
        body = function_body(activity, "void BleButtonMapActivity::render(")
        hint_body = function_body(lyra, "void LyraTheme::drawButtonHints(")
        self.assertIn("const auto pageWidth = renderer.getScreenWidth()", body)
        self.assertIn("const auto pageHeight = renderer.getScreenHeight()", body)
        self.assertIn("Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20}", body)
        self.assertIn("x4ButtonPositions[] = {58, 146, 254, 342}", hint_body)
        self.assertIn("constexpr int buttonWidth = 80", hint_body)
        self.assertIn("renderer.setOrientation(orig_orientation)", hint_body)

        store = (ROOT / "src/BleMappingProfileStore.cpp").read_text(encoding="utf-8")
        parse_map = function_body(store, "bool parseMap(")
        append_map = function_body(store, "void appendMap(")
        switch_to = function_body(store, "bool BleMappingProfileStore::switchTo(")
        duplicate = function_body(store, "bool BleMappingProfileStore::duplicate(")
        self.assertIn('item["n"]', append_map)
        self.assertIn('item["n"]', parse_map)
        self.assertIn("fromPersisted", parse_map)
        self.assertIn("readMap(profileId, candidate", switch_to)
        self.assertIn("readMap(sourceProfileId, copy", duplicate)
        self.assertIn("Always load the selected profile", switch_to)
        self.assertIn("Do not snapshot", switch_to)
        self.assertIn("LearnedInputLabelPersistsWithoutChangingIdentityOrAction", MAP_TEST)


if __name__ == "__main__":
    unittest.main()
