#!/usr/bin/env python3
"""Serve a local Nooir emulator preview on loopback only."""
from __future__ import annotations

import argparse
import ipaddress
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class IsolatedPreviewHandler(SimpleHTTPRequestHandler):
    def guess_type(self, path: str) -> str:
        # Windows' MIME database may map .mjs to text/plain, which strict ES
        # module loading rejects. GitHub Pages serves it as JavaScript.
        if Path(path).suffix.lower() == '.mjs':
            return 'text/javascript'
        return super().guess_type(path)

    def end_headers(self) -> None:
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, default=Path(__file__).resolve().parents[1] / "web-emulator")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8177)
    args = parser.parse_args()
    try:
        if not ipaddress.ip_address(args.host).is_loopback:
            raise SystemExit("This preview server only accepts a loopback bind address.")
    except ValueError as error:
        raise SystemExit("Use a numeric loopback host such as 127.0.0.1 or ::1.") from error
    directory = args.directory.resolve(strict=True)
    handler = partial(IsolatedPreviewHandler, directory=str(directory))
    server = ThreadingHTTPServer((args.host, args.port), handler)
    print(f"Serving {directory} at http://{args.host}:{args.port}/")
    print("The server is local-only. Add ?devEngine=1 to use the existing local eink-qemu engine on port 8176.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())