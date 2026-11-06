# Funktionsvergleich: Dock-Version (v2-Dashboard) vs. OBS-Plugin (T7)

Stand: 2026-11-04 — nach M7h (inkl. IMP-A..E). Alle Meilensteine M0–M7h + Ui1–Ui4 committed
(`d69e3dc`, `f1556e9`, `df8e5f4`+`d2b104a`, `ae4162b`, `97900a9`, `383d0d0`, `8539313`, `76da0a2`, `d398532`, `913072c`, `1092aa0`, `e330a11`, `18987fb`, `7ad504a`, `8eb231d`, `90e25fd`, `7d097c9`, `32fc830`, `a32cf2d`+`fbca542`, `1ea454f`).
Quellen: `rewrite/dashboard/` + `rewrite/server/` (v2, „Dock-Version“) vs. `plugin/` (natives OBS-32.2.2-Plugin).
Verifikation Stand M7h: ctest 7/7 (state, textfmt, theme-icons, import, ai, render, test-liedplan-html), DLL deployed (obs-plugins\64bit).

## Architektur-Kontext (wichtig vor dem Tabellenvergleich)

| | v2 „Dock-Version“ | Plugin (T7) |
|---|---|---|
| Anzeige in OBS | **Browser-Quellen** mit injizierten CSS-Variablen (`--box-id` …) | **Browser-Quellen (CEF)** pro Karte mit `main/index.html` + injiziertem `:root`-CSS-Block (M2a). Ursprünglich `text_gdiplus`/Custom Source geplant; nach E2E-Fehlschlag der QPainter-Custom-Source (schwarz, 25.10) per User-Entscheidung auf Browser-Quelle umgestellt |
| Transport | OBS WebSocket v5 (SHA-256-Auth, Reconnect 3,5 s, 10 s-Timeouts) | **in-process** — kein Transport, kein Passwort, kein Reconnect |
| Host | Python-Server `:8080` (statisch + `/api/*`) | keiner |
| Zustand | `localStorage` im Browser | JSON-Datei `plugin_config/liedplan-for-obs/liedplan-state.json` (global, nicht pro Profil) |
| Sync-Trigger | `autoSend` + 140 ms Debounce + CSS-Vergleich | `save_state()` → `write_card()` (HTML-Datei) + `push_update()` (Live-Kanal im CEF, **ohne Reload**); Regenbogen per `lp-rainbow`-Platzhalter via `textContent` toggled |

Konsequenz: Die v2-Features „OBS-Verbinden/Trennen“, „WS-Passwort“, „Reconnect“, „CSS-Vergleich“
haben im Plugin **kein Pendant — und brauchen keines**. Sie fallen von der Liste.

## Status-Legende
- ✅ funktioniert (im Plugin, mit Meilenstein)
- ⚠️ teilweise (Modell/Feld vorhanden, UI oder Rendering fehlt)
- ❌ fehlt (mit Implementierungsvorschlag)
- ➖ nicht nötig (Architektur-Unterschied)

## A. Karten & State

| # | Funktion (v2) | Plugin | Umsetzung im Plugin |
|---|---|---|---|
| A1 | Variable Kartenliste: neue Box, löschen, „Alle leeren“ | ✅ M1 | 12er-Vorlage, „＋ Neue Karte“, „✕“ pro Karte, „Alle löschen“ mit Bestätigung |
| A2 | 3 Felder pro Karte (GL-Nummer / Verse / Liedtitel) | ✅ M1 | — |
| A3 | Eindeutige Box-Ids, keine Wiederverwendung | ✅ M1 | `boxN` + persistenter `next_id` (CTest-geprüft) |
| A4 | Persistenz über Reload | ✅ M1 | JSON-Autosave bei jeder Änderung; überlebt OBS-Neustart (E2E verifiziert) |
| A5 | Gesamte Konfiguration als JSON exportieren/importieren (Backup) | ✅ M4 | „💾 Backup“ / „♻ Restore“ (QFileDialog); Restore atomar (Temp-State, Fehler lässt State unverändert); CTest-Roundtrip |
| A6 | Schnell-Profile: gespeicherte **Gesamt-Snapshots** in horizontaler Leiste | ✅ M4 | „⭐ Schnell-Profile“: speichern/übernehmen/löschen (`quick_profiles` im State, Backward-Compat-Defaults); E2E im Live-OBS verifiziert |
| A7 | Pro-Karte-Deckkraft: Text 0–255 + Hintergrund 0–255 | ✅ M6 | `text_opacity`/`box_opacity` im State + gerendert (rgba; `box_opacity 0` → `display:none`); **per-Karte-UI im Dock**: TEXT-/BOX-DECKKRAFT-Slider (0–255) mit Live-Update via push_update (E2E verifiziert: 128 → rgba-Alpha 0.501961) |
| A8 | Pro-Karte freie Farbwahl (Color-Picker Text + Hintergrund) | ✅ M6 | Eckfarben A/B frei wählbar (QColorDialog, M2b) + **freier Text-/BG-Color-Picker**: TEXT-FARBE/BOX-FARBE-Buttons (QColorDialog) pro Karte, live in State + Card-HTML (E2E verifiziert: #c8320a → rgba(200,50,10,…)) |
| A9 | Eckenfarben A/B (je + Opacity) + „Regenbogen-Ecken“ (animiert) | ✅ M2b/Ui2 | `corner_color_a/b`, `corner_opacity_a/b`, `rainbow_corners` im State; Regenbogen = v2-Mechanik 1:1 (hue-rotate 12 s), Live-Toggle ohne Reload (CTest + E2E verifiziert) |
| A10 | Pro-Karte-Animation: GIF/Webm-Datei, Skala, X/Y-Offset (±1200 px), Show-Hide | ✅ M5+M5.1 | `anim_file`, `anim_scale`, `anim_x`, `anim_y`, `show_anim` im State; Dock-Gruppe „🎬 Animation“ (Datei- + Ordnerwahl, Skalierung, X-/Y-Versatz, Show/Hide). M5.1-CEF-Fix: **Bilder (gif/png/jpg/jpeg/webp, ≤16 MB) inline als base64-Data-URI** (CEF-127-File-Access-Begrenzung), **Videos (webm/mp4/mov) als Kopie neben die Card-HTML** + file-URL; `normalize_anim_src` mit vollem percent-Encoding (Umlaute/Spaces). E2E: Advent-GIF rendert per GetSourceScreenshot-Proof |

## B. Farben & Profile

| # | Funktion (v2) | Plugin | Umsetzung |
|---|---|---|---|
| B1 | 6 Standard-Farbprofile (OBS Dark/Light, Midnight Blue, Forest Green, Sunset Warm, High Contrast) | ✅ M1 | Port von `color-profiles.yaml`; pro Karte als PROFIL-Dropdown. (Rendering-Entscheidung B1 — „eine Quelle“ — per M2a über **Browser-Quelle** gelöst, s. Architektur) |
| B2 | Eigene Farbprofile anlegen/speichern/löschen (alle Farb-Felder per Picker) | ✅ M6 | State-Feld `color_profiles` (Custom-Profile); per-Karte „Profil +“ (speichert aktuelle Text-/Box-Farbe, Standard-Namen case-insensitiv geschützt, upsert); Settings-Gruppe „Farbprofile“ (Custom-Liste + Löschen); Lookup Standard→Custom (E2E verifiziert) |
| B3 | Farbprofile als YAML exportieren/importieren | ✅ M6 | Pure Funktionen `liedplan_profile_to_yaml`/`liedplan_yaml_to_profile` (v2-Key-Namen textColor/cardOuterBg → v2-18-Key-Dateien importierbar; 18-Key-CTest); Settings „YAML exportieren/importieren …“ (Standard-Kollision → „ (Import)“-Suffix) (CTest 7 Gruppen + E2E verifiziert) |
| B4 | Dashboard-UI-Thema (Dark/Light, `uiColors`) | ➖ | OBS-UI-Thema gehört zu OBS, nicht zum Plugin (Dock trägt seither Ui3 die native Yami-Dark-Palette von OBS 32.2.2) |

## C. Anzeige & Sync in OBS

| # | Funktion (v2) | Plugin | Umsetzung |
|---|---|---|---|
| C1 | Karten erscheinen in OBS (Browser-Quelle pro Box) | ✅ M2a | Browser-Quelle „Liedplan `<box-id>`“ (800×600) in der **aktuellen** Szene; „▶ Zu Szene“ (Migration alter `liedplan_card`-Quellen, E2E box13 verifiziert) |
| C2 | Live-Sync bei Änderungen (autoSend, Debounce, CSS-Vergleich) | ✅ M2a/M2b | `save_state()` → `write_card()` + `push_update()` (Live-Kanal, **kein Reload**); Fallback-`applyCard`-Schema 1:1 mit push_update (M2b-F1-Fix verifiziert) |
| C3 | Gelöschte Box wird in OBS ausgeblendet (leerer Block/Opacity 0) | ✅ M2b | „✕ Leeren“ (Quelle bleibt in Szene, Text leer — Layout stabil) + „Entf.“ (Quelle entfernt) |
| C4 | OBS WebSocket v5-Client (Handshake, SHA-256-Auth, Request-Promises, Auto-Reconnect) | ➖ | kein Transport nötig — in-process. Das war der Hauptzweck von T7 |
| C5 | Verbinden/Trennen, Auto-Connect mit gespeichertem Passwort, Statusanzeige im Header | ➖ | kein Verbindungskonzept, kein Passwort |

## D. Import

| # | Funktion (v2) | Plugin | Umsetzung |
|---|---|---|---|
| D1 | Google-Forms-Import: neueste **oder** alle Antworten (OAuth, Pagination) | ✅ M3 | WinHTTP im Worker-Thread; Access-Token per Refresh aus Hermes-Dateien (Client-Secret + Token, **nur lesen**, nie beschreiben); „Neueste Antwort laden“ im Import-Dialog; FORM-ID als Setting (Default = Liedplan-Form) |
| D2 | Datei-Import: CSV (BOM, Komma/Semikolon, DE/EN-Timestamp, 40 Spalten) + XLSX + ZIP | ✅ M3+M4 | C++-Port von `server/forms_import.py` (deterministisch, Referenz-Parser); XLSX + ZIP (Deflate/Store) in M4; CTest-Suite `liedplan-import` 19/19 inkl. aller 5 Samples aus `rewrite/tests/samples/` |
| D3 | AI-Import: PDF/Dokumente → OpenRouter-LLM → Programmpunkte; System-Prompt; Fallback; Titel-Enrichment | ✅ M5 | `liedplan-ai.{hpp,cpp}`: **markitdown-Python-Sidecar** (Repo-Venv) + OpenRouter (Key-Pfad als Setting) + **Offline-Fallback** (deterministischer Parser, 1:1-Port von `pdf-ai-importer.js`); AI-Dialog im Dock (Dokument, System-Prompt, API-Key ein-/ausblenden, Vorschau, „AI-Import starten“); CTest-Suite `liedplan-ai`. **Offenes Ende:** `main/api.env`-Key fehlt noch für den Live-OpenRouter-Pfad — Offline-Fallback deckt E2E ab |
| D4 | Lied-Referenz `lieder.md` (908 Lieder, GL-Nummer → Titel) zur Titel-Vervollständigung | ✅ M5 | `lieder_path`-Setting; nach Import/AI: GL-Nummer → Titel auffüllen, wenn leer |
| D5 | Python-Server `:8080` (statisch + `/api/*`) | ➖ | war v2s Transport/Proxy — im Plugin ersetzt durch in-process + direkte WinHTTP-Calls (D1) + Sidecar (D3) |

## E. UI-Extras (v2)

| # | Funktion (v2) | Plugin | Umsetzung |
|---|---|---|---|
| E1 | Toasts (Erfolg/Fehler/Info) | ✅ | `QMessageBox` bei Fehlern + Statuszeile im Dock für letzte Aktion („Importiert: 12 Punkte“, „Schnell-Profil gespeichert: …“ u. a.) |
| E2 | Tastenkürzel (Esc schließt Dialoge) | ➖ | QDialog-Standardverhalten, kein Aufwand |
| E3 | Live-Preview (index.html) | ➖ | die OBS-Preview **ist** die Preview (Browser-Quellen) |
| E4 | Header-Status „Mit OBS verbunden / Nicht verbunden“ | ➖ | kein Pendant nötig |

## F. Was das Plugin BEREITS hat, was v2 nicht kann

- In-Prozess-Sync ohne Latenz, Passwort, Reconnect-Logik
- Dock ohne Browser-Tab; läuft, wenn OBS läuft (Ui3: natives OBS-Look — exakte Yami-Dark-Palette aus obs-studio 32.2.2)
- Kein CSS-Variablen-Trick, keine Quellen-Neulade-Problematik (push_update ohne Reload)
- CTest-Automatisierung: 7 Suiten (state, textfmt, theme-icons, import, ai, render, test-liedplan-html) — v2 hat Python- + JS-Tests ohne OBS-Bezug
- Backup/Restore atomar; Schnell-Profile als Gesamt-Snapshots; einstufiges Undo (Strg+Z)
- AI-Import mit Offline-Fallback (läuft auch ohne OpenRouter-Key)

## Meilenstein-Status (abgeschlossen M0–M7h)

| Meilenstein | Features | Status | Commit |
|---|---|---|---|
| M0 | Skelett, Dock sichtbar | ✅ | `d69e3dc` |
| M1 | Kartenliste, State, Profile, CTest | ✅ | `f1556e9` |
| M2a | Browser-Quelle pro Karte (index.html + CSS-Injektion + push_update) | ✅ | `df8e5f4`+`d2b104a` |
| Ui1 | Dock im v1-Look | ✅ | `ae4162b` |
| M2b/Ui2 | Ecken A/B + Deckkraft A/B + Regenbogen | ✅ | `97900a9` |
| M3 | Forms-/CSV-Import (Google-Forms-API + CSV-Port) | ✅ | `383d0d0` |
| M4 | XLSX/ZIP-Import, Backup/Restore, Schnell-Profile, Settings | ✅ | `8539313` |
| M5 | Animationen (A10) + AI-Import (D3) + lieder.md-Enrichment (D4) | ✅ | `76da0a2` |
| M5.1 | CEF-File-Access-Fix (Bilder base64-Inline, Videos als Kopie) | ✅ | `d398532` |
| Ui3 | Natives OBS-Look (Yami-Dark-Palette 1:1) | ✅ | `913072c` |
| Ui4 | Natives OBS-Icon (Yami-SVG-Icons, keine Emoji) | ✅ | `1092aa0` |
| M6 | A7/A8-UI (Deckkraft-Slider + Text-/BG-Color-Picker) + B2 eigene Farbprofile + B3 YAML-Export/Import | ✅ | `e330a11` |
| M7 | Dock-Nativisierung R2: Icon-Toolbar unten (Neu/Löschen/AI-Import/Einstellungen inkl. Backup-Restore), Such-Dropdown oben (Profil + Karten mit Scroll-to-Card-Flash), einklappbare Style-Optionen (Preset/Text-Farbe/Box-Farbe/Ecken A+B/Deckkraft/gedruckt), schmalere Außen-/Box-Ränder, kVersion 2 | ✅ | `18987fb` |
| M7b | Dock-Fixes (User-Feedback): 1px-Boxrand (Doppelrahmen entfernt), redundanter „Entf.“-Button entfernt (✕ in ID-Leiste löscht bereits Karte + Szenen-Quelle), 1px-Fensterrand um das Dock, Style-Optionen default-sichtbar (Chevron-Toggle bleibt) | ✅ | `7ad504a` |
| M7c | Dock-Fensterrand klarer (`#464B59` → `#5B6273`, 1px) | ✅ | `8eb231d` |
| M7d | Dock-Fensterrand dezent wie PTZ-Plugin (1px `#0F1013` statt heller `#5B6273`) | ✅ | `90e25fd` |
| M7e | Dock-Innenrand entfernt — QSS-Border rendert in der Clientfläche, Kante pixelidentisch mit PTZ | ✅ | `7d097c9` |
| M7f | Dock-Rand 1px `#3C404D` — gleiche Dicke und Farbe wie der Karten-Box-Rand | ✅ | `32fc830` |
| M7g | Abgerundete Kartenecken unten (10px/9px per-Ecke `border-radius`), Trash-Icon in Karten-ID-Leiste, Profil-Lösch-Button im oberen Dropdown | ✅ | `a32cf2d`+`fbca542` |
| M7h | Abgerundete untere Fensterecken des Docks (10px, inkl. `LiedplanFloatRoundedHelper` für Floating-Zustand) | ✅ | `1ea454f` |

**Offen:** Einziger Rest = D3-OpenRouter-Key in `main/api.env` (User-Aktion; Offline-Fallback aktiv und E2E-verifiziert). Alle v2-Funktionsbereiche A–D sind damit portiert — **M7h (`1ea454f`) schließt die UI-Nativisierung ab; Feature-Parität vollständig.** (Ui4 `1092aa0`: SVG-Icons statt Emoji.)

## Entscheidungen (Johann, 2026-10-24 — final)

1. **B1 — Karten-Hintergrund:** soll mit **einer einzigen Quelle** gelöst werden (any source). **Erledigt 25.10 per M2a:** Browser-Quelle (CEF) pro Karte — nach E2E-Fehlschlag der QPainter-Custom-Source (schwarzes Rendern) per User-Entscheidung gewählt.
2. **A9 — Eckenfarben/Regenbogen: bleiben im Scope** — ✅ M2b/Ui2 umgesetzt.
3. **D3 — AI-Import: wird übernommen** (portiert ins Plugin). **Generell: ALLE v2-Funktionen müssen ins Plugin** — ✅ umgesetzt (M6 schließt die letzten offenen B2/B3 + A7/A8-UI ab, `e330a11`).
4. **A6 — Schnell-Profile: ja** (in M4) — ✅ M4 umgesetzt.
