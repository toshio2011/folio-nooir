import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SERVER = (ROOT / "src/network/CrossPointWebServer.cpp").read_text(encoding="utf-8")
PAGE = (ROOT / "src/network/html/SettingsPage.html").read_text(encoding="utf-8")
POLICY = (ROOT / "src/network/WebSettingsApiPolicy.h").read_text(encoding="utf-8")


class GroupedSettingsContractTest(unittest.TestCase):
    def test_only_grouped_settings_routes_are_registered(self):
        self.assertIn('server->on("/api/settings/groups", HTTP_GET', SERVER)
        self.assertIn('server->on("/api/settings", HTTP_GET', SERVER)
        self.assertIn('server->on("/api/settings", HTTP_POST', SERVER)
        self.assertIn('server->on("/settings", HTTP_GET', SERVER)
        self.assertNotIn('server->on("/api/clippings', SERVER)

    def test_post_checks_declared_body_size_before_copy_and_document(self):
        body_size_check = SERVER.index("const int bodyBytes = server->clientContentLength();", SERVER.index("void CrossPointWebServer::handlePostSettings"))
        body_copy = SERVER.index('String body = server->arg("plain");', body_size_check)
        document = SERVER.index("JsonDocument doc;", body_copy)
        self.assertLess(body_size_check, body_copy)
        self.assertLess(body_copy, document)
        self.assertIn("kMaxGroupRequestBytes = 2048", POLICY)

    def test_every_post_key_is_validated_before_settings_are_applied(self):
        handler = SERVER[SERVER.index("void CrossPointWebServer::handlePostSettings"):]
        validate = handler.index("allKeysBelongToGroup")
        apply = handler.index("const auto applySetting")
        self.assertLess(validate, apply)
        self.assertIn("Unknown or out-of-group settings key; no changes applied", handler)

    def test_browser_keeps_only_one_group_and_uses_group_scoped_routes(self):
        self.assertIn("/api/settings/groups", PAGE)
        self.assertIn("/api/settings?group=", PAGE)
        self.assertIn("/api/settings?group=' + encodeURIComponent(currentGroupId)", PAGE)
        self.assertNotIn("allSettings", PAGE)
        self.assertNotIn("/api/settings?offset=", PAGE)
        self.assertNotIn("loadWifiNetworks();\n  loadOpdsServers();", PAGE)

    def test_font_registry_refresh_is_limited_to_reading_picker_paths(self):
        get_handler = SERVER[SERVER.index("void CrossPointWebServer::handleGetSettings"):SERVER.index("void CrossPointWebServer::handlePostSettings")]
        post_handler = SERVER[SERVER.index("void CrossPointWebServer::handlePostSettings"):SERVER.index("// ---- OPDS Server API ----")]
        self.assertIn("if (selectedGroup == SettingsGroupId::Reading) sdFontSystem.refreshIfDirty();", get_handler)
        self.assertIn("if (selectedGroup == SettingsGroupId::Reading && doc[\"fontFamily\"].is<JsonVariant>())", post_handler)
        self.assertLess(post_handler.index("body = String();"), post_handler.index("sdFontSystem.refreshIfDirty();"))

    def test_backing_out_clears_accumulated_group_rows_after_a_late_page(self):
        self.assertIn("if (token !== groupLoadToken) { rows.length = 0; return; }", PAGE)
        self.assertIn("if (activeGroupController) activeGroupController.abort();", PAGE)

    def test_late_group_extras_do_not_retain_state_after_leaving_their_group(self):
        self.assertIn("if (currentGroupId !== 'system') clockWeatherState = null;", PAGE)
        self.assertIn("if (currentGroupId !== 'connectivity') wifiNetworks = [];", PAGE)
        self.assertIn("if (currentGroupId !== 'connectivity') opdsServers = [];", PAGE)


if __name__ == "__main__":
    unittest.main()
