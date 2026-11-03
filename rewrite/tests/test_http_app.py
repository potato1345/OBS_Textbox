"""
test_http_app.py — Integrationstests für den HTTP-Server und die API-Endpunkte (Version 2, rewrite/).

Zweck:
  - Startet den HTTP-Server auf einem dynamischen Port.
  - Prüft Sicherheitssperren (is_blocked) für Python-Quellcode, Secrets und Systemdateien.
  - Verifiziert Legacy-Redirects von alten Pfaden auf /dashboard/.
  - Testet API-Endpunkte (/api/health, /api/forms/upload) sowie MIME-Types.
"""

import json
import threading
import unittest
import urllib.request
import urllib.error
import urllib.parse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from server.config import Config
from server.http_app import create_server, is_blocked


class TestHttpApp(unittest.TestCase):
    """Integrationstests für den ThreadingHTTPServer und Handler in http_app.py."""

    @classmethod
    def setUpClass(cls):
        """Startet eine Testinstanz des HTTP-Servers auf einem freien Port."""
        cls.config = Config(
            host="127.0.0.1",
            port=0,  # Dynamischer freier Port
            root=ROOT
        )
        cls.server = create_server(cls.config)
        cls.port = cls.server.server_address[1]
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        """Fährt den Testserver herunter und schließt den Socket."""
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def test_security_blocked_paths(self):
        """Prüft, ob sensible Server-Dateien und Konfigurationen als blockiert erkannt werden."""
        self.assertTrue(is_blocked("/server/config.py"))
        self.assertTrue(is_blocked("/server/__init__.py"))
        self.assertTrue(is_blocked("/api.env"))
        self.assertTrue(is_blocked("/config.json"))
        self.assertTrue(is_blocked("/requirements.txt"))
        self.assertTrue(is_blocked("/start_server.bat"))
        self.assertFalse(is_blocked("/dashboard/index.html"))
        self.assertFalse(is_blocked("/dashboard/js/app.js"))
        self.assertFalse(is_blocked("/textbox.html"))

    def test_blocked_files_return_404(self):
        """Prüft, dass der Server beim Versuch, gesperrte Dateien abzurufen, HTTP 404 liefert."""
        for path in ["/server/config.py", "/requirements.txt"]:
            with self.assertRaises(urllib.error.HTTPError) as ctx:
                urllib.request.urlopen(f"{self.base_url}{path}")
            self.assertEqual(ctx.exception.code, 404)

    def test_legacy_redirects(self):
        """Prüft Weiterleitungen alter URLs (z. B. Root '/') auf '/dashboard/'."""
        class NoRedirectHandler(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, req, fp, code, msg, headers, newurl):
                return None  # Verhindert automatisches Folgen, damit 302 geprüft werden kann

        opener = urllib.request.build_opener(NoRedirectHandler)
        try:
            resp = opener.open(f"{self.base_url}/")
            code = resp.status
        except urllib.error.HTTPError as e:
            code = e.code
            location = e.headers.get("Location")

        self.assertEqual(code, 302)
        self.assertEqual(location, "/dashboard/")

    def test_api_health(self):
        """Prüft die Ausgabe des Health-Endpunkts /api/health."""
        with urllib.request.urlopen(f"{self.base_url}/api/health") as resp:
            self.assertEqual(resp.status, 200)
            data = json.loads(resp.read().decode())
            self.assertTrue(data["success"])
            self.assertEqual(data["version"], 2)
            self.assertIn("markitdown", data["features"])

    def test_static_js_mime_type(self):
        """Verifiziert, dass JavaScript-Dateien mit dem korrekten MIME-Type ausgeliefert werden."""
        req = urllib.request.Request(f"{self.base_url}/dashboard/js/app.js")
        with urllib.request.urlopen(req) as resp:
            self.assertEqual(resp.status, 200)
            content_type = resp.headers.get("Content-Type", "")
            self.assertTrue("javascript" in content_type.lower(), f"Erwartet JS MIME-Type, erhalten: {content_type}")

    def test_static_textbox_overlay(self):
        """Verifiziert die Auslieferung der Textbox-Overlay-HTML."""
        with urllib.request.urlopen(f"{self.base_url}/textbox.html") as resp:
            self.assertEqual(resp.status, 200)
            body = resp.read().decode("utf-8")
            self.assertIn("OBS Textbox Overlay", body)
            self.assertIn("--box-id", body)

    def test_api_forms_upload(self):
        """Testet den Upload und das Parsen einer Google-Forms-CSV-Datei über die HTTP-API."""
        sample = ROOT / "tests" / "samples" / "antwort_comma.csv"
        self.assertTrue(sample.is_file())

        req = urllib.request.Request(
            f"{self.base_url}/api/forms/upload",
            data=sample.read_bytes(),
            headers={
                "Content-Type": "text/csv",
                "X-Filename": urllib.parse.quote(sample.name)
            },
            method="POST"
        )
        with urllib.request.urlopen(req) as resp:
            self.assertEqual(resp.status, 200)
            res = json.loads(resp.read().decode())
            self.assertTrue(res["success"])
            self.assertEqual(res["responses"], 1)
            self.assertEqual(res["service"]["tag"], "Sonntag")


if __name__ == "__main__":
    unittest.main()
