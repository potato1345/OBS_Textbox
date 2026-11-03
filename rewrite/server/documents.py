"""
Dokument-Konvertierung (PDF/DOCX -> Markdown) über MarkItDown.

Gehört zu Version 2 (rewrite/server/).
Zusammenspiel:
  - Wird vom Endpunkt /api/documents/convert in http_app.py aufgerufen.
  - Ermöglicht dem Web-Dashboard (pdf-ai-importer.js), Dokumenteninhalte
    in strukturierten Text zu überführen.
"""

from __future__ import annotations

import logging
import os
import tempfile
from pathlib import Path

logger = logging.getLogger("liedplan")

ALLOWED_EXTENSIONS = {".pdf", ".docx", ".doc", ".pptx", ".xlsx", ".txt", ".md", ".html"}


class ConversionUnavailable(RuntimeError):
    """Ausnahme: MarkItDown ist nicht in der Python-Umgebung installiert."""


def markitdown_available() -> bool:
    """Prüft, ob das markitdown-Paket importiert werden kann."""
    try:
        import markitdown  # noqa: F401
    except ImportError:
        return False
    return True


def convert_to_markdown(data: bytes, filename: str) -> str:
    """
    Konvertiert rohe Datei-Bytes (PDF, Word, etc.) über MarkItDown in Markdown-Text.
    Verwendet eine temporäre Datei mit der ursprünglichen Endung zur Formaterkennung.
    """
    extension = Path(filename or "").suffix.lower() or ".pdf"
    if extension not in ALLOWED_EXTENSIONS:
        logger.warning("Dokumenten-Konvertierung abgelehnt: Nicht unterstützter Typ '%s'", extension)
        raise ValueError(f"Dateityp „{extension}“ wird nicht unterstützt.")
    try:
        from markitdown import MarkItDown
    except ImportError as exc:
        logger.error("MarkItDown ist nicht installiert")
        raise ConversionUnavailable(
            'MarkItDown ist nicht installiert. Bitte `pip install "markitdown[all]"` ausführen.'
        ) from exc

    logger.info("Konvertiere Dokument '%s' (%d Bytes, Typ %s) nach Markdown", filename, len(data), extension)
    # MarkItDown erkennt das Format am Dateinamen – daher temporäre Datei mit Endung.
    handle, temp_path = tempfile.mkstemp(suffix=extension)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(data)
        result = MarkItDown().convert(temp_path).text_content or ""
        logger.info("Konvertierung von '%s' erfolgreich (%d Zeichen Markdown)", filename, len(result))
        return result
    except Exception as exc:
        logger.error("Fehler bei der MarkItDown-Konvertierung von '%s': %s", filename, exc)
        raise
    finally:
        try:
            os.remove(temp_path)
        except OSError:
            pass
