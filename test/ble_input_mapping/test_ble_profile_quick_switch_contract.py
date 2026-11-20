from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PROFILE_UI = (ROOT / "src/activities/settings/BleMappingProfilesActivity.cpp").read_text(encoding="utf-8")
OPTION_POPUP = (ROOT / "src/components/OptionPopup.h").read_text(encoding="utf-8")


def function_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


def test_switch_action_is_preselected_for_saved_and_factory_profiles():
    assert "constexpr int kQuickSwitchDefaultAction = 1;" in PROFILE_UI
    assert 'std::vector<std::string>{"Cancel", "Use preset", "Duplicate preset", "View assignments"}' in PROFILE_UI
    assert '"Cancel", "Switch to this profile", "Duplicate profile"' in PROFILE_UI
    assert "actionsPopup.show(StrId::STR_SETTINGS_PROFILES, options, kQuickSwitchDefaultAction," in PROFILE_UI


def test_quick_switch_keeps_the_existing_store_action_and_active_marker():
    assert "BleMappingProfileStore::switchTo(id)" in PROFILE_UI
    assert 'if (record.id == BleMappingProfileStore::activeId()) label += "  (Active)";' in PROFILE_UI
    assert "bleinput::start" not in PROFILE_UI
    assert "bleinput::stop" not in PROFILE_UI


def test_opening_profile_actions_only_sets_focus_and_waits_for_a_new_confirm():
    popup_show = function_body(OPTION_POPUP, "void show(const char* titleStr")
    assert "selectedIndex = clampIndex(currentIndex, optionCount);" in popup_show
    assert "onSelectCallback = std::move(onSelect);" in popup_show
    assert "onSelectCallback(selectedIndex)" not in popup_show
    loop = function_body(PROFILE_UI, "void BleMappingProfilesActivity::loop()")
    assert "actionsPopup.handleInput(mappedInput" in loop
    assert "showActions();" in loop
    assert "requestUpdate();" in loop
    assert "return;" in loop
