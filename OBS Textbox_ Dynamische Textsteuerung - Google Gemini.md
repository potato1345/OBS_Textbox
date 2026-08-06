21.07.26, 17:48 

# **OBS Textbox: Dynamische Textsteuerung** 

https://gemini.google.com/app/c04c69f6b9daa011 

_User prompt: ich habe schon code für eine custom textbox geschrieben, die in obs mithilfe des browserviews und der lokale Datei option eingefügt wird. durch custom css und css variablen kann ich dann diese boxen aus obs gleich mit text befüllen. ich will jetzt aber um diese boxen ein besseres system bauen, was mir die Möglichkeit geben soll, dass ich auch den text von einer extra Seite im browser aktualisiere. die textbox soll dann falls die externe Seite verfügbar ist die custom css aus obs mit dem text der Webseite überschreiben. quasi ist der text noch immer über css aber jetzt gibt es theoretisch auch eine höher priorisierte externe Seite bei der dann der css-text über js geupdaten werden kann, um komplexere Optionen zum ausfüllen mehrerer Textboxen gleichzeitig z.b. mit einer ai api möglich machen soll, oder mir die Möglichkeit geben soll, den text einfach in einem ladbarem Profil zu speichern, so dass ich häufig verwendeten text nicht immer wieder beim austauschen manuell schreiben oder kopieren muss. hier ist mein aktueller code der auch so funktioniert:<!DOCTYPE html> <html lang="de"> <head>     <meta charset="UTF-8">     <meta name="viewport" content="width=device-width, initial-scale=1.0">     <title>OBS Textbox</title> <style>         /*  OBS CUSTOM CSS VARIABELEN */         :root {             /* TEXTZEILEN (CSS-Eingabe) */             --zeile-1: "";             --zeile-2: "";             --zeile-3: "";              /* DESIGN-EINSTELLUNGEN */             --box-bg: rgba(255, 255, 255, 0.75);                 --box-border: rgba(0, 0, 0, 0.7);                    --triangle-color: #fce647;                           --text-color: #000000;         }       /* generelles Styling */         body { background: gray;             margin: 0;             padding: 20px;              font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;         }       /* generelles Styling */         .textbox-container {             /* Textbox-Container */             position: relative;             background-color: var(--box-bg); /* Ecken /(Dreiecke) */             background-image:                  linear-gradient(135deg, var(--triangle-color) 50%, transparent 50%), linear-gradient(315deg, var(--triangle-color) 50%, transparent 50%);             background-position: top left, bottom right;             backgroundsize: 50px 50px, 50px 50px;             background-repeat: no-repeat;             /* Rand */             border: 0px solid var(--box-border);             /* Ecken abrundungen */             border-radius: 4px 4px 4px 4px;             /* Größen und Abstände */             min-width: 300px;             width: maxcontent; /* Passt sich der Textlänge an */             max-width: 800px;             min-height: 1.5em;  /* Schrumpft auf Minimum eine Zeile */ /* Padding: Textabstand zum Rand bzw. den Dreiecken */             padding: 15px 60px 20px 50px;                           display: flex; flex-direction: column;             justify-content: center;         }         /* Styling der Textzeilen */         .text-line {             line-height: 1.2;             color: var(--text-color);         }         /* CSS Elemente um Text einzufügen */         .text-line::before {             display: inline-block;             white-space: pre-wrap;              font-size: 32px;                }         /* Zuweisung der CSS-Variablen zu den Zeilen */         .line1::before { content: var(--zeile-1); } .line2::before { content: var(--zeile-2); }         .line3::before { content: var(--zeile-3); }         /* Styling der ersten Zeile */         .line1::before { font-size: 38px;                    font-weight: 500;                  margin-bottom: 5px;         }                  /* Styling der zweiten Zeile */ .line2::before {             font-size: 32px;             font-weight: 400;         }         /* Styling der dritten Zeile */         .line3::before {             font-size: 32px;             font-weight: 400;         }     </style> </head> <body>     <!-- HTML Struktur ohne Leerzeichen zwischen den Divs -->     <div class="textbox-container">                  <div class="text-line line1">         </div>                  <div class="text-line line2">         </div>                  <div class="text-line line3">         </div>     </div> </body> </html>_ 

Response: Das ist ein extrem cleverer Ansatz für ein OBS-Overlay! Der Trick, CSS-Variablen über JavaScript zu überschreiben (document.documentElement.style.setProperty), ist hier perfekt: **Wenn JavaScript neue Daten liefert, überschreibt es die OBSVariablen – falls nicht, greift automatisch das Standard-CSS aus OBS als Fallback.** 

Um das flexibel für Steuerung, KI-Anbindungen oder Profile zu machen, ist die **BroadcastChannel API** (für lokale Steuerung im selben Browser/Netzwerk) oder ein einfaches **Fetch-Polling** (für externe Server/APIs) ideal. 

## **1. Aktualisierter Code für die OBS-Textbox (index.html)** 

Füge am Ende deiner Datei vor dem schließenden </body>-Tag folgendes JavaScript-Skript ein. Es lauscht auf Live-Signale und unterstützt optional das Abfragen eines externen Webservers/einer API. 

<!DOCTYPE html> 

<html lang="de"> 

#### <head> 

- <meta charset="UTF-8"> 

<meta name="viewport" content="width=device-width, initial-scale=1.0"> <title>OBS Textbox</title> <style> 

/* OBS CUSTOM CSS VARIABLEN (Fallback) */ :root { 

- --zeile-1: "Standard Zeile 1"; 

--zeile-2: "Standard Zeile 2"; --zeile-3: "Standard Zeile 3"; 

/* DESIGN-EINSTELLUNGEN */ --box-bg: rgba(255, 255, 255, 0.75); --box-border: rgba(0, 0, 0, 0.7); --triangle-color: #fce647; --text-color: #000000; } 

body { 

background: transparent; /* Für OBS Transparenz */ margin: 0; padding: 20px; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; } .textbox-container { position: relative; background-color: var(--box-bg); background-image: linear-gradient(135deg, var(--triangle-color) 50%, transparent 50%), linear-gradient(315deg, var(--triangle-color) 50%, transparent 50%); background-position: top left, bottom right; background-size: 50px 50px, 50px 50px; background-repeat: no-repeat; border: 0px solid var(--box-border); 

Seite 1 von 10 

about:blank 

21.07.26, 17:48 

border-radius: 4px; min-width: 300px; width: max-content; max-width: 800px; min-height: 1.5em; padding: 15px 60px 20px 50px; display: flex; flex-direction: column; justify-content: center; } .text-line { line-height: 1.2; color: var(--text-color); } .text-line::before { display: inline-block; white-space: pre-wrap; font-size: 32px; } .line1::before { content: var(--zeile-1); font-size: 38px; font-weight: 500; margin-bottom: 5px; } .line2::before { content: var(--zeile-2); font-size: 32px; font-weight: 400; } .line3::before { content: var(--zeile-3); font-size: 32px; font-weight: 400; } </style> </head> <body> <div class="textbox-container"> <div class="text-line line1"></div> <div class="text-line line2"></div> <div class="text-line line3"></div> </div> <script> // Helper: CSS-Variable dynamisch aktualisieren function updateCssText(varName, text) { if (text === undefined || text === null) return; // Escape Anführungszeichen, damit CSS Syntax gültig bleibt const escapedText = `"${text.replace(/"/g, '\\"')}"`; document.documentElement.style.setProperty(varName, escapedText); } function updateAllLines(data) { if (data.zeile1 !== undefined) updateCssText('--zeile-1', data.zeile1); if (data.zeile2 !== undefined) updateCssText('--zeile-2', data.zeile2); if (data.zeile3 !== undefined) updateCssText('--zeile-3', data.zeile3); } // --- METHODE A: Lokaler BroadcastChannel (Echtzeit im selben Browser) --const channel = new BroadcastChannel('obs_textbox_channel'); channel.onmessage = (event) => { console.log("Neue Daten via Broadcast empfangen:", event.data); updateAllLines(event.data); }; // --- METHODE B: Externe API / Webserver Polling --const EXTERNAL_API_URL = ''; // z.B. 'http://localhost:3000/api/textbox' async function fetchExternalData() { if (!EXTERNAL_API_URL) return; try { const response = await fetch(EXTERNAL_API_URL); if (response.ok) { const data = await response.json(); updateAllLines(data); } } catch (err) { // Bei Fehler (z.B. Offline) greift automatisch das OBS Custom CSS console.log("API nicht erreichbar, nutze OBS Custom CSS Fallback."); } } // Alle 3 Sekunden externe API abfragen (falls URL gesetzt) if (EXTERNAL_API_URL) { setInterval(fetchExternalData, 3000); fetchExternalData(); } </script> </body> 

</html> 

Seite 2 von 10 

about:blank 

21.07.26, 17:48 

## **2. Test-Steuerung (Steuerungs-Dashboard)** 

Erstelle eine zweite HTML-Datei (z. B. control.html) und öffne sie einfach in deinem Browser. Damit kannst du direkt per **BroadcastChannel** Text live an das OBS-Overlay schicken oder vordefinierte Profile laden! 

<!DOCTYPE html> <html lang="de"> <head> <meta charset="UTF-8"> <title>OBS Textbox Controller</title> <style> body { font-family: sans-serif; padding: 20px; max-width: 400px; } input, button { display: block; width: 100%; margin-bottom: 10px; padding: 8px; } </style> </head> <body> <h2>OBS Textbox Steuerung</h2> <label>Zeile 1:</label> <input type="text" id="z1" placeholder="Titel / Name"> <label>Zeile 2:</label> <input type="text" id="z2" placeholder="Untertitel"> <label>Zeile 3:</label> <input type="text" id="z3" placeholder="Zusatzinfo"> <button onclick="sendText()">Senden</button> <hr> <h3>Schnell-Profile:</h3> <button onclick="applyProfile('Willkommen!', 'Gleich geht es los', 'Stream Start: 19:00 Uhr')">Profil: <button onclick="applyProfile('BRB - Bin gleich zurück', 'Kaffee holen ', '')">Profil: Pause</button <script> const channel = new BroadcastChannel('obs_textbox_channel'); function sendText() { const data = { zeile1: document.getElementById('z1').value, zeile2: document.getElementById('z2').value, zeile3: document.getElementById('z3').value }; channel.postMessage(data); } function applyProfile(z1, z2, z3) { document.getElementById('z1').value = z1; document.getElementById('z2').value = z2; document.getElementById('z3').value = z3; sendText(); } </script> </body> 

</html> 

## **Wie das System jetzt funktioniert** 

1. **Standard-Verhalten (Keine externe Quelle):** 

Das CSS nutzt standardmäßig die Werte aus :root (welche du in OBS im Feld _Benutzerdefiniertes CSS_ anpassen kannst). 

2. **Live-Steuerung über lokale Browser-Tab (control.html):** 

Über BroadcastChannel sendet die Steuerungsseite Änderungen direkt an das OBS Browser Source Fenster. 

3. **Anbindung von KI-APIs oder externen Servern:** 

Du kannst entweder in der control.html einen KI-Prompt ausführen und das Ergebnis an den Kanal senden, oder du trägst in index.html eine EXTERNAL_API_URL ein (z. B. einen lokalen Node.js/Python Server). 

_User prompt: ok passe den code von der obs-box und dem server jetzt noch leicht an, sodass es möglich ist den obs-box code für mehrere Textboxen z.b. mit einer id leicht anzupassen, um dann verschiedene Textboxen in obs einfügen zu können, die sich dann individuell im separaten browser steuern lassen_ 

Response: Um das System für mehrere Textboxen tauglich zu machen, nutzen wir eine **ID** , die wir der OBS-Box einfach über die URL mitgeben (z. B. ?id=box1). 

Die Steuerungsseite (control.html) erhält ein Dropdown-Menü, mit dem du auswählst, an welche Box der Text gesendet werden soll. So 

Seite 3 von 10 

about:blank 

21.07.26, 17:48 

brauchst du **nur noch eine einzige HTML-Datei für OBS** , kannst sie aber beliebig oft als Quelle einfügen. 

Hier ist der angepasste Code: 

### **1. Die OBS Textbox (index.html)** 

Das JavaScript wurde so angepasst, dass es seine ID aus der URL ausliest. Empfängt es eine Nachricht, prüft es, ob die Nachricht für diese ID (oder für "alle") bestimmt ist. 

|<!DOCTYPE html><br><html lang="de"><br><head><br><meta charset="UTF-8"><br><meta name="viewport" content="width=device-width, initial-scale=1.0"><br><title>OBS Textbox</title><br><style><br>/* OBS CUSTOM CSS VARIABLEN (Fallback) */<br>:root {<br>--zeile-1: "";<br>--zeile-2: "";<br>--zeile-3: "";<br>--box-bg: rgba(255, 255, 255, 0.75);<br>--box-border: rgba(0, 0, 0, 0.7);<br>--triangle-color: #fce647;<br>--text-color: #000000;<br>}<br>body {<br>background: transparent;<br>margin: 0;<br>padding: 20px;<br>font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;<br>/* Verstecke die Box komplett, wenn kein Text da ist */<br>opacity: 1;<br>transition: opacity 0.3s ease;<br>}<br>.textbox-container {<br>position: relative;<br>background-color: var(--box-bg);<br>background-image:<br>linear-gradient(135deg, var(--triangle-color) 50%, transparent 50%),<br>linear-gradient(315deg, var(--triangle-color) 50%, transparent 50%);<br>background-position: top left, bottom right;<br>background-size: 50px 50px, 50px 50px;<br>background-repeat: no-repeat;<br>border: 0px solid var(--box-border);<br>border-radius: 4px;<br>min-width: 300px;<br>width: max-content;<br>max-width: 800px;<br>min-height: 1.5em;<br>padding: 15px 60px 20px 50px;<br>display: flex;<br>flex-direction: column;<br>justify-content: center;<br>}<br>.text-line { line-height: 1.2; color: var(--text-color); }<br>.text-line::before { display: inline-block; white-space: pre-wrap; font-size: 32px; }<br>.line1::before { content: var(--zeile-1); font-size: 38px; font-weight: 500; margin-bottom: 5px; }<br>.line2::before { content: var(--zeile-2); font-size: 32px; font-weight: 400; }<br>.line3::before { content: var(--zeile-3); font-size: 32px; font-weight: 400; }<br></style><br></head><br><body><br><div class="textbox-container" id="box-container"><br><div class="text-line line1"></div><br><div class="text-line line2"></div><br><div class="text-line line3"></div><br></div><br><script><br>// Lese die ID aus der URL (z.B. index.html?id=lowerthird)<br>const urlParams = new URLSearchParams(window.location.search);<br>const myBoxId = urlParams.get('id') || 'default';<br>console.log("Diese Box hat die ID:", myBoxId);<br>function updateCssText(varName, text) {<br>if (text === undefined || text === null) return;<br>const escapedText = `"${text.replace(/"/g, '\\"')}"`;|
|---|



Seite 4 von 10 

about:blank 

21.07.26, 17:48 

document.documentElement.style.setProperty(varName, escapedText); } function updateAllLines(data) { if (data.zeile1 !== undefined) updateCssText('--zeile-1', data.zeile1); if (data.zeile2 !== undefined) updateCssText('--zeile-2', data.zeile2); if (data.zeile3 !== undefined) updateCssText('--zeile-3', data.zeile3); // Optional: Box ausblenden, wenn alle Felder leer sind const container = document.getElementById('box-container'); if(!data.zeile1 && !data.zeile2 && !data.zeile3) { container.style.opacity = "0"; } else { container.style.opacity = "1"; } } // --- LOKALE STEUERUNG --const channel = new BroadcastChannel('obs_textbox_channel'); channel.onmessage = (event) => { const data = event.data; // Prüfen, ob die Nachricht für diese Box oder für alle gedacht ist if (data.targetId === myBoxId || data.targetId === 'all') { updateAllLines(data); } }; // --- EXTERNE API STEUERUNG (Falls du einen Server nutzt) --// Übergibt die myBoxId an den Server, damit dieser weiß, welche Daten er schicken soll const EXTERNAL_API_URL = ''; // z.B. 'http://localhost:3000/api/textbox' async function fetchExternalData() { if (!EXTERNAL_API_URL) return; try { // Hänge die ID an die Server-Anfrage an const response = await fetch(`${EXTERNAL_API_URL}?id=${myBoxId}`); if (response.ok) { const data = await response.json(); updateAllLines(data); } } catch (err) { console.log("API nicht erreichbar."); } } if (EXTERNAL_API_URL) { setInterval(fetchExternalData, 3000); fetchExternalData(); } </script> 

</body> 

</html> 

### **2. Die Steuerung (control.html)** 

Hier wurde ein <select>-Element hinzugefügt. Das JavaScript liest das ausgewählte Ziel aus und hängt es als targetId an das Datenpaket an. 

<!DOCTYPE html> <html lang="de"> <head> <meta charset="UTF-8"> <title>OBS Textbox Controller</title> <style> body { font-family: sans-serif; padding: 20px; max-width: 400px; background: #f4f4f9;} .panel { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0 input, select, button { display: block; width: 100%; margin-bottom: 15px; padding: 10px; border: 1 button { background: #007bff; color: white; border: none; cursor: pointer; font-weight: bold; } button:hover { background: #0056b3; } .btn-clear { background: #dc3545; } .btn-clear:hover { background: #a71d2a; } </style> </head> <body> <div class="panel"> <h2>OBS Steuerung</h2> <label><b>Ziel-Textbox:</b></label> <select id="targetBox"> <option value="all">An ALLE Boxen senden</option> <option value="lowerthird">Lower Third (?id=lowerthird)</option> <option value="info">Info-Tafel (?id=info)</option> <option value="alert">Alert-Box (?id=alert)</option> 

Seite 5 von 10 

about:blank 

21.07.26, 17:48 

</select> 

<label>Zeile 1:</label> <input type="text" id="z1" placeholder="Titel / Name"> <label>Zeile 2:</label> <input type="text" id="z2" placeholder="Untertitel"> <label>Zeile 3:</label> <input type="text" id="z3" placeholder="Zusatzinfo"> <button onclick="sendText()">Text Aktualisieren</button> <button class="btn-clear" onclick="clearText()">Box leeren (Ausblenden)</button> <hr style="margin: 20px 0; border: 0; border-top: 1px solid #eee;"> <h3>Schnell-Profile:</h3> <button onclick="applyProfile('Willkommen!', 'Der Stream startet gleich', '')">Start-Profil laden< </div> <script> const channel = new BroadcastChannel('obs_textbox_channel'); function sendText() { const data = { targetId: document.getElementById('targetBox').value, zeile1: document.getElementById('z1').value, zeile2: document.getElementById('z2').value, zeile3: document.getElementById('z3').value }; channel.postMessage(data); } function clearText() { document.getElementById('z1').value = ""; document.getElementById('z2').value = ""; document.getElementById('z3').value = ""; sendText(); } function applyProfile(z1, z2, z3) { document.getElementById('z1').value = z1; document.getElementById('z2').value = z2; document.getElementById('z3').value = z3; sendText(); } </script> </body> </html> 

### **So bindest du es jetzt in OBS ein:** 

Da OBS in der regulären Dateiauswahl keine URL-Parameter erlaubt, musst du die Datei über einen kleinen Umweg als URL laden: 

1. Füge eine neue **Browser-Quelle** in OBS hinzu. 

2. **Deaktiviere** das Häkchen bei "Lokale Datei" (Wichtig!). 

3. Gib in das Feld "URL" den Pfad zu deiner lokalen Datei ein und hänge ?id=deineId an. Das sieht bei Windows zum Beispiel so aus: file:///C:/Users/DeinName/Documents/index.html?id=lowerthird 

4. Füge eine zweite Browser-Quelle hinzu, nutze die gleiche Methode und nenne sie am Ende z. B. ?id=info. 

5. Über das Dropdown-Menü in der control.html kannst du nun gezielt bestimmen, welche Box welchen Text erhält. 

_User prompt: kann ich die id nicht auch im custom css konfigurieren? und kann nicht jede textbox mit den drei Zeilen auf einmal angezeigt werden, mit je textbox eine kleine bearbeitbare Überschrift, die als id gilt. also obs mit custom css variablen für die Zeilen falls es keine externen text gibt und einer id mit der dann von der externen Seite dann falls verfügbar der text geholt werden kann. auf der externen Seite gibt es dann einfach untereinander für jede hinzugefügte box eine bearbeitbare id und der text der über diese id freigegeben wird_ 

Response: Das ist eine hervorragende Idee! Den Umweg über die URL wegzulassen und die ID direkt im OBS-Feld für das _Benutzerdefinierte CSS_ festzulegen, macht den Workflow in OBS viel sauberer. 

Die Steuerungsseite passen wir so an, dass sie wie ein **Dashboard** funktioniert: Alle Boxen werden gleichzeitig als "Karten" nebeneinander angezeigt. Der Titel der Karte ist ein bearbeitbares Textfeld, das gleichzeitig als ID dient. 

Hier ist das komplett angepasste System: 

### **1. Die OBS Textbox (index.html)** 

Du ziehst diese Datei ganz normal als lokale Datei in OBS. Im Feld "Benutzerdefiniertes CSS" in OBS kannst du nun für jede Quelle einfach die Variable --box-id anpassen. 

Seite 6 von 10 

about:blank 

21.07.26, 17:48 

<!DOCTYPE html> <html lang="de"> <head> <meta charset="UTF-8"> <meta name="viewport" content="width=device-width, initial-scale=1.0"> <title>OBS Textbox</title> <style> /* OBS CUSTOM CSS VARIABLEN */ :root { /* NEU: Hier vergibst du die ID für diese spezifische OBS-Quelle */ --box-id: "box1"; /* TEXT-FALLBACK (wird gezeigt, wenn JS noch nichts gesendet hat) */ --zeile-1: "Fallback Titel"; --zeile-2: "Wartet auf Steuerung..."; --zeile-3: ""; /* DESIGN-EINSTELLUNGEN */ --box-bg: rgba(255, 255, 255, 0.75); --box-border: rgba(0, 0, 0, 0.7); --triangle-color: #fce647; --text-color: #000000; } body { background: transparent; margin: 0; padding: 20px; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; opacity: 1; transition: opacity 0.3s ease; } .textbox-container { position: relative; background-color: var(--box-bg); background-image: linear-gradient(135deg, var(--triangle-color) 50%, transparent 50%), linear-gradient(315deg, var(--triangle-color) 50%, transparent 50%); background-position: top left, bottom right; background-size: 50px 50px, 50px 50px; background-repeat: no-repeat; border: 0px solid var(--box-border); border-radius: 4px; min-width: 300px; width: max-content; max-width: 800px; min-height: 1.5em; padding: 15px 60px 20px 50px; display: flex; flex-direction: column; justify-content: center; } .text-line { line-height: 1.2; color: var(--text-color); } .text-line::before { display: inline-block; white-space: pre-wrap; font-size: 32px; } .line1::before { content: var(--zeile-1); font-size: 38px; font-weight: 500; margin-bottom: 5px; } .line2::before { content: var(--zeile-2); font-size: 32px; font-weight: 400; } .line3::before { content: var(--zeile-3); font-size: 32px; font-weight: 400; } </style> </head> <body> <div class="textbox-container" id="box-container"> <div class="text-line line1"></div> <div class="text-line line2"></div> <div class="text-line line3"></div> </div> <script> // Liest die ID live aus den CSS-Variablen von OBS aus function getMyId() { const rootStyles = getComputedStyle(document.documentElement); let rawId = rootStyles.getPropertyValue('--box-id'); // Anführungszeichen und Leerzeichen entfernen, damit die ID sauber ist return rawId.replace(/["']/g, "").trim(); } function updateCssText(varName, text) { if (text === undefined || text === null) return; const escapedText = `"${text.replace(/"/g, '\\"')}"`; document.documentElement.style.setProperty(varName, escapedText); } 

Seite 7 von 10 

about:blank 

21.07.26, 17:48 

function updateAllLines(data) { if (data.zeile1 !== undefined) updateCssText('--zeile-1', data.zeile1); if (data.zeile2 !== undefined) updateCssText('--zeile-2', data.zeile2); if (data.zeile3 !== undefined) updateCssText('--zeile-3', data.zeile3); const container = document.getElementById('box-container'); if(!data.zeile1 && !data.zeile2 && !data.zeile3) { container.style.opacity = "0"; // Box ausblenden, wenn alles leer ist } else { container.style.opacity = "1"; } } // --- LOKALE STEUERUNG --const channel = new BroadcastChannel('obs_textbox_channel'); channel.onmessage = (event) => { const data = event.data; const myBoxId = getMyId(); // ID bei jeder Nachricht neu auslesen, falls in OBS geändert if (data.targetId === myBoxId || data.targetId === 'all') { updateAllLines(data); } }; </script> </body> 

</html> 

### **2. Das Steuerungs-Dashboard (control.html)** 

Diese Seite nutzt CSS-Grid, um dir alle Boxen übersichtlich anzuzeigen. Du kannst neue Boxen dynamisch hinzufügen und die Titel (IDs) direkt ändern. 

<!DOCTYPE html> <html lang="de"> <head> <meta charset="UTF-8"> <title>OBS Dashboard</title> <style> body { font-family: sans-serif; padding: 20px; background: #eef2f5; margin: 0; } h1 { margin-top: 0; color: #333; } /* Grid-Layout für die Karten */ .dashboard { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 20px; } /* Styling der einzelnen Box-Karten */ .card { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.05); border-top: 4px solid #007bff; } /* Die bearbeitbare ID (Überschrift) */ .id-input { font-size: 1.4em; font-weight: bold; color: #007bff; border: none; border-bottom: 2px dashed #ccc; width: 100%; margin-bottom: 15px; padding-bottom: 5px; background: transparent; } .id-input:focus { border-bottom-color: #007bff; outline: none; } label { font-size: 0.85em; color: #666; font-weight: bold; margin-bottom: 4px; display: block; } input[type="text"].text-input { width: 100%; padding: 8px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box; } /* Buttons */ .btn-group { display: flex; gap: 10px; } button { flex: 1; padding: 10px; border: none; border-radius: 4px; cursor: pointer; font-weight: bold; color: white; } 

Seite 8 von 10 

about:blank 

21.07.26, 17:48 

.btn-send { background: #28a745; } .btn-send:hover { background: #218838; } .btn-clear { background: #dc3545; } .btn-clear:hover { background: #c82333; } .btn-add { background: #343a40; margin-bottom: 20px; width: auto; padding: 10px 20px;} </style> </head> <body> <h1>OBS Textbox Dashboard</h1> <button class="btn-add" onclick="addNewCard()">+ Neue Box hinzufügen</button> <div class="dashboard" id="dashboard"> <!-- Hier werden die Boxen gerendert --> </div> <!-- Template für eine einzelne Box-Karte --> <template id="card-template"> <div class="card"> <!-- Bearbeitbare ID als Titel --> <input type="text" class="id-input" value="box1" title="Dies ist die ID für OBS (--box-id)"> <label>Zeile 1</label> <input type="text" class="text-input z1" placeholder="Haupttext..."> <label>Zeile 2</label> <input type="text" class="text-input z2" placeholder="Untertext..."> <label>Zeile 3</label> <input type="text" class="text-input z3" placeholder="Zusatz..."> <div class="btn-group"> <button class="btn-send" onclick="sendCard(this)">Senden</button> <button class="btn-clear" onclick="clearCard(this)">Leeren</button> </div> </div> </template> <script> const channel = new BroadcastChannel('obs_textbox_channel'); const dashboard = document.getElementById('dashboard'); const template = document.getElementById('card-template'); // Funktion zum Senden der Daten einer spezifischen Karte function sendCard(buttonElement) { // Finde die Karte, in der dieser Button geklickt wurde const card = buttonElement.closest('.card'); // Lese die Werte aus genau dieser Karte aus const targetId = card.querySelector('.id-input').value.trim(); const data = { targetId: targetId, zeile1: card.querySelector('.z1').value, zeile2: card.querySelector('.z2').value, zeile3: card.querySelector('.z3').value }; channel.postMessage(data); } // Funktion zum Leeren (Ausblenden) einer spezifischen Karte function clearCard(buttonElement) { const card = buttonElement.closest('.card'); card.querySelector('.z1').value = ""; card.querySelector('.z2').value = ""; card.querySelector('.z3').value = ""; sendCard(buttonElement); // Sendet leere Daten, Box blendet sich aus } // Fügt eine neue leere Karte zum Dashboard hinzu function addNewCard(idName = "neue_box") { const clone = template.content.cloneNode(true); clone.querySelector('.id-input').value = idName; dashboard.appendChild(clone); } // Starte beim ersten Öffnen standardmäßig mit zwei Boxen addNewCard("lowerthird"); addNewCard("info"); </script> </body> 

</html> 

Seite 9 von 10 

about:blank 

21.07.26, 17:48 

### **Wie du es jetzt in OBS konfigurierst:** 

1. Füge die index.html als **Lokale Datei** (oder mit deaktiviertem Häkchen als lokaler Pfad) in OBS hinzu. 

2. Scrolle in der Browser-Quelle nach unten zum Feld . 

3. Dort steht standardmäßig etwas wie body { background-color: rgba(0, 0, 0, 0); ... }. 

4. Füge dort einfach deine Variablen ein: 

:root { 

- --box-id: "lowerthird"; 

} 

5. Kopiere die gleiche Browser-Quelle in OBS (oder lege eine neue an) und schreibe ins CSS: 

:root { 

- --box-id: "info"; 

} 

6. Auf deiner control.html passt du nun einfach die große blaue Überschrift der jeweiligen Karte so an, dass sie lowerthird bzw. info heißt. Wenn du "Senden" drückst, reagiert nur die passende Box in OBS. 

Seite 10 von 10 

about:blank 

