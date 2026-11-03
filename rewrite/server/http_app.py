"""HTTP-Schicht: statische Dateien + JSON-APIs für das Dashboard."""

from __future__ import annotations

import json
import mimetypes
import posixpath
import urllib.parse
from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable

from . import documents, forms_import
from .config import Config, read_openrouter_key
from .console import RequestStats

# Diese Pfade/Endungen werden nie ausgeliefert (Quellcode, Secrets, Tests).
BLOCKED_SEGMENTS = {"server", "tests", "__pycache__", "node_modules", "venv"}
BLOCKED_NAMES = {"api.env", "config.json", "requirements.txt"}
BLOCKED_SUFFIXES = {".py", ".pyc", ".env", ".bat", ".sh", ".ps1"}

LEGACY_REDIRECTS = {
    "/": "/dashboard/",
    "/dashboard": "/dashboard/",
    "/controlui/control.html": "/dashboard/",
    "/controlui/": "/dashboard/",
}

MIME_OVERRIDES = {
    # Windows liefert .js je nach Registry als text/plain aus – ES-Module brauchen JS-MIME.
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".html": "text/html; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".yaml": "text/yaml; charset=utf-8",
    ".yml": "text/yaml; charset=utf-8",
    ".md": "text/markdown; charset=utf-8",
    ".webm": "video/webm",
    ".webp": "image/webp",
    ".svg": "image/svg+xml",
}


class ApiError(Exception):
    def __init__(self, status: int, message: str) -> None:
        super().__init__(message)
        self.status = status
        self.message = message


def is_blocked(url_path: str) -> bool:
    parts = [p for p in posixpath.normpath(url_path).split("/") if p]
    if any(p.startswith(".") for p in parts):
        return True
    if any(p.lower() in BLOCKED_SEGMENTS for p in parts):
        return True
    if parts and (parts[-1].lower() in BLOCKED_NAMES or Path(parts[-1]).suffix.lower() in BLOCKED_SUFFIXES):
        return True
    return False


def make_handler(config: Config, stats: RequestStats | None = None) -> type[SimpleHTTPRequestHandler]:
    root = str(config.root)

    class Handler(SimpleHTTPRequestHandler):
        server_version = "OBSTextbox/2"
        extensions_map = {**SimpleHTTPRequestHandler.extensions_map, **MIME_OVERRIDES}

        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=root, **kwargs)

        # ------------------------------------------------------------ logging
        def log_message(self, format: str, *args) -> None:  # noqa: A002
            if stats is None:
                super().log_message(format, *args)

        def log_request(self, code="-", size="-") -> None:
            if stats is None:
                return super().log_request(code, size)
            try:
                status = int(code)
            except (TypeError, ValueError):
                status = 0
            stats.record(self.command or "?", self.path, status,
                         int(size) if str(size).isdigit() else 0, self.client_address[0])

        def log_error(self, format: str, *args) -> None:  # noqa: A002
            if stats is None:
                super().log_error(format, *args)

        # ------------------------------------------------------------ helpers
        @property
        def route(self) -> str:
            return urllib.parse.urlsplit(self.path).path

        def is_loopback(self) -> bool:
            return self.client_address[0] in ("127.0.0.1", "::1", "::ffff:127.0.0.1")

        def send_json(self, status: int, payload: dict) -> None:
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def read_upload(self) -> tuple[bytes, str]:
            try:
                length = int(self.headers.get("Content-Length") or 0)
            except ValueError:
                length = 0
            if length <= 0:
                raise ApiError(HTTPStatus.BAD_REQUEST, "Keine Datei empfangen.")
            if length > config.max_upload_bytes:
                raise ApiError(HTTPStatus.REQUEST_ENTITY_TOO_LARGE,
                               f"Datei zu groß (max. {config.max_upload_mb} MB).")
            filename = urllib.parse.unquote(self.headers.get("X-Filename") or "upload")
            return self.rfile.read(length), Path(filename).name

        def end_headers(self) -> None:
            # OBS-Docks cachen aggressiv – Änderungen sollen sofort ankommen.
            if not self.route.startswith("/api/"):
                self.send_header("Cache-Control", "no-cache")
            super().end_headers()

        def dispatch(self, routes: dict[str, Callable[[], dict]]) -> bool:
            action = routes.get(self.route)
            if action is None:
                return False
            try:
                self.send_json(HTTPStatus.OK, {"success": True, **action()})
            except ApiError as err:
                self.send_json(err.status, {"success": False, "error": err.message})
            except Exception as err:  # letzte Verteidigungslinie
                self.send_json(HTTPStatus.INTERNAL_SERVER_ERROR,
                               {"success": False, "error": f"Serverfehler: {err}"})
            return True

        # ------------------------------------------------------------ API
        def api_health(self) -> dict:
            return {
                "version": 2,
                "features": {
                    "markitdown": documents.markitdown_available(),
                    "googleForms": config.token_path.is_file(),
                    "openrouterKey": read_openrouter_key(config.root) is not None,
                },
            }

        def api_openrouter_key(self) -> dict:
            if not self.is_loopback():
                raise ApiError(HTTPStatus.FORBIDDEN, "Nur lokal abrufbar.")
            key = read_openrouter_key(config.root)
            if not key:
                raise ApiError(HTTPStatus.NOT_FOUND, "Kein Key in api.env hinterlegt.")
            return {"key": key}

        def api_convert_document(self) -> dict:
            data, filename = self.read_upload()
            try:
                markdown = documents.convert_to_markdown(data, filename)
            except documents.ConversionUnavailable as err:
                raise ApiError(HTTPStatus.SERVICE_UNAVAILABLE, str(err)) from err
            except ValueError as err:
                raise ApiError(HTTPStatus.BAD_REQUEST, str(err)) from err
            except Exception as err:
                raise ApiError(HTTPStatus.UNPROCESSABLE_ENTITY, f"Konvertierung fehlgeschlagen: {err}") from err
            if not markdown.strip():
                raise ApiError(HTTPStatus.UNPROCESSABLE_ENTITY, "Das Dokument enthält keinen Text.")
            return {"markdown": markdown, "filename": filename}

        def api_forms_upload(self) -> dict:
            data, filename = self.read_upload()
            try:
                services, count = forms_import.all_services_from_file(data, filename)
            except (forms_import.ImportFormatError, forms_import.NoResponsesError) as err:
                raise ApiError(HTTPStatus.BAD_REQUEST, str(err)) from err
            return {
                "source": "file",
                "filename": filename,
                "responses": count,
                "service": services[0],
                "services": services,
            }

        def api_forms_responses(self) -> dict:
            try:
                services = forms_import.fetch_all_services(config.token_path, config.google_form_id)
            except forms_import.NoResponsesError as err:
                raise ApiError(HTTPStatus.CONFLICT, str(err)) from err
            except (FileNotFoundError, RuntimeError) as err:
                raise ApiError(HTTPStatus.SERVICE_UNAVAILABLE, str(err)) from err
            except Exception as err:
                raise ApiError(HTTPStatus.BAD_GATEWAY, f"Google-Abfrage fehlgeschlagen: {err}") from err
            return {"source": "google", "responses": len(services), "services": services}

        def api_forms_latest(self) -> dict:
            try:
                service = forms_import.fetch_latest_service(config.token_path, config.google_form_id)
            except forms_import.NoResponsesError as err:
                raise ApiError(HTTPStatus.CONFLICT, str(err)) from err
            except (FileNotFoundError, RuntimeError) as err:
                raise ApiError(HTTPStatus.SERVICE_UNAVAILABLE, str(err)) from err
            except Exception as err:
                raise ApiError(HTTPStatus.BAD_GATEWAY, f"Google-Abfrage fehlgeschlagen: {err}") from err
            return {"source": "google", "service": service}

        # ------------------------------------------------------------ verbs
        def do_GET(self) -> None:
            if self.dispatch({
                "/api/health": self.api_health,
                "/api/openrouter-key": self.api_openrouter_key,
                "/api/forms/latest": self.api_forms_latest,
                "/api/forms/responses": self.api_forms_responses,
            }):
                return
            if self.route.startswith("/api/"):
                return self.send_json(HTTPStatus.NOT_FOUND, {"success": False, "error": "Unbekannter Endpunkt."})
            target = LEGACY_REDIRECTS.get(self.route)
            if target:
                self.send_response(HTTPStatus.FOUND)
                self.send_header("Location", target)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            if is_blocked(self.route):
                return self.send_error(HTTPStatus.NOT_FOUND)
            super().do_GET()

        def do_HEAD(self) -> None:
            if self.route.startswith("/api/") or is_blocked(self.route):
                return self.send_error(HTTPStatus.NOT_FOUND)
            super().do_HEAD()

        def do_POST(self) -> None:
            if not self.dispatch({
                "/api/documents/convert": self.api_convert_document,
                "/api/forms/upload": self.api_forms_upload,
            }):
                self.send_json(HTTPStatus.NOT_FOUND, {"success": False, "error": "Unbekannter Endpunkt."})

    return Handler


def create_server(config: Config, stats: RequestStats | None = None) -> ThreadingHTTPServer:
    mimetypes.init()
    server = ThreadingHTTPServer((config.host, config.port), make_handler(config, stats))
    server.daemon_threads = True
    return server
