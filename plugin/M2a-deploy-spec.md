# M2a-Deploy: Plugin deployen, OBS neu starten, Log verifizieren

## Kontext
- Repo: `C:\Users\Johann\Desktop\GitHub\OBS_Textbox\plugin` (OBS-Plugin "Liedplan", T7).
- Der vorherige agy-Run hat die Browser-Source-Implementierung gebaut. Frische DLL liegt in:
  `C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin/build_x64/RelWithDebInfo/liedplan-for-obs.dll`
- OBS Studio 32.2.2 läuft aktuell (PID im Tasklist prüfen) und hält die
  Deploy-DLL in `C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs-plugins\64bit\`
  in Beschlag ⇒ OBS muss erst beendet werden.
- Deploy + Restart wurde bisher von Hermes per Hand gemacht – dies ist der
  automatische Ablauff für agy (bash = git-bash auf Windows; PowerShell via
  `powershell.exe -NoProfile -Command "..."` aufrufen).

## Aufgabe (in genau dieser Reihenfolge)

### 1. Vorbedingung prüfen
- `build_x64/RelWithDebInfo/liedplan-for-obs.dll` existiert und ist JÜNGER als
  `src/liedplan-html.cpp` (ansonsten: erst `cmake --build build_x64 --config
  RelWithDebInfo` bauen mit PATH
  `export PATH="/c/Johann/tools/cmake-3.30.5-windows-x86_64/bin:$PATH"`).

### 2. OBS beenden
- `taskkill /F /IM obs64.exe` (Exit-Code 128/1 „läuft nicht" ist OK).
- Bis `tasklist | grep obs64` leer ist warten (max. 15 s, dann nochmal taskkill).

### 3. DLL deployen
- Kopie: `cp "C:/Users/Johann/Desktop/GitHub/OBS_Textbox/plugin/build_x64/RelWithDebInfo/liedplan-for-obs.dll"
  "C:/Program Files (x86)/Steam/steamapps/common/OBS Studio/obs-plugins/64bit/liedplan-for-obs.dll"`
- Schreibrechte: Steam-Verzeichnis ist user-schreibbar (bisher ohne Admin gelaufen).
  Fehlschlag → Fehler mit genauen Meldetext im Report, NICHT mit UAC/admin hacken.

### 4. OBS mit korrektem WorkingDirectory starten
- `powershell.exe -NoProfile -Command "Start-Process -FilePath 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio\obs64.exe' -WorkingDirectory 'C:\Program Files (x86)\Steam\steamapps\common\OBS Studio'"`
- WorkingDirectory ist WICHTIG (Theme-/CEF-Pfadprobleme sonst).
- 30 s warten, dann `tasklist | grep obs64` → neue PID notieren.

### 5. Log verifizieren
- Neueste Datei in `C:\Users\Johann\AppData\Roaming\obs-studio\logs\` (Datum heute,
  neueste mtime) lesen (Tail ~150 Zeilen + gezielte grep).
- Erhalten sein muss (Auszug in den Report):
  - `plugin loaded successfully` (Liedplan)
  - `Liedplan`-LOG_INFO-Zeilen (Dock/State/Scene)
  - Browser-Source "Browser" (bestehend) ohne CEF-Fehler
  - KEIN "crash", KEIN "failed to load" für Liedplan/browser
- Die Szene "Szene" soll alle 10 Sources laden (VLC, cam1, Browser,
  Videoaufnahmegerät, kamera, cam2, Liedplan box1, TextGDIPlus, Liedplan box13
  – box13 ist noch alter `liedplan_card`-Typ, darf schwarz bleiben; die
  Migration passiert später per UI-Klick, NICHT von agy auslösen).

### 6. Report (deutsch)
- Neue OBS-PID, Deploy-Pfad + Datei-Timestamp, Log-Auszug (erwartete Zeilen +
  Auffälligkeiten), ob alles grün ist.
- **Nicht** tun: keine UI-Interaktion (Hermes klickt "Zu Szene"), keine
  Screenshots, keine weiteren OBS-Aktionen, keine Code-Änderungen, kein
  Rebuild (außer Schritt 1 zwingt dazu).

## Constraints
- Änderungen außerhalb: KEINE (nur DLL-Kopie nach obs-plugins/64bit).
- Wenn OBS nach dem Start abgestürzt ist (Process weg nach 30 s): zweites
  Restart-Versuch + Log-Auszug des Fehlers in den Report, nicht weiter hacken.
