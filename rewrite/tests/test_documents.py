"""
test_documents.py — Unit-Tests für die Dokumenten-Konvertierung (Version 2, rewrite/).

Zweck:
  - Prüft erlaubte Dateiendungen (PDF, DOCX).
  - Verifiziert Fehlerbehandlung bei nicht unterstützten Formaten.
  - Testet reale DOCX-Konvertierung über MarkItDown (falls installiert).
"""

import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from server.documents import convert_to_markdown, markitdown_available, ALLOWED_EXTENSIONS

SAMPLE_DOCX = ROOT.parent / "main" / "liedplan.docx"


class TestDocuments(unittest.TestCase):
    """Test-Suite für die MarkItDown-Konvertierung in documents.py."""

    def test_allowed_extensions(self):
        """Prüft, ob Standard-Formate wie .pdf und .docx in der Allow-List enthalten sind."""
        self.assertIn(".pdf", ALLOWED_EXTENSIONS)
        self.assertIn(".docx", ALLOWED_EXTENSIONS)

    def test_unsupported_format(self):
        """Prüft, dass nicht unterstützte Dateiendungen einen ValueError auslösen."""
        with self.assertRaises(ValueError):
            convert_to_markdown(b"dummy", "datei.xyz")

    def test_docx_conversion_if_installed(self):
        """Prüft die tatsächliche Konvertierung einer Beispieldatei ins Markdown-Format."""
        if not markitdown_available():
            self.skipTest("MarkItDown ist in dieser Umgebung nicht verfügbar.")
        if not SAMPLE_DOCX.is_file():
            self.skipTest(f"Testdokument {SAMPLE_DOCX} nicht gefunden.")

        text = convert_to_markdown(SAMPLE_DOCX.read_bytes(), SAMPLE_DOCX.name)
        self.assertIsInstance(text, str)
        self.assertTrue(len(text) > 0)
        self.assertIn("Eröffnung", text)
        self.assertIn("144", text)


if __name__ == "__main__":
    unittest.main()
