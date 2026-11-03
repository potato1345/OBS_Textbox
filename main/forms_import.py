# -*- coding: utf-8 -*-
"""
forms_import.py — Deterministischer Parser für Google-Forms-Antwort-Exports
des Formulars „Liedplan – Gottesdienstprogramm" (St. Johannes Hammelburg).

Gehört zu Version 1 (main/).
Zusammenspiel:
  - Wird von main/server.py aufgerufen, um Uploads von Google-Forms-Dateien
    oder direkte API-Abfragen zu verarbeiten.
  - Liefert ein standardisiertes JSON-Dictionary mit 12 festen Programmpunkten,
    das im v1-Dashboard (main/controlui/control.html) visualisiert wird.

Unterstützte Export-Formate (Google Forms „Antworten" → Herunterladen):
  - CSV  („Antworten als .csv herunterladen", ggf. in .zip verpackt, UTF-8,
          Komma- oder Semikolon-Trennung, mit/ohne BOM)
  - XLSX (über das verlinkte Antwort-Sheet → Herunterladen → Microsoft Excel)
  - ZIP  (enthält genau die CSV-Datei)
  - API  (direkt per Forms API, siehe fetch_latest_response())

Ohne AI: 12 feste Programmpunkte x (GL-Nummer, Verse, Liedtitel) +
Kopfdaten (Woche, Tag, Organist).
"""

import csv
import io
import os
import re
import zipfile
import logging

# Logger für Version 1 (main/)
logger = logging.getLogger("liedplan-v1")

# ---------- Formularstruktur (fix, 12 Programmpunkte) ----------

POINTS = [
    "Er\u00f6ffnung", "Kyrie", "Gloria", "Zwischengesang", "Halleluja", "Credo",
    "Gabenbereitung", "Heilig", "Agnus Dei", "Kommunion", "Danklied", "Schlusslied",
]

FIELD_NAMES = ("GL-Nummer", "Verse", "Liedtitel")

GOOGLE_TOKEN = r"C:\Users\Johann\AppData\Local\hermes\google_token.json"
FORM_ID = "1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY"


class FormEmptyError(Exception):
    """Ausnahme: Im Google-Formular liegen noch keine Antworten vor."""


def _norm_title(title):
    """
    Header normalisieren: Trim, En-Dash/Em-Dash -> Bindestrich, Whitespace kollabieren.
    Ermöglicht toleranten Abgleich unterschiedlich formatierter Spaltenüberschriften.
    """
    t = (title or "").strip()
    t = t.replace("\u2013", "-").replace("\u2014", "-")
    t = re.sub(r"\s+", " ", t)
    return t


def _canonical_key(normalized_title):
    """
    Normierten Header auf internen Schlüssel mappen. None wenn unbekannt.

    Beispiele:
      - '1. Eröffnung - GL-Nummer' -> '1:GL-Nummer'
      - 'Zeitstempel' -> 'zeitstempel'
      - 'Organist' -> 'organist'
    """
    if not normalized_title:
        return None
    if normalized_title in ("Zeitstempel", "Timestamp"):
        return "zeitstempel"
    if normalized_title in ("Woche", "Tag", "Organist"):
        return normalized_title.lower()
    if " - " in normalized_title:
        left, right = normalized_title.rsplit(" - ", 1)
        m = re.match(r"^(\d+)\.\s*(.+)$", left)
        if m and right in FIELD_NAMES:
            num = int(m.group(1))
            name = m.group(2).strip()
            if 1 <= num <= len(POINTS) and name == POINTS[num - 1]:
                return "%d:%s" % (num, right)
    return None


# ---------- CSV / XLSX / ZIP Parsing ----------

def _rows_to_dicts(header_row, data_rows):
    """
    Kombiniert Header-Zeile und Datenzeilen zu einer Liste von Wörterbüchern
    mit kanonischen Schlüsseln. Validiert, dass mindestens Spalten des ersten
    Programmpunkts vorhanden sind.
    """
    keys = {}
    for idx, h in enumerate(header_row):
        key = _canonical_key(_norm_title(h))
        if key is not None and key not in keys:
            keys[key] = idx
    if not any(k.startswith("1:") for k in keys):
        raise ValueError(
            "Unbekanntes Dateiformat: Keine Programmpunkt-Spalten des "
            "Liedplan-Formulars erkannt (erwartet z. B. ‚1. Eröffnung - GL-Nummer').")

    rows = []
    for raw in data_rows:
        row = {}
        for key, idx in keys.items():
            v = raw[idx] if idx < len(raw) else ""
            row[key] = "" if v is None else str(v)
        rows.append(row)
    return rows


def parse_answers_csv(data):
    """
    Parst rohe CSV-Bytes (UTF-8, optional mit BOM, automatischer Erkennung von Komma/Semikolon)
    in eine Liste von Zeilen-Dictionaries.
    """
    logger.info("CSV-Parsing gestartet (%d Bytes)", len(data))
    text = data.decode("utf-8-sig", errors="replace")
    lines = [ln for ln in text.splitlines() if ln.strip() != ""]
    if not lines:
        logger.warning("CSV-Parsing abgebrochen: Datei ist leer")
        raise ValueError("Die CSV-Datei ist leer.")
    first = lines[0]
    delimiter = ";" if first.count(";") > first.count(",") else ","
    reader = csv.reader(io.StringIO("\n".join(lines)), delimiter=delimiter)
    all_rows = list(reader)
    if len(all_rows) < 2:
        logger.warning("CSV-Parsing abgebrochen: Nur Kopfzeile gefunden")
        raise ValueError("Keine Antworten in der CSV-Datei gefunden (nur Kopfzeile).")
    rows = _rows_to_dicts(all_rows[0], all_rows[1:])
    logger.info("CSV-Parsing erfolgreich: %d Antworten extrahiert", len(rows))
    return rows


def parse_answers_xlsx(data):
    """
    Parst rohe XLSX-Bytes eines Google-Spreadsheet-Exports in eine Liste
    von Zeilen-Dictionaries.
    """
    logger.info("XLSX-Parsing gestartet (%d Bytes)", len(data))
    from openpyxl import load_workbook
    wb = load_workbook(io.BytesIO(data), read_only=True)
    ws = wb.worksheets[0]
    rows_iter = ws.iter_rows(values_only=True)
    try:
        header_row = next(rows_iter)
    except StopIteration:
        logger.warning("XLSX-Parsing abgebrochen: Datei ist leer")
        raise ValueError("Die XLSX-Datei ist leer.")
    data_rows = [r for r in rows_iter if any(c is not None and str(c).strip() != "" for c in r)]
    if not data_rows:
        logger.warning("XLSX-Parsing abgebrochen: Keine Datenzeilen vorhanden")
        raise ValueError("Keine Antworten in der XLSX-Datei gefunden.")
    rows = _rows_to_dicts([c if c is not None else "" for c in header_row],
                          [list(r) for r in data_rows])
    logger.info("XLSX-Parsing erfolgreich: %d Antworten extrahiert", len(rows))
    return rows


def parse_answers_zip(data):
    """
    Extrahiert die CSV-Datei aus einem Google-Forms ZIP-Download und parst sie.
    """
    logger.info("ZIP-Parsing gestartet (%d Bytes)", len(data))
    try:
        zf = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        logger.warning("ZIP-Parsing abgebrochen: Ungültiges Archiv")
        raise ValueError("Die ZIP-Datei ist beschädigt oder kein gültiges Archiv.")
    csv_names = sorted(n for n in zf.namelist()
                       if n.lower().endswith(".csv") and not n.endswith(".DS_Store"))
    if not csv_names:
        logger.warning("ZIP-Parsing abgebrochen: Keine CSV-Datei im Archiv gefunden")
        raise ValueError("Keine CSV-Datei in der ZIP-Datei gefunden.")
    logger.info("Extrahierte CSV '%s' aus ZIP-Archiv", csv_names[0])
    return parse_answers_csv(zf.read(csv_names[0]))


def parse_answers_file(path, filename):
    """
    Wählt anhand der Dateiendung (.csv, .xlsx, .zip) den passenden Parser aus.
    Liest den Dateiinhalt von der Festplatte.
    """
    _, ext = os.path.splitext(filename or os.path.basename(path))
    ext = ext.lower()
    logger.info("parse_answers_file für '%s' (Format: %s)", filename, ext)
    with open(path, "rb") as f:
        data = f.read()
    if ext == ".csv":
        return parse_answers_csv(data)
    if ext == ".xlsx":
        return parse_answers_xlsx(data)
    if ext == ".zip":
        return parse_answers_zip(data)
    logger.warning("Nicht unterstütztes Dateiformat: %s", ext)
    raise ValueError("Nicht unterstütztes Format „%s“. Bitte CSV, XLSX oder ZIP." % ext)


# ---------- Mappen auf Gottesdienst-Struktur ----------

def to_gottesdienst(row):
    """
    Transformiert ein Zeilen-Dictionary mit internen Schlüsseln in die standardisierte
    Gottesdienst-JSON-Struktur mit genau 12 Programmpunkten:
      {woche, tag, organist, zeitstempel, punkte: [12 x {name, gl, verse, titel}]}
    """
    punkte = []
    for i, name in enumerate(POINTS, 1):
        punkte.append({
            "name": name,
            "gl": row.get("%d:GL-Nummer" % i, "").strip(),
            "verse": row.get("%d:Verse" % i, "").strip(),
            "titel": row.get("%d:Liedtitel" % i, "").strip(),
        })
    return {
        "woche": row.get("woche", "").strip(),
        "tag": row.get("tag", "").strip(),
        "organist": row.get("organist", "").strip(),
        "zeitstempel": row.get("zeitstempel", "").strip(),
        "punkte": punkte,
    }


# ---------- Direkte API-Abfrage (ohne Datei-Download) ----------

def fetch_latest_response():
    """
    Fragt die neueste Formular-Antwort direkt über die Google Forms API ab
    und gibt die standardisierte Gottesdienst-Struktur zurück.
    Benötigt ein gültiges OAuth-Token unter GOOGLE_TOKEN.
    """
    logger.info("Google Forms API-Abfrage gestartet für Formular %s", FORM_ID)
    if not os.path.exists(GOOGLE_TOKEN):
        logger.error("Google-Token-Datei nicht gefunden: %s", GOOGLE_TOKEN)
        raise FileNotFoundError("Google-Token nicht gefunden: %s" % GOOGLE_TOKEN)
    from google.oauth2.credentials import Credentials
    from googleapiclient.discovery import build
    # WICHTIG: keinen expliziten Scope übergeben — das ersetzt die gespeicherten
    # Scopes beim Token-Refresh und Google verweigert dann (403
    # ACCESS_TOKEN_SCOPE_INSUFFICIENT). Die Datei enthält bereits den Forms-Scope.
    creds = Credentials.from_authorized_user_file(GOOGLE_TOKEN)
    service = build("forms", "v1", credentials=creds, cache_discovery=False)

    fb = service.forms().get(formId=FORM_ID).execute()
    id2title = {}
    for it in fb.get("items", []):
        qi = it.get("questionItem")
        if qi:
            id2title[qi["question"]["questionId"]] = it.get("title", "")

    rl = service.forms().responses().list(formId=FORM_ID).execute()
    responses = rl.get("responses", [])
    if not responses:
        logger.warning("Keine Antworten im Google-Formular vorhanden")
        raise FormEmptyError("Noch keine Antworten")

    def _stamp(r):
        return (r.get("createTime") or r.get("create_time") or "")
    newest = max(responses, key=_stamp)
    logger.info("Neueste Antwort ausgewählt (Erstellungszeit: %s)", _stamp(newest))

    row = {}
    for q in newest.get("questions", []):
        title = id2title.get(q.get("questionId"), "")
        key = _canonical_key(_norm_title(title))
        if key is None:
            continue
        v = q.get("textValue", "")
        row[key] = "" if v is None else str(v)
    return to_gottesdienst(row)
