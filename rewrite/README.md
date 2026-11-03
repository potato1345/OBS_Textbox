# OBS Textbox v2 (Clean-Room Rewrite)

Eine moderne, leichtgewichtige Gesamtlösung zur Steuerung dynamischer Textbox-Overlays in **OBS Studio** über ein Web-Dashboard (OBS Custom Dock) und den OBS WebSocket v5.

![OBS Textbox Preview](../main/preview.png)

---

## 🌟 Highlights & Verbesserungen in v2

* **Clean-Room Modularisierung:**
  * **Frontend:** 100 % Vanilla JS als **ES-Module** (`import`/`export`) ohne Webpack/Vite-Build-Zwang. Läuft nativ und direkt im Chromium-basierten OBS Custom Dock.
  * **Barrierefreie Dialoge:** Native HTML5 `<dialog>`-Elemente mit `::backdrop`, `closedby="any"` und Fokusfalle statt veralteter `div`-Overlays.
  * **Kompakte Karten:** Text- und Hintergrundfarben sowie Animations-Optionen sind standardmäßig in einklappbaren Dropdowns (`<details>`) untergebracht. Eingeklappt zeigt jede Karte nur die drei Textzeilen und die Senden/Leeren-Buttons.
  * **Backend:** Modularer Python-Server (`config`, `console`, `documents`, `forms_import`, `http_app`) mit konfigurierbaren Tokens, sauberem Argparse-CLI und integrierter Schutzfilterung (kein Ausliefern von Python-Dateien oder Secrets).
* **Farbprofil-Verwaltung:**
  * Vordefinierte Standard-Profile (z. B. *OBS Dark*, *OBS Light*, *Midnight Blue*, *Forest Green*, *Sunset Warm*, *High Contrast*).
  * Eigene Profile erstellen, bearbeiten und als **YAML exportieren bzw. importieren**.
  * Schutzfunktion: Vordefinierte Standardprofile können weder überschrieben noch gelöscht werden.
* **Intelligenter Liedplan- / Dokument-Import:**
  * **AI-Import:** Erkennt Gottesdienste aus PDF- und Word-Dateien (`.docx`) über kostenlose OpenRouter-Modelle mit Live-Discovery, Retries und automatischer Liedtitel-Vervollständigung aus dem Gotteslob-Index (`lieder.md`).
  * **Offline-Fallback:** Sollte das OpenRouter Free-Tier belegt oder keine Internetverbindung vorhanden sein, übernimmt ein deterministischer lokaler Regex-Parser nahtlos die Strukturierung.
* **Google Forms Gottesdienst-Import:**
  * Eigene Benutzeroberfläche direkt im Dashboard mit Tabellen-Vorschau.
  * Unterstützt direkten Live-Abruf über die Google Forms API oder Datei-Uploads (`.csv`, `.xlsx`, `.zip`).
* **Nahtlose Abwärtskompatibilität:**
  * Unterstützt dieselben CSS-Variablen (`--box-id`, `--zeile-1`, `--zeile-2`, `--zeile-3`, `--box-bg`, etc.).
  * Migriert bestehende Einstellungen und Passwörter aus `localStorage`.

---

## 📂 Projektstruktur

```text
rewrite/
├── config.example.json      # Beispiel-Konfigurationsdatei
├── requirements.txt         # Python-Abhängigkeiten (MarkItDown, openpyxl)
├── start_server.bat         # Start-Skript für Windows
├── start_server.sh          # Start-Skript für Linux/macOS
├── textbox.html             # OBS Browser Source Overlay
│
├── server/                  # Modularer Python-Server (HTTP & APIs)
│   ├── __init__.py
│   ├── __main__.py          # CLI-Einstiegspunkt
│   ├── config.py            # Konfigurations-Management
│   ├── console.py           # Terminal-Live-Dashboard
│   ├── documents.py         # Word/PDF-Konvertierung (MarkItDown)
│   ├── forms_import.py      # CSV/XLSX/API-Parser für Google Forms
│   └── http_app.py          # HTTP-Routing & Sicherheitsfilter
│
├── dashboard/               # Frontend (OBS Custom Dock)
│   ├── index.html           # Dashboard UI
│   ├── css/
│   │   └── dashboard.css    # Modernes Dashboard-Styling
│   ├── js/
│   │   ├── app.js           # Orchestrator & Initialisierung
│   │   ├── cards.js         # Textbox-Karten (kompakt, Dropdowns)
│   │   ├── color-profiles.js# Farbprofile, Hex/RGBA, YAML-Parser
│   │   ├── css-builder.js   # Generierung der OBS-CSS-Injektionen
│   │   ├── forms-importer.js# Google Forms Schnittstelle
│   │   ├── modals.js        # Native <dialog>-Steuerung
│   │   ├── obs-sync.js      # OBS WebSocket Synchronisation
│   │   ├── obs-ws.js        # OBS WebSocket v5 Client
│   │   ├── pdf-ai-importer.js# PDF/DOCX Extraktion & AI-Verarbeitung
│   │   ├── storage.js       # localStorage-Verwaltung & Migration
│   │   └── toast.js         # Toast-Benachrichtigungssystem
│   ├── lieder.md            # Gotteslob-Referenztabelle
│   └── color-profiles.yaml  # Standard-Farbprofile
│
├── animation/               # Sample-Animationen (z. B. 1_advent.gif, ...)
└── tests/                   # Automatisierte Test-Suite
    ├── test_forms_import.py
    ├── test_documents.py
    ├── test_http_app.py
    ├── test_js_modules.js
    └── samples/             # Test-Dateien (CSV, XLSX)
```

---

## 🚀 Einrichtung & Schnellstart

### 1. OBS WebSocket aktivieren
1. In OBS Studio auf **Werkzeuge -> WebSocket-Servereinstellungen** gehen.
2. **WebSocket-Server aktivieren** ankreuzen.
3. Standard-Port ist `4455`. Optional ein Passwort vergeben und notieren.

### 2. Server starten
* **Windows:** Doppelklick auf `start_server.bat`
* **Linux/macOS:** Im Terminal `./start_server.sh` ausführen
* Alternativ via Python:
  ```bash
  python -m server --port 8080
  ```

### 3. Dashboard in OBS einbinden (Custom Browser Dock)
1. In OBS auf **Docks -> Benutzerdefinierte Browser-Docks...** klicken.
2. Dock-Name vergeben (z. B. `Textbox Steuerung`).
3. Als URL eingeben:
   ```text
   http://localhost:8080/dashboard/
   ```
4. Auf **Übernehmen** klicken und das Dock an die gewünschte Stelle in OBS ziehen.
5. In den Einstellungen (⚙) das OBS WebSocket Passwort eingeben und auf **Mit OBS verbinden** klicken.

### 4. Textbox als Browser-Quelle in OBS anlegen
1. Neue **Browser-Quelle** in OBS hinzufügen.
2. Option **Lokale Datei** wählen und `rewrite/textbox.html` auswählen.
3. Dimensionen auf mindestens **1200 × 600 px** stellen.
4. Im Feld **Benutzerdefiniertes CSS** die Box-ID definieren:
   ```css
   :root {
       --box-id: "lowerthird";
   }
   body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }
   ```
5. Sobald im Dashboard eine Karte mit der ID `lowerthird` bearbeitet wird, aktualisiert OBS den Text und die Farben live!

---

## 🧪 Test-Suite ausführen

Die gesamte Funktionalität ist durch automatisierte Tests abgesichert:

### Python Tests (Server, HTTP-APIs, Forms-Parser, MarkItDown):
```bash
python -m unittest discover tests
```

### JavaScript Tests (Farbprofile, YAML-Roundtrip, CSS-Builder, Liedplan-Parser):
```bash
node tests/test_js_modules.js
```

---

## 🔒 Sicherheitsnotizen

* **`GET /api/openrouter-key`** liefert den OpenRouter-Key aus `api.env` **absichtlich** an Clients — das Dashboard liest ihn nur als Vorschlag (Prefill) ins API-Key-Feld ein. Der Endpunkt ist per Code **strikt auf Loopback-Adressen beschränkt** (127.0.0.1 / ::1); Clients aus dem LAN erhalten `403 Forbidden`, auch wenn der Server via `--host 0.0.0.0` im LAN erreichbar ist.
* Ausgeliefert werden ausschließlich Dashboard-, Overlay- und API-Dateien. Blockiert bleiben: alle Punkt-Pfade (`.git`, `.venv`, …), `server/`, `tests/`, `api.env`, `config.json`, `requirements.txt` und Python-Quelldateien.
* Formular- und Dateiname-Werte werden im Dashboard vor dem Rendern HTML-maskiert (`escapeHtml` in `box-utils.js`) — das öffentlich zugängliche Liedplan-Formular kann so kein XSS ins OBS-Dock einschleusen.
