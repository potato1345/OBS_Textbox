"""Dokument-Konvertierung (PDF/DOCX -> Markdown) über MarkItDown."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path

ALLOWED_EXTENSIONS = {".pdf", ".docx", ".doc", ".pptx", ".xlsx", ".txt", ".md", ".html"}


class ConversionUnavailable(RuntimeError):
    """MarkItDown ist nicht installiert."""


def markitdown_available() -> bool:
    try:
        import markitdown  # noqa: F401
    except ImportError:
        return False
    return True


def convert_to_markdown(data: bytes, filename: str) -> str:
    extension = Path(filename or "").suffix.lower() or ".pdf"
    if extension not in ALLOWED_EXTENSIONS:
        raise ValueError(f"Dateityp „{extension}“ wird nicht unterstützt.")
    try:
        from markitdown import MarkItDown
    except ImportError as exc:
        raise ConversionUnavailable(
            'MarkItDown ist nicht installiert. Bitte `pip install "markitdown[all]"` ausführen.'
        ) from exc

    # MarkItDown erkennt das Format am Dateinamen – daher temporäre Datei mit Endung.
    handle, temp_path = tempfile.mkstemp(suffix=extension)
    try:
        with os.fdopen(handle, "wb") as stream:
            stream.write(data)
        return MarkItDown().convert(temp_path).text_content or ""
    finally:
        try:
            os.remove(temp_path)
        except OSError:
            pass
