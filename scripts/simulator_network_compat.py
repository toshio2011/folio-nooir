"""Patch only the upstream host simulator's missing networking surface."""

from pathlib import Path

Import("env")  # noqa: F821 -- provided by PlatformIO


SIMULATOR_ENVS = {
    "simulator_x4",
    "simulator_x3",
    "simulator_x4pro",
    "simulator_x4classic",
    "simulator_nooir167_x4pro",
    "simulator_nooir167_x4classic",
}

if str(env.get("PIOENV") or "") in SIMULATOR_ENVS:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    simulator_dir = (
        project_dir / ".pio" / "libdeps" / str(env.get("PIOENV")) / "simulator" / "src"
    )

    network_client = simulator_dir / "NetworkClient.h"
    if network_client.is_file():
        header = network_client.read_text(encoding="utf-8")
        if "write(const char *str, size_t size)" not in header:
            anchor = "  virtual size_t write(const uint8_t *buf, size_t size);\n"
            if anchor not in header:
                raise RuntimeError("Simulator NetworkClient.h changed: byte write() missing")
            header = header.replace(
                anchor,
                anchor
                + "  // Match Arduino Client's bounded string-write overload.\n"
                + "  size_t write(const char *str, size_t size) {\n"
                + "    return write(reinterpret_cast<const uint8_t *>(str), size);\n"
                + "  }\n",
                1,
            )
            network_client.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator NetworkClient with bounded string-write overload")
        if "int fd() const" not in header:
            anchor = "  virtual uint8_t connected();\n"
            if anchor not in header:
                raise RuntimeError("Simulator NetworkClient.h changed: connected() missing")
            header = header.replace(
                anchor,
                "  // The headless simulator has no ESP-IDF/lwIP socket descriptor.\n"
                "  int fd() const { return -1; }\n" + anchor,
                1,
            )
            network_client.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator NetworkClient with unavailable fd() sentinel")

    web_server_header = simulator_dir / "WebServer.h"
    if web_server_header.is_file():
        header = web_server_header.read_text(encoding="utf-8")
        if "chunkResponseBegin(const char*)" not in header:
            anchor = "  NetworkClient client();\n"
            if anchor not in header:
                raise RuntimeError("Simulator WebServer.h changed: client() missing")
            header = header.replace(
                anchor,
                "  NetworkClient& client();\n"
                "  // The headless simulator does not implement HTTP chunk framing.\n"
                "  void chunkResponseBegin(const char*) {}\n"
                "  void chunkResponseEnd() {}\n",
                1,
            )
            web_server_header.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator WebServer with bounded-chunk host stubs")

    web_server_source = simulator_dir / "WebServer.cpp"
    if web_server_source.is_file():
        source = web_server_source.read_text(encoding="utf-8")
        value_client = "NetworkClient WebServer::client() { return _currentClient; }"
        reference_client = "NetworkClient& WebServer::client() { return _currentClient; }"
        if reference_client not in source:
            if value_client not in source:
                raise RuntimeError("Simulator WebServer.cpp changed: client() definition missing")
            source = source.replace(value_client, reference_client, 1)
            web_server_source.write_text(source, encoding="utf-8", newline="")
            print("Patched simulator WebServer::client() to return its current client by reference")
    http_client = simulator_dir / "esp_http_client.h"
    if http_client.is_file():
        header = http_client.read_text(encoding="utf-8")
        additions = """
inline esp_err_t esp_http_client_get_url(esp_http_client_handle_t handle,
                                         char *buffer, int len) {
  if (!handle || !buffer || len <= 0 || !handle->config.url)
    return ESP_FAIL;
  const size_t urlLen = std::strlen(handle->config.url);
  if (urlLen + 1 > static_cast<size_t>(len))
    return ESP_FAIL;
  std::memcpy(buffer, handle->config.url, urlLen + 1);
  return ESP_OK;
}

inline esp_err_t esp_http_client_delete_header(esp_http_client_handle_t handle,
                                                const char *name) {
  if (!handle || !name)
    return ESP_FAIL;
  handle->headers.erase(name);
  return ESP_OK;
}
"""
        if "esp_http_client_get_url(" not in header:
            anchor = "inline esp_err_t esp_http_client_set_header(esp_http_client_handle_t handle,\n"
            if anchor not in header:
                raise RuntimeError("Simulator esp_http_client.h changed: set_header missing")
            header = header.replace(anchor, additions + "\n" + anchor, 1)
            http_client.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator HTTP client with URL and header helpers")

    secure_http_client = simulator_dir / "SecureHttpClient.h"
    if secure_http_client.is_file():
        header = secure_http_client.read_text(encoding="utf-8")
        if "callbackAborted() const" not in header:
            anchor = "  bool aborted() const { return aborted_; }\n"
            if anchor not in header:
                raise RuntimeError("Simulator SecureHttpClient.h changed: aborted() missing")
            header = header.replace(
                anchor,
                anchor + "  bool callbackAborted() const { return callbackStopped_; }\n",
                1,
            )
            secure_http_client.write_text(header, encoding="utf-8", newline="")
            print("Patched simulator SecureHttpClient with callback-abort query")
