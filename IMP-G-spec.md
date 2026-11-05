# IMP-G — Generalisierte Karten: Zeilen, Styling, Rendering

## Ziel (User, 05.11.2026)
GL-Nummer/Verse/Liedtitel werden genereller und anpassbar (2, 3, 4, ... Zeilen statt fest 3).
Styling-Optionen werden genereller (frei benennbare CSS-Regeln) und sind einklappbar.
Karten rendern sichtbar (aktuell leer + keine Szenen-Items).

## G1 — Dynamische Zeilen
- `LiedplanRow {label, value}`; `LiedplanCard.rows` (max 8), `LiedplanCard.style` (CSS-Regeln).
- Legacy-Fallback: `effective_rows(card)` = rows, sonst 3 Default-Zeilen aus zeile1..3
  (Labels "GL-Nummer","Verse","Liedtitel"). zeile1..3 bleiben Mirror der ersten drei Values.
- JSON v3: `rows` (Array {label,value}) + `style` (Array {name,value}) + Legacy-Keys.
- Render: `--zeile-1..N` + `--zeile-count`; textbox.html rendert Linien dynamisch per JS
  (Font-Hierarchie: Zeile 1 größer, Rest kleiner); Listener akzeptiert `zeilen`-Array ODER
  Legacy `zeile1..3` (v1/v2-Dashboards bleiben kompatibel).
- Templates: Tokens `{{zeile-1}}..{{zeile-8}}` (Value der N-ten Zeile) zusätzlich zu {{zeile1..3}}.
- Dock: Zeilen-Editor pro Karte (Label+Value+✕), "+ Zeile" (max 8).

## G2 — Generelles Styling
- `LiedplanStyleRule {name, value}`; Injection als `--name: value;` (Name auf [A-Za-z0-9-]
  normalisiert, "-" Prefix; Value stripping von ';' '{' '}' Newlines; ungültige Rules droppen).
- textbox.html konsumiert generisch: `--lp-padding-top` (350px Default), `--lp-padding-bottom`,
  `--lp-text-align`, `--lp-font-1/2/3`, `--lp-corner-size` (Scale), `--lp-gap`.
- Profil erhält `style` (Merge: Karten-Regeln gewinnen gegen Profil-Regeln); YAML: `style: name=value` Lines.
- Dock: CSS-Regel-Editor (+ Regel / ✕ je Regel) innerhalb der einklappbaren Styling-Sektion;
  Sektion bleibt default-aufgeklappt (M7-Regel), Chevron einklappbar.

## G3 — Rendering sichtbar
- Karten-Inhalt aus Liedplan (PDF-Offline-Parser, 12 Programmpunkte) in State geschrieben;
  Start-Sync (F2b) erzeugt Karten-HTML beim Start.
- Szenen-Items: über Dock-Button "Zu Szene" je Karte (OBS als Normal-User gestartet,
  UIA-Invoke möglich); Browser-Quellen `local_file` + shutdown:false.
- WebSocket-Passwort beim Neustart auf bekannten Wert gesetzt (config.json).

## Tests
- state: rows-Migration legacy→rows, rows-Roundtrip, Labels, Clamp 8, style Roundtrip/Drop.
- html: Inject --zeile-1..N/--zeile-count, Style-Inject sanitized, Template {{zeile-4}},
  card_html-Fallback dynamisch, push-Payload-Shape (zeilen+count).
- profile: style-Merge + YAML.

## Akzeptanz
Alle Tests grün; deployed; Karten in Szene sichtbar; Screenshot des ganzen Fensters;
Commit+Push; iteriere weiter bis User stoppt.
