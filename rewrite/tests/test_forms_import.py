"""Unit-Tests für forms_import.py im rewrite-Paket."""

import unittest
from pathlib import Path
import sys

# Füge rewrite zum Modulpfad hinzu
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from server.forms_import import (
    parse_answers,
    latest_service_from_file,
    normalize_header,
    header_key,
    PROGRAM_POINTS,
    ImportFormatError
)

SAMPLES_DIR = ROOT / "tests" / "samples"


class TestFormsImport(unittest.TestCase):

    def test_normalize_header(self):
        self.assertEqual(normalize_header("  1.  Eröffnung   –   GL-Nummer "), "1. Eröffnung - GL-Nummer")
        self.assertEqual(normalize_header("Woche\u2013Tag"), "Woche-Tag")

    def test_header_key(self):
        self.assertEqual(header_key("Zeitstempel"), "zeitstempel")
        self.assertEqual(header_key("Timestamp"), "zeitstempel")
        self.assertEqual(header_key("Woche"), "woche")
        self.assertEqual(header_key("Tag"), "tag")
        self.assertEqual(header_key("Organist"), "organist")
        self.assertEqual(header_key("1. Eröffnung - GL-Nummer"), "1.gl")
        self.assertEqual(header_key("1. Eröffnung – Verse"), "1.verse")
        self.assertEqual(header_key("1. Eröffnung - Liedtitel"), "1.titel")
        self.assertEqual(header_key("12. Schlusslied – Liedtitel"), "12.titel")
        self.assertIsNone(header_key("Unbekannte Spalte"))

    def test_csv_comma(self):
        csv_file = SAMPLES_DIR / "antwort_comma.csv"
        self.assertTrue(csv_file.is_file(), f"Testdatei fehlt: {csv_file}")
        service, count = latest_service_from_file(csv_file.read_bytes(), csv_file.name)

        self.assertEqual(count, 1)
        self.assertEqual(service["tag"], "Sonntag")
        self.assertEqual(service["organist"], "E. Stolper")
        self.assertEqual(len(service["punkte"]), 12)

        # 1. Eröffnung: 144, 1+4+6, Nun jauchzt dem Herren, aller Welt
        p1 = service["punkte"][0]
        self.assertEqual(p1["name"], "Eröffnung")
        self.assertEqual(p1["gl"], "144")
        self.assertEqual(p1["verse"], "1+4+6")
        self.assertEqual(p1["titel"], "Nun jauchzt dem Herren, aller Welt")

    def test_csv_semicolon(self):
        csv_file = SAMPLES_DIR / "antwort_semicolon.csv"
        service, count = latest_service_from_file(csv_file.read_bytes(), csv_file.name)
        self.assertEqual(count, 1)
        self.assertEqual(service["tag"], "Sonntag")
        self.assertEqual(service["punkte"][0]["gl"], "144")

    def test_csv_bom(self):
        csv_file = SAMPLES_DIR / "antwort_bom.csv"
        service, count = latest_service_from_file(csv_file.read_bytes(), csv_file.name)
        self.assertEqual(count, 1)
        self.assertEqual(service["punkte"][0]["gl"], "144")

    def test_csv_timestamp_en(self):
        csv_file = SAMPLES_DIR / "antwort_timestamp_en.csv"
        service, count = latest_service_from_file(csv_file.read_bytes(), csv_file.name)
        self.assertEqual(count, 1)
        self.assertEqual(service["zeitstempel"], "20.10.2026 09:15:32")

    def test_xlsx_import(self):
        xlsx_file = SAMPLES_DIR / "antwort.xlsx"
        try:
            import openpyxl
        except ImportError:
            self.skipTest("openpyxl ist nicht installiert.")
        service, count = latest_service_from_file(xlsx_file.read_bytes(), xlsx_file.name)
        self.assertEqual(count, 1)
        self.assertEqual(service["punkte"][0]["gl"], "144")

    def test_invalid_extension(self):
        with self.assertRaises(ImportFormatError):
            parse_answers(b"dummy", "unsupported.txt")


if __name__ == "__main__":
    unittest.main()
