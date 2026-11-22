# check-prereq.ps1 - Prod-PC Selbstcheck fuer das Textbox-Studio-Plugin
# Prueft: VC++ Redistributable, Windows-Version, OBS-Installation, Plugin-Dateien, Qt6, letztes OBS-Log.
# Reines ASCII, damit PowerShell ohne UTF-8-BOM nicht zerbricht.
$ok = $true
function Report([string]$label, [string]$status, [bool]$good) {
    $tag = "OK"
    if (-not $good) { $tag = "FEHLER"; $script:ok = $false }
    Write-Output ("[$tag] $label : $status")
}

# 1) Windows-Version (UCRT braucht Win 10+)
$win = (Get-CimInstance Win32_OperatingSystem).Version
$good = $false
if ([version]$win -ge [version]"10.0") { $good = $true }
Report "Windows" $win $good

# 2) VC++ 2015-2022 Redistributable x64
$reg = Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64" -ErrorAction SilentlyContinue
if ($reg) {
    Report "VC-Redist x64" ("installiert, Version " + $reg.Version) $true
} else {
    $sys = Test-Path "C:\Windows\System32\VCRUNTIME140_1.dll"
    Report "VC-Redist x64" "NICHT installiert - https://aka.ms/vc14/vc_redist.x64.exe ausfuehren" $sys
}
foreach ($d in "VCRUNTIME140.dll", "VCRUNTIME140_1.dll", "MSVCP140.dll") {
    $p = "C:\Windows\System32\$d"
    Report "System32 $d" ($(Test-Path $p)) $(Test-Path $p)
}

# 3) OBS-Installationsorte finden
$candidates = @()
$obsProc = Get-CimInstance Win32_Process -Filter "name='obs64.exe'" | Select-Object -First 1 -ExpandProperty ExecutablePath
if ($obsProc) { $candidates += (Split-Path (Split-Path $obsProc -Parent) -Parent) }
$candidates += "C:\Program Files\obs-studio"
$candidates += "C:\Program Files (x86)\obs-studio"
$candidates += "C:\Program Files (x86)\Steam\steamapps\common\OBS Studio"
$obsRoot = $null
foreach ($c in $candidates) {
    if (Test-Path ($c + "\bin\64bit\obs.dll")) { $obsRoot = $c; break }
}
if ($obsRoot) {
    Report "OBS-Wurzel" $obsRoot $true
} else {
    Report "OBS-Wurzel" "kein OBS mit bin\64bit\obs.dll gefunden" $false
    Write-Output "--- Ergebnis: PROBLEME gefunden"
    exit 1
}

# 4) Qt6 in OBS (Plugin importiert Qt6Core/Gui/Widgets)
foreach ($q in "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll") {
    $p = Join-Path $obsRoot "bin\64bit\$q"
    Report "OBS $q" ($(Test-Path $p)) $(Test-Path $p)
}

# 5) Plugin-Dateien an richtiger Stelle
$dll = Join-Path $obsRoot "obs-plugins\64bit\textbox-studio.dll"
if (Test-Path $dll) {
    $di = Get-Item $dll
    Report "Plugin-DLL" ($di.Length.ToString() + " Bytes, " + $di.LastWriteTime.ToString()) $true
} else {
    Report "Plugin-DLL" "fehlt - ZIP in OBS-Wurzel entpacken (obs-plugins UND data)" $false
}
$data = Join-Path $obsRoot "data\obs-plugins\textbox-studio"
if (Test-Path $data) {
    Report "Datenordner (templates)" $data $true
} else {
    Report "Datenordner (templates)" "fehlt - data-Ordner aus dem ZIP fehlt (haeufigster Fehler)" $false
}

# 6) Letztes OBS-Log: Plugin geladen?
$logDir = Join-Path $env:APPDATA "obs-studio\logs"
if (Test-Path $logDir) {
    $log = Get-ChildItem $logDir -Filter *.txt | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($log) {
        $txt = Get-Content $log.FullName
        $loaded = $txt | Select-String -Pattern "Plugin erfolgreich geladen"
        $failed = $txt | Select-String -Pattern "textbox-studio.dll" | Where-Object { $_.Line -match "Failed" }
        if ($loaded) {
            $line = ($loaded | Select-Object -First 1).Line.Trim()
            Report "Letztes Log ($($log.Name))" $line $true
        } elseif ($failed) {
            $msg = ($failed | Select-Object -First 2 | ForEach-Object { $_.Line.Trim() }) -join " ; "
            Report "Letztes Log ($($log.Name))" $msg $false
        } else {
            Report "Letztes Log ($($log.Name))" "keine textbox-Zeile - Plugin lief nie (Safe-Modus? DLL fehlt? VC-Redist?)" $false
        }
    } else {
        Report "OBS-Log" "kein Log gefunden - OBS wurde hier nie gestartet" $false
    }
} else {
    Report "OBS-Log" "kein Log-Ordner" $false
}

if ($ok) {
    Write-Output "--- Ergebnis: ALLE PREREQS OK - Plugin sollte laden"
    exit 0
} else {
    Write-Output "--- Ergebnis: PROBLEME gefunden (siehe FEHLER-Zeilen)"
    exit 1
}
