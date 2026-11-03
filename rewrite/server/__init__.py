"""
OBS Textbox – lokaler Hilfsserver für Version 2 (rewrite/).

Zweck:
  - Liefert statische Dateien für das v2-Dashboard (rewrite/dashboard/) und die Textbox-View aus.
  - Stellt REST-APIs für Dokumenten-Konvertierung (PDF/DOCX via MarkItDown) und
    Google-Forms-Imports (CSV/XLSX/ZIP sowie direkte Forms-API) bereit.
  - Enthält ein konfigurierbares Terminal-Dashboard zur Überwachung von Server-Statistiken.

Zusammenspiel:
  - config: Lädt Einstellungen aus config.json und Umgebungsvariablen.
  - documents: MarkItDown-Integration für Dokument-Uploads.
  - forms_import: Deterministischer Parser für Google-Forms-Antworten.
  - http_app: HTTP-Routing, Sicherheitssperren und JSON-Handler.
  - console: Interaktive Terminal-Statusanzeige mit ANSI-Farben.
"""

__version__ = "2.0.0"
