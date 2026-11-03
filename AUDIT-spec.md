# Spec: Verbesserungs-Audit OBS_Textbox (READ-ONLY)

Datum: 2026-11-03. Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox`

## Ziel
Analysiere alle drei Versionen (`main/` v1, `rewrite/` v2, `plugin/` v3) und schreibe eine Verbesserungsliste
nach `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\IMPROVEMENTS.md` (NEU, einzige erlaubte Schreib-Datei).

## Erlaubt
- Lesen aller Dateien in `main/`, `rewrite/`, `plugin/src/`, `plugin/CMakeLists.txt`, Root-Doku
  (README.md, FUNKTIONSVERGLEICH.md, CODE_TODO.md).
- Schreiben von `IMPROVEMENTS.md` ONLY.

## Verboten (hart)
- Keine Code-/Doku-Änderungen an bestehenden Dateien. Kein git. Kein Build, keine Tests ausführen.
- Keine Umbenennungen/Verschiebungen.

## Analyse-Achsen
1. **Robustheit**: Fehlerpfade ohne Behandlung (IO-Fehler, Parse-Grenzen, Race/Thread-Grenzen, Deadlocks,
   Resource-Leaks), fehlende Input-Validierung, Cross-Plattform (Linux/macOS Pfade bei Plugin-Code?).
2. **UX für Johann (Gottesdienst-Praxis)**: fehlende Features die im Gottesdienst helfen
   (z. B. Wochen-/Multiple-Gottesdienste-Selection ist bereits als Wunsch notiert, Import UX,
   Fehler-Sichtbarkeit im Dock, Keyboard-Shortcuts, Undo), Performance (CEF reloads, Save-Häufigkeit).
3. **Code-Qualität**: Duplikate (Parser v2↔plugin, utils), tote Stellen, inkonsistente Schemas
   (push_update vs Fallback-Schema Regel!), Test-Coverage-Gaps (welche Pfade NICHT getestet sind).
4. **Sicherheit**: XSS-Flächen (public Form → innerHTML), Token Handling (read-only Regel), Path-Traversal bei
   Datei-Import/anim-Dateien, YAML/JSON-Parsing harte Grenzen (Zip-Bomb Cap s. CODE_TODO K1).
5. **Doku-Konsistenz**: FUNKTIONSVERGLEICH.md vs Plugin-Realität, README vs Layout.

## IMPROVEMENTS.md Format
Für jeden Kandidat (max ~18, priorisiert):
`P1|P2|P3` Priorität, Titel, Version(en), Datei:Zeile-Anker, Problem (2 Zeilen), Verbesserung (konkret,
implementierbar), Risiko der Umsetzung (low/med/high), geschätzter Aufwand.
Am Ende Abschnitt "NICHT empfohlen" für verworfene Ideen mit Grund.

## Report-Format am Ende
`STATUS: DONE|BLOCKED:<grund>`, `KANDIDATEN: <zahl>`, `GE SCHRIEBEN: IMPROVEMENTS.md`
