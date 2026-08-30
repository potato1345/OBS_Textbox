 die farbsauswahlen inklusive der opacity der textbox karten sollten in einem dropdown sitzen, so dass die karten im eingeklapptem zustand nur die drei zeilen und die senden/leeren buttons zeigen

 die farbeinstellungen in den einstellungen sollten auch profile enthalten, es soll default profiles für verschiedene farbkombinationen geben, die ich im ide in yaml datein festlgen kann, man soll aber auch die option haben, eigene farbprofile zu erstllen, und diese abspeichern zu lassen oder als yaml exportieren bzw. importieren zu lassen, es soll auch ne option geben die standert hardcodet profile zu exportieren. profile müssen eine option zum löschen haben, allerdings dürfen die default profile natürlich nicht gelöscht werden können

# Specification: OBS Dock PDF Automation Script
## 1. Objective
Automatisch Textinhalte (Lieder/Abläufe) aus einer hochgeladenen PDF extrahieren, fehlende Liedtexte (erste 5 Wörter) aus einer lokalen Referenzliste ergänzen und die Zielfelder des OBS-Docks vollautomatisch befüllen.
## 2. Technical Stack
- **Runtime:** HTML5 / Vanilla JavaScript (ES6+) innerhalb des OBS Custom Dock (Chromium-basiert).
- **PDF Parsing:** Mozilla `pdfjs-dist` via CDN (Client-side, kein Node.js Backend erforderlich).
- **LLM API:** OpenRouter Chat Completions mit Auto-Fallback über ein Modell-Array.
## 3. Workflow & Data Flow
1. **PDF Upload:** Benutzer wählt eine PDF-Datei im OBS Dock aus.
2. **Text Extraction:** PDF.js extrahiert den rohen Text zeilenweise.
3. **Reference Lookup:** Das Skript hält ein lokales JavaScript-Objekt (Dictionary) mit allen Liednummern und deren Volltexten bereit.
4. **LLM Prompting (OpenRouter):** 
   - Der extrahierte Text und die Referenzliste werden an OpenRouter gesendet.
   - Modell-Reihenfolge (Free): Llama-3.3-70b -> Gemini-2.5-Flash -> Qwen-2.5-7b.
   - System Prompt zwingt das Modell zu einem strikten JSON-Output.
5. **DOM Update:** Die UI-Eingabefelder (Input-Inputs/Textareas) des OBS Docks werden mit den gemappten Werten befüllt und Events (`input` / `change`) werden getriggert, damit die OBS Browser-Views die Updates sofort erhalten.
## 4. Error Handling
- **HTTP 429 (Rate Limit):** Wird automatisch durch das OpenRouter-Modell-Array abgefangen.
- **JSON Parsing Error:** Fallback auf Roh-Text-Anzeige, falls das LLM das Format bricht.

