from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_iine_direction_labels_keep_synthetic_identity_and_show_physical_direction():
    source = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
    assert 'physicalDirectionName(physical)' in source
    assert 'swipe %s' in source
    assert 'IINE %s' in source
    touch = (ROOT / "src/util/IineTouchGesture.h").read_text(encoding="utf-8")
    assert '"IINE_control"' in touch
    assert "reportMapAllowsDigitizerCoordinates" in (
        ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp"
    ).read_text(encoding="utf-8")


def test_direct_map_screen_saves_to_existing_profile_store_and_supports_save_as():
    source = (ROOT / "src/activities/settings/BleButtonMapActivity.cpp").read_text(encoding="utf-8")
    assert 'BleMappingProfileStore::saveActive()' in source
    assert 'BleMappingProfileStore::duplicate(sourceId, safeName)' in source
    assert 'switchTo(newId)' in source
    assert 'Save mapping as profile' in source
    assert 'activeProfileName' in source


def test_old_preset_rows_are_removed_and_timeout_off_rebuilds_menu():
    source = (ROOT / "src/activities/settings/BluetoothSettingsActivity.cpp").read_text(encoding="utf-8")
    header = (ROOT / "src/activities/settings/BluetoothSettingsActivity.h").read_text(encoding="utf-8")
    assert 'Action::ApplyPreset' not in source
    assert 'Action::ApplyPreset' not in header
    assert 'applyYiserPreset' not in source + header
    assert 'preferenceOffTransitionRequiresMenuRebuild' in source
    assert 'view = View::Menu;' in source
    assert 'rebuildMenuRows();' in source


def test_factory_preset_permissions_and_legacy_default_remain_separate():
    source = (ROOT / "src/activities/settings/BleMappingProfilesActivity.cpp").read_text(encoding="utf-8")
    assert '"Use preset", "Duplicate preset", "View assignments"' in source
    assert '"Switch to this profile"' in source
    assert 'Default and the active profile cannot be deleted.' in source
    assert '(legacy mapping)' in source
