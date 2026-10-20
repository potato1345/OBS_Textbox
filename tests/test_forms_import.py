# -*- coding: utf-8 -*-
"""Unit-Tests für forms_import.py (ohne pytest). Start: .venv/Scripts/python.exe tests/test_forms_import.py"""
import io
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import forms_import as fi

SAMPLES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samples")
FAILURES = []


def check(name, fn):
    try:
        fn()
        print("PASS:", name)
    except AssertionError as e:
        FAILURES.append(name)
        print("FAIL:", name, "->", e)
    except Exception as e:
        FAILURES.append(name)
        print("FAIL:", name, "-> Unerwarteter Fehler:", type(e).__name__, e)


def expect_common(rows, label):
    assert len(rows) == 1, "%s: erwartet 1 Zeile, got %d" % (label, len(rows))
    r = rows[0]
    assert r.get("woche") == "19.10. \u2013 25.10.2026", "woche: %r" % r.get("woche")
    assert r.get("tag") == "Sonntag", "tag: %r" % r.get("tag")
    assert r.get("organist") == "E. Stolper", "organist: %r" % r.get("organist")
    assert r.get("1:GL-Nummer") == "144", "gl1: %r" % r.get("1:GL-Nummer")
    assert r.get("1:Verse") == "1+4+6", "verse1: %r" % r.get("1:Verse")
    assert r.get("1:Liedtitel") == "Nun jauchzt dem Herren, aller Welt", "titel1: %r" % r.get("1:Liedtitel")
    assert r.get("12:GL-Nummer") == "365", "gl12: %r" % r.get("12:GL-Nummer")
    assert r.get("12:Liedtitel") == "Gott sei Dank f\u00fcr alles", "titel12: %r" % r.get("12:Liedtitel")
    assert r.get("6:GL-Nummer", "") == "", "Credo-GL soll leer sein: %r" % r.get("6:GL-Nummer")


def t_comma():
    with open(os.path.join(SAMPLES, "antwort_comma.csv"), "rb") as f:
        rows = fi.parse_answers_csv(f.read())
    expect_common(rows, "comma")


def t_semicolon():
    with open(os.path.join(SAMPLES, "antwort_semicolon.csv"), "rb") as f:
        rows = fi.parse_answers_csv(f.read())
    expect_common(rows, "semicolon")


def t_bom():
    with open(os.path.join(SAMPLES, "antwort_bom.csv"), "rb") as f:
        rows = fi.parse_answers_csv(f.read())
    expect_common(rows, "bom")


def t_timestamp_en():
    with open(os.path.join(SAMPLES, "antwort_timestamp_en.csv"), "rb") as f:
        rows = fi.parse_answers_csv(f.read())
    expect_common(rows, "timestamp_en")
    assert rows[0].get("zeitstempel") == "20.10.2026 09:15:32", "zeitstempel: %r" % rows[0].get("zeitstempel")


def t_xlsx():
    with open(os.path.join(SAMPLES, "antwort.xlsx"), "rb") as f:
        rows = fi.parse_answers_xlsx(f.read())
    expect_common(rows, "xlsx")


def t_zip():
    src = os.path.join(SAMPLES, "antwort_comma.csv")
    with open(src, "rb") as f:
        csv_bytes = f.read()
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("Form Responses 1.csv", csv_bytes)
    rows = fi.parse_answers_zip(buf.getvalue())
    expect_common(rows, "zip")


def t_missing_columns():
    # Ohne Verse-Spalten, mit unbekannter Extra-Spalte
    text = ("Zeitstempel,Woche,Organist,1. Er\u00f6ffnung \u2013 GL-Nummer,1. Er\u00f6ffnung \u2013 Liedtitel,Unbekannt\n"
            "x,26.10.2026,Priester,42,Lied,ignore_me\n")
    rows = fi.parse_answers_csv(text.encode("utf-8"))
    r = rows[0]
    assert r.get("woche") == "26.10.2026", r
    assert r.get("1:GL-Nummer") == "42", r
    assert r.get("1:Verse", "") == "", "fehlende Verse-Spalte muss leer sein"


def t_unknown_format():
    try:
        fi.parse_answers_csv("Hallo,Welt\n1,2\n".encode("utf-8"))
        raise AssertionError("hätte ValueError werfen müssen")
    except ValueError:
        pass


def t_empty_csv():
    try:
        fi.parse_answers_csv(b"")
        raise AssertionError("hätte ValueError werfen müssen")
    except ValueError:
        pass


def t_header_only():
    try:
        fi.parse_answers_csv("Zeitstempel,Woche\n".encode("utf-8"))
        raise AssertionError("hätte ValueError werfen müssen")
    except ValueError:
        pass


def t_bad_zip():
    try:
        fi.parse_answers_zip(b"das ist kein zip")
        raise AssertionError("hätte ValueError werfen müssen")
    except ValueError:
        pass


def t_to_gottesdienst_shape():
    rows = fi.parse_answers_csv(open(os.path.join(SAMPLES, "antwort_comma.csv"), "rb").read())
    d = fi.to_gottesdienst(rows[0])
    assert len(d["punkte"]) == 12, len(d["punkte"])
    assert d["punkte"][0]["name"] == "Er\u00f6ffnung"
    assert d["punkte"][11]["name"] == "Schlusslied"
    assert d["punkte"][4] == {"name": "Halleluja", "gl": "175", "verse": "1", "titel": "Halleluja!"}
    assert d["punkte"][9]["gl"] == "" and d["punkte"][9]["titel"] == "", "Kommunion muss leer sein"
    assert d["woche"] and d["tag"] and d["organist"]


def main():
    tests = [
        ("CSV Komma", t_comma),
        ("CSV Semikolon", t_semicolon),
        ("CSV BOM", t_bom),
        ("CSV Header Timestamp (EN)", t_timestamp_en),
        ("XLSX", t_xlsx),
        ("ZIP mit CSV", t_zip),
        ("Fehlende/extra Spalten", t_missing_columns),
        ("Unbekanntes Format -> ValueError", t_unknown_format),
        ("Leere CSV -> ValueError", t_empty_csv),
        ("Nur Kopfzeile -> ValueError", t_header_only),
        ("Defekte ZIP -> ValueError", t_bad_zip),
        ("Gottesdienst-Struktur (12 Punkte)", t_to_gottesdienst_shape),
    ]
    for name, fn in tests:
        check(name, fn)
    print()
    if FAILURES:
        print("FEHLGESCHLAGEN:", FAILURES)
        sys.exit(1)
    print("ALL TESTS PASSED (%d)" % len(tests))
    sys.exit(0)


if __name__ == "__main__":
    main()
