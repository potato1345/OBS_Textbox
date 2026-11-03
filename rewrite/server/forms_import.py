"""Import der Antworten des Google-Formulars „Liedplan – Gottesdienstprogramm".

Unterstützte Quellen:
  * CSV  – Google-Forms-Download (UTF-8, optional BOM, Komma oder Semikolon)
  * ZIP  – Google-Forms-Download, der die CSV enthält
  * XLSX – verknüpftes Antwort-Sheet als Excel-Datei (benötigt openpyxl)
  * API  – neueste Antwort direkt über die Google Forms API

Das Formular hat einen festen Aufbau: Kopfdaten (Woche, Tag, Organist) und zwölf
Programmpunkte mit je drei Feldern (GL-Nummer, Verse, Liedtitel). Spaltenköpfe
haben die Form ``"<n>. <Programmpunkt> – <Feld>"``.
"""

from __future__ import annotations

import csv
import io
import re
import zipfile
from pathlib import Path
from typing import Iterable, Sequence

PROGRAM_POINTS: tuple[str, ...] = (
    "Eröffnung", "Kyrie", "Gloria", "Zwischengesang", "Halleluja", "Credo",
    "Gabenbereitung", "Heilig", "Agnus Dei", "Kommunion", "Danklied", "Schlusslied",
)
POINT_FIELDS: dict[str, str] = {"GL-Nummer": "gl", "Verse": "verse", "Liedtitel": "titel"}
HEADER_FIELDS: dict[str, str] = {
    "zeitstempel": "zeitstempel", "timestamp": "zeitstempel",
    "woche": "woche", "tag": "tag", "organist": "organist",
}
SUPPORTED_EXTENSIONS = (".csv", ".xlsx", ".zip")

_DASHES = re.compile(r"[\u2012\u2013\u2014\u2015\u2212]")
_SPACES = re.compile(r"\s+")
_POINT_HEADER = re.compile(r"^(\d{1,2})\.\s*(.+?)\s+-\s+(.+)$")


class ImportFormatError(ValueError):
    """Datei ist lesbar, entspricht aber nicht dem erwarteten Formular."""


class NoResponsesError(LookupError):
    """Das Formular bzw. die Datei enthält keine Antworten."""


# --------------------------------------------------------------------------- #
#  Spaltenköpfe erkennen
# --------------------------------------------------------------------------- #

def normalize_header(raw: object) -> str:
    text = "" if raw is None else str(raw)
    text = _DASHES.sub("-", text.replace("\ufeff", ""))
    return _SPACES.sub(" ", text).strip()


def header_key(raw: object) -> str | None:
    """Ordnet einen Spaltenkopf einem internen Schlüssel zu.

    ``"Woche"`` -> ``"woche"``, ``"3. Gloria – Verse"`` -> ``"3.verse"``.
    Unbekannte Spalten liefern ``None``.
    """
    title = normalize_header(raw)
    if not title:
        return None
    simple = HEADER_FIELDS.get(title.lower())
    if simple:
        return simple
    match = _POINT_HEADER.match(title)
    if not match:
        return None
    index = int(match.group(1))
    name, field = match.group(2).strip(), match.group(3).strip()
    if not 1 <= index <= len(PROGRAM_POINTS):
        return None
    if name.casefold() != PROGRAM_POINTS[index - 1].casefold():
        return None
    field_key = POINT_FIELDS.get(field)
    return f"{index}.{field_key}" if field_key else None


def rows_to_records(header: Sequence[object], rows: Iterable[Sequence[object]]) -> list[dict[str, str]]:
    columns: dict[str, int] = {}
    for position, title in enumerate(header):
        key = header_key(title)
        if key and key not in columns:
            columns[key] = position
    if not any(key.startswith("1.") for key in columns):
        raise ImportFormatError(
            "Unbekanntes Dateiformat: Es wurden keine Programmpunkt-Spalten des "
            "Liedplan-Formulars gefunden (erwartet z. B. „1. Eröffnung – GL-Nummer“)."
        )

    records = []
    for row in rows:
        if not any(cell not in (None, "") and str(cell).strip() for cell in row):
            continue
        record = {}
        for key, position in columns.items():
            value = row[position] if position < len(row) else ""
            record[key] = "" if value is None else str(value).strip()
        records.append(record)
    if not records:
        raise NoResponsesError("Die Datei enthält keine Antworten (nur die Kopfzeile).")
    return records


# --------------------------------------------------------------------------- #
#  Dateiformate
# --------------------------------------------------------------------------- #

def parse_csv_bytes(data: bytes) -> list[dict[str, str]]:
    text = data.decode("utf-8-sig", errors="replace")
    if not text.strip():
        raise ImportFormatError("Die CSV-Datei ist leer.")
    first_line = text.lstrip().splitlines()[0]
    delimiter = ";" if first_line.count(";") > first_line.count(",") else ","
    table = list(csv.reader(io.StringIO(text), delimiter=delimiter))
    table = [row for row in table if any(cell.strip() for cell in row)]
    return rows_to_records(table[0], table[1:])


def parse_xlsx_bytes(data: bytes) -> list[dict[str, str]]:
    try:
        from openpyxl import load_workbook
    except ImportError as exc:  # pragma: no cover - abhängig von der Umgebung
        raise ImportFormatError("Für XLSX-Dateien wird das Paket „openpyxl“ benötigt.") from exc
    workbook = load_workbook(io.BytesIO(data), read_only=True, data_only=True)
    try:
        rows = list(workbook.worksheets[0].iter_rows(values_only=True))
    finally:
        workbook.close()
    if not rows:
        raise ImportFormatError("Die XLSX-Datei ist leer.")
    return rows_to_records(rows[0], rows[1:])


def parse_zip_bytes(data: bytes) -> list[dict[str, str]]:
    try:
        archive = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile as exc:
        raise ImportFormatError("Die ZIP-Datei ist beschädigt oder kein gültiges Archiv.") from exc
    with archive:
        names = sorted(
            name for name in archive.namelist()
            if name.lower().endswith(".csv") and not Path(name).name.startswith((".", "__"))
        )
        if not names:
            raise ImportFormatError("Die ZIP-Datei enthält keine CSV-Datei.")
        return parse_csv_bytes(archive.read(names[0]))


_PARSERS = {".csv": parse_csv_bytes, ".xlsx": parse_xlsx_bytes, ".zip": parse_zip_bytes}


def parse_answers(data: bytes, filename: str) -> list[dict[str, str]]:
    extension = Path(filename or "").suffix.lower()
    parser = _PARSERS.get(extension)
    if parser is None:
        raise ImportFormatError(
            f"Nicht unterstütztes Format „{extension or '?'}“. Bitte CSV, XLSX oder ZIP hochladen."
        )
    return parser(data)


# --------------------------------------------------------------------------- #
#  Ergebnisstruktur
# --------------------------------------------------------------------------- #

def record_to_service(record: dict[str, str]) -> dict:
    """Interne Antwort -> Gottesdienst-Struktur für das Dashboard."""
    points = []
    for index, name in enumerate(PROGRAM_POINTS, start=1):
        points.append({
            "name": name,
            "gl": record.get(f"{index}.gl", "").strip(),
            "verse": record.get(f"{index}.verse", "").strip(),
            "titel": record.get(f"{index}.titel", "").strip(),
        })
    return {
        "woche": record.get("woche", "").strip(),
        "tag": record.get("tag", "").strip(),
        "organist": record.get("organist", "").strip(),
        "zeitstempel": record.get("zeitstempel", "").strip(),
        "punkte": points,
    }


def all_services_from_file(data: bytes, filename: str) -> tuple[list[dict], int]:
    """Alle Antworten (neueste zuerst) und die Anzahl als (Liste, count)."""
    records = parse_answers(data, filename)
    services = [record_to_service(record) for record in reversed(records)]
    return services, len(records)


def latest_service_from_file(data: bytes, filename: str) -> tuple[dict, int]:
    """Gibt die letzte Antwort (Google sortiert chronologisch) und die Anzahl zurück."""
    services, count = all_services_from_file(data, filename)
    return services[0], count


# --------------------------------------------------------------------------- #
#  Google Forms API
# --------------------------------------------------------------------------- #

def fetch_all_services(token_path: Path, form_id: str) -> list[dict]:
    """Alle Antworten des Formulars über die Google Forms API (neueste zuerst).

    Blattet über alle Seiten (``pageSize`` + ``pageToken``), damit auch bei
    mehr als 100 Antworten garantiert alle Einreichungen geladen werden.
    """
    if not token_path.is_file():
        raise FileNotFoundError(
            f"Google-Token nicht gefunden: {token_path}. Pfad in config.json "
            "(google_token_path) oder per OBS_TEXTBOX_GOOGLE_TOKEN setzen."
        )
    try:
        from google.oauth2.credentials import Credentials
        from googleapiclient.discovery import build
    except ImportError as exc:
        raise RuntimeError(
            "Google-API-Pakete fehlen: pip install google-api-python-client google-auth"
        ) from exc

    # Scopes werden aus dem gespeicherten Token übernommen.
    credentials = Credentials.from_authorized_user_file(str(token_path))
    service = build("forms", "v1", credentials=credentials, cache_discovery=False)

    form = service.forms().get(formId=form_id).execute()
    titles = {
        item["questionItem"]["question"]["questionId"]: item.get("title", "")
        for item in form.get("items", [])
        if "questionItem" in item
    }

    responses: list[dict] = []
    page_token = None
    while True:
        params: dict = {"formId": form_id, "pageSize": 100}
        if page_token:
            params["pageToken"] = page_token
        body = service.forms().responses().list(**params).execute()
        responses.extend(body.get("responses", []))
        page_token = body.get("nextPageToken")
        if not page_token:
            break

    if not responses:
        raise NoResponsesError("Im Google-Formular liegen noch keine Antworten vor.")

    responses.sort(key=lambda r: r.get("lastSubmittedTime") or r.get("createTime") or "", reverse=True)
    return [response_to_record(response, titles) for response in responses]


def response_to_record(response: dict, titles: dict[str, str]) -> dict:
    """Eine API-Antwort -> internes Record-Format (gleiche Keys wie die Datei-Parser)."""
    record: dict[str, str] = {"zeitstempel": response.get("lastSubmittedTime") or response.get("createTime", "")}
    for question_id, answer in (response.get("answers") or {}).items():
        key = header_key(titles.get(question_id, ""))
        if not key:
            continue
        values = [a.get("value", "") for a in (answer.get("textAnswers") or {}).get("answers", [])]
        record[key] = ", ".join(v for v in values if v)
    return record


def fetch_latest_service(token_path: Path, form_id: str) -> dict:
    services = fetch_all_services(token_path, form_id)
    return services[0]
