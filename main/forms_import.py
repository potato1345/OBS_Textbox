# -*- coding: utf-8 -*-
"""
forms_import.py — Deterministischer Parser für Google-Forms-Antwort-Exports
des Formulars „Liedplan – Gottesdienstprogramm" (St. Johannes Hammelburg).

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

# ---------- Formularstruktur (fix, 12 Programmpunkte) ----------

POINTS = [
    "Er\u00f6ffnung", "Kyrie", "Gloria", "Zwischengesang", "Halleluja", "Credo",
    "Gabenbereitung", "Heilig", "Agnus Dei", "Kommunion", "Danklied", "Schlusslied",
]

FIELD_NAMES = ("GL-Nummer", "Verse", "Liedtitel")

GOOGLE_TOKEN = r"C:\Users\Johann\AppData\Local\hermes\google_token.json"
FORM_ID = "1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY"


class FormEmptyError(Exception):
    """Im Formular liegen noch keine Antworten vor."""


def _norm_title(title):
    """Header normalisieren: Trim, En-Dash/Em-Dash -> Bindestrich, Whitespace kollabieren."""
    t = (title or "").strip()
    t = t.replace("\u2013", "-").replace("\u2014", "-")
    t = re.sub(r"\s+", " ", t)
    return t


def _canonical_key(normalized_title):
    """Normierten Header auf internen Schlüssel mappen. None wenn unbekannt.

    z.B. '1. Eröffnung - GL-Nummer' -> '1:GL-Nummer', 'Zeitstempel' -> 'zeitstempel'
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
    """Header-Zeile + Datenzeilen zu Liste von Dicts mit internen Schlüsseln."""
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
    """CSV-Bytes (UTF-8, BOM erlaubt, Komma oder Semikolon) -> Liste von Zeilen-Dicts."""
    text = data.decode("utf-8-sig", errors="replace")
    lines = [ln for ln in text.splitlines() if ln.strip() != ""]
    if not lines:
        raise ValueError("Die CSV-Datei ist leer.")
    first = lines[0]
    delimiter = ";" if first.count(";") > first.count(",") else ","
    reader = csv.reader(io.StringIO("\n".join(lines)), delimiter=delimiter)
    all_rows = list(reader)
    if len(all_rows) < 2:
        raise ValueError("Keine Antworten in der CSV-Datei gefunden (nur Kopfzeile).")
    return _rows_to_dicts(all_rows[0], all_rows[1:])


def parse_answers_xlsx(data):
    """XLSX-Bytes (Antwort-Sheet) -> Liste von Zeilen-Dicts."""
    from openpyxl import load_workbook
    wb = load_workbook(io.BytesIO(data), read_only=True)
    ws = wb.worksheets[0]
    rows_iter = ws.iter_rows(values_only=True)
    try:
        header_row = next(rows_iter)
    except StopIteration:
        raise ValueError("Die XLSX-Datei ist leer.")
    data_rows = [r for r in rows_iter if any(c is not None and str(c).strip() != "" for c in r)]
    if not data_rows:
        raise ValueError("Keine Antworten in der XLSX-Datei gefunden.")
    return _rows_to_dicts([c if c is not None else "" for c in header_row],
                          [list(r) for r in data_rows])


def parse_answers_zip(data):
    """ZIP-Bytes (Google-Forms-CSV-Download) -> Liste von Zeilen-Dicts."""
    try:
        zf = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile:
        raise ValueError("Die ZIP-Datei ist beschädigt oder kein gültiges Archiv.")
    csv_names = sorted(n for n in zf.namelist()
                       if n.lower().endswith(".csv") and not n.endswith(".DS_Store"))
    if not csv_names:
        raise ValueError("Keine CSV-Datei in der ZIP-Datei gefunden.")
    return parse_answers_csv(zf.read(csv_names[0]))


def parse_answers_file(path, filename):
    """Dispatch per Dateiname/Endung. Liest die Datei selbst."""
    _, ext = os.path.splitext(filename or os.path.basename(path))
    ext = ext.lower()
    with open(path, "rb") as f:
        data = f.read()
    if ext == ".csv":
        return parse_answers_csv(data)
    if ext == ".xlsx":
        return parse_answers_xlsx(data)
    if ext == ".zip":
        return parse_answers_zip(data)
    raise ValueError("Nicht unterstütztes Format „%s“. Bitte CSV, XLSX oder ZIP." % ext)


# ---------- Mappen auf Gottesdienst-Struktur ----------

def to_gottesdienst(row):
    """Eine Antwort-Zeile (interne Schlüssel) -> fixer Gottesdienst-JSON.

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
    """Neueste Formular-Antwort direkt per Forms API holen -> Gottesdienst-JSON."""
    if not os.path.exists(GOOGLE_TOKEN):
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
        raise FormEmptyError("Noch keine Antworten")

    def _stamp(r):
        return (r.get("createTime") or r.get("create_time") or "")
    newest = max(responses, key=_stamp)

    row = {}
    for q in newest.get("questions", []):
        title = id2title.get(q.get("questionId"), "")
        key = _canonical_key(_norm_title(title))
        if key is None:
            continue
        v = q.get("textValue", "")
        row[key] = "" if v is None else str(v)
    return to_gottesdienst(row)
