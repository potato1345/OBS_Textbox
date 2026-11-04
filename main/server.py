# -*- coding: utf-8 -*-
"""
server.py — Lokaler HTTP-Server und Dashboard für die OBS-Textbox (Version 1, main/).

Zweck:
  - Stellt die browserbasierte Steuerungs-Oberfläche (main/controlui/control.html) bereit.
  - Liefert statische Ressourcen (HTML, JS, CSS, Templates) aus.
  - Stellt API-Endpunkte für die Dokumentenkonvertierung (/api/parse-document via MarkItDown)
    und den Google-Forms-Import (/api/import/answers und /api/form-responses via forms_import.py)
    zur Verfügung.
  - Zeichnet ein interaktives Terminal-Dashboard mit Status und Zugriffszahlen.

Zusammenspiel:
  - Wird als lokaler Hilfsserver für Version 1 betrieben.
  - Kommuniziert mit main/forms_import.py für Google-Forms-Antworten.
  - Dient als Backend für das Frontend unter main/controlui/.
"""

import os
import sys
import time
import socket
import threading
import tempfile
import json
import posixpath
import urllib.parse
import logging
import forms_import
from http.server import HTTPServer, SimpleHTTPRequestHandler
from collections import defaultdict
from datetime import datetime

# Logger für Version 1 initialisieren
logger = logging.getLogger("liedplan-v1")

# ANSI Colors
C_CYAN = '\033[96m'
C_GREEN = '\033[92m'
C_YELLOW = '\033[93m'
C_RED = '\033[91m'
C_RESET = '\033[0m'
C_BOLD = '\033[1m'
C_WHITE = '\033[97m'

if os.name == 'nt':
    os.system("") # Enable ANSI escaping in Windows CMD
    import ctypes
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.GetStdHandle(-11)
    mode = ctypes.c_uint32()
    kernel32.GetConsoleMode(handle, ctypes.byref(mode))
    mode.value |= 0x0004
    kernel32.SetConsoleMode(handle, mode)

BANNER = f"""{C_GREEN}
 ██████╗ ██████╗ ███████╗     ████████╗███████╗██╗  ██╗████████╗██████╗  ██████╗ ██╗  ██╗
██╔════╝ ██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝
██║  ███╗██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝ 
██║   ██║██╔══██╗╚════██║        ██║   ██╔══╝   ██╔██╗    ██║   ██╔══██╗██║   ██║ ██╔██╗ 
╚██████╔╝██████╔╝███████║███████╗██║   ███████╗██╔╝ ██╗   ██║   ██████╔╝╚██████╔╝██╔╝ ██╗
 ╚═════╝ ╚═════╝ ╚══════╝╚══════╝╚═╝   ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═════╝  ╚═════╝ ╚═╝  ╚═╝{C_RESET}
"""

stats = {
    "start_time": time.time(),
    "total_requests": 0,
    "status_codes": defaultdict(int),
    "traffic": 0,
    "connected_ips": set(),
    "last_request_time": None,
    "recent_activity": []
}
stats_lock = threading.Lock()

def get_lan_ip():
    """
    Ermittelt die primäre lokale IPv4-Adresse des Rechners über einen
    Dummy-UDP-Socket (ohne tatsächlichen Netzwerk-Traffic).
    Gibt bei fehlender Netzwerkverbindung '127.0.0.1' zurück.
    """
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def get_bind_host():
    """
    Liefert die Bind-Adresse des Servers aus der Umgebungsvariable OBS_TEXTBOX_BIND.
    Standardmäßig wird nur auf Loopback ('127.0.0.1') gebunden; für Opt-in-LAN
    kann z. B. OBS_TEXTBOX_BIND=0.0.0.0 gesetzt werden.
    """
    raw = os.environ.get("OBS_TEXTBOX_BIND", "127.0.0.1")
    host = raw.strip() if raw is not None else ""
    return host if host != "" else "127.0.0.1"


def is_loopback_bind(bind_host=None):
    """Prüft, ob die gegebene (oder konfigurierte) Bind-Adresse eine Loopback-Adresse ist."""
    host = (bind_host if bind_host is not None else get_bind_host()).strip().lower()
    return host in ("127.0.0.1", "localhost", "::1") or host.startswith("127.")


def is_blocked_path(raw_path):
    """
    Prüft, ob ein angefragter GET-Pfad auf geschützte Dateien zeigt:
    Punkt-Dateien/-Ordner ('/.*'), Python-Skripte ('*.py', '*.pyc') oder 'api.env'.
    """
    url_path = urllib.parse.urlsplit(raw_path or "").path
    unquoted = urllib.parse.unquote(url_path).replace("\\", "/")
    raw_parts = [p for p in unquoted.split("/") if p and p != "."]
    if any(p == ".." or p.startswith(".") for p in raw_parts):
        return True
    norm_parts = [p for p in posixpath.normpath(unquoted).split("/") if p and p != "."]
    for part in norm_parts:
        low = part.lower()
        if part.startswith(".") or low == "api.env" or low.endswith(".env") or low.endswith(".py") or low.endswith(".pyc"):
            return True
    return False

class StatsHandler(SimpleHTTPRequestHandler):
    """
    HTTP-Request-Handler für das v1-Backend.
    Erfasst Request-Statistiken für das Terminal-Dashboard und routet
    API-Endpunkte für MarkItDown und Google Forms Import.
    """
    def log_message(self, format, *args):
        # Standard-Terminal-Ausgabe unterdrücken, um Dashboard-Anzeige nicht zu stören
        pass
        
    def log_error(self, format, *args):
        # Stderr-Fehlermeldungen unterdrücken
        pass
        
    def log_request(self, code='-', size='-'):
        """Erfasst Metriken (Status, Traffic, IP) thread-sicher für das Dashboard."""
        with stats_lock:
            stats["total_requests"] += 1
            stats["last_request_time"] = time.time()
            
            try:
                stats["status_codes"][int(code)] += 1
            except ValueError:
                pass
                
            if str(size).isdigit():
                stats["traffic"] += int(size)
                
            ip = self.client_address[0]
            if ip == "::1": ip = "127.0.0.1"
            stats["connected_ips"].add(ip)
            
            try:
                parts = self.requestline.split()
                method = parts[0] if len(parts) > 0 else "GET"
                path = parts[1] if len(parts) > 1 else "/"
            except Exception:
                method, path = "GET", "UNKNOWN"
            
            timestamp = datetime.now().strftime("%H:%M:%S")
            entry = f"[{timestamp}] {code} {method} {path} ({ip})"
            stats["recent_activity"].append(entry)
            
            if len(stats["recent_activity"]) > 10:
                stats["recent_activity"].pop(0)

    def _get_cors_origin(self):
        """
        Bestimmt den Access-Control-Allow-Origin-Header abhängig vom Bind-Modus:
        - Loopback-Bind (Standard '127.0.0.1'): beschränkt auf Loopback/Localhost-Origins.
        - Opt-in-LAN-Bind (z. B. OBS_TEXTBOX_BIND=0.0.0.0): '*' für Geräte im Kirchen-WLAN.
        """
        bind_host = getattr(getattr(self, "server", None), "bind_host", None)
        if not is_loopback_bind(bind_host):
            return "*"
        origin = self.headers.get("Origin", "").strip() if getattr(self, "headers", None) else ""
        if origin:
            try:
                parsed = urllib.parse.urlsplit(origin)
                hostname = (parsed.hostname or "").lower()
                if parsed.scheme in ("http", "https") and (
                    hostname in ("localhost", "127.0.0.1", "::1") or hostname.startswith("127.")
                ):
                    return origin
            except Exception:
                pass
        port = getattr(getattr(self, "server", None), "server_port", 8080)
        return f"http://127.0.0.1:{port}"

    def do_OPTIONS(self):
        """Behandelt CORS-Preflight-Anfragen des Browser-Dashboards."""
        logger.info("OPTIONS-Preflight: %s von %s", self.path, self.client_address[0])
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', self._get_cors_origin())
        self.send_header('Access-Control-Allow-Methods', 'POST, GET, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type, X-Filename')
        self.end_headers()

    def do_POST(self):
        """
        Behandelt Dokumenten- und Antwortendatei-Uploads:
          - /api/parse-document: Konvertiert Dokumente (PDF/DOCX) via MarkItDown
          - /api/import/answers: Importiert CSV/XLSX/ZIP-Antworten aus Google Forms
        """
        logger.info("POST-Anfrage empfangen: %s von %s", self.path, self.client_address[0])
        if self.path == '/api/parse-document':
            try:
                content_length = int(self.headers.get('Content-Length', 0))
                if content_length == 0:
                    logger.warning("POST /api/parse-document: Keine Daten empfangen")
                    self._send_json(400, {'success': False, 'error': 'Keine Datei empfangen'})
                    return
                if content_length > 25 * 1024 * 1024:
                    self.send_error(413)
                    return
                
                raw_filename = self.headers.get('X-Filename', 'document.pdf')
                filename = urllib.parse.unquote(raw_filename)
                _, ext = os.path.splitext(filename)
                if not ext:
                    ext = '.pdf'
                
                file_bytes = self.rfile.read(content_length)
                logger.info("Dokument-Upload: '%s' (%d Bytes)", filename, content_length)
                
                with tempfile.NamedTemporaryFile(delete=False, suffix=ext) as tmp:
                    tmp.write(file_bytes)
                    tmp_path = tmp.name

                try:
                    from markitdown import MarkItDown
                    md = MarkItDown()
                    result = md.convert(tmp_path)
                    markdown_text = result.text_content
                    logger.info("MarkItDown-Konvertierung erfolgreich: '%s'", filename)
                    self._send_json(200, {
                        'success': True,
                        'markdown': markdown_text,
                        'filename': filename
                    })
                except ImportError:
                    logger.error("MarkItDown ist nicht installiert")
                    self._send_json(500, {
                        'success': False,
                        'error': 'MarkItDown ist nicht installiert. Bitte `pip install "markitdown[all]"` ausführen.'
                    })
                except Exception as conv_err:
                    logger.error("Fehler bei MarkItDown-Konvertierung von '%s': %s", filename, conv_err)
                    self._send_json(500, {
                        'success': False,
                        'error': f'Fehler bei der Konvertierung: {str(conv_err)}'
                    })
                finally:
                    if os.path.exists(tmp_path):
                        try:
                            os.remove(tmp_path)
                        except Exception:
                            pass
            except Exception as e:
                logger.exception("Unerwarteter Serverfehler bei /api/parse-document: %s", e)
                self._send_json(500, {'success': False, 'error': f'Serverfehler: {str(e)}'})
            return

        if self.path == '/api/import/answers':
            try:
                content_length = int(self.headers.get('Content-Length', 0))
                if content_length == 0:
                    logger.warning("POST /api/import/answers: Keine Daten empfangen")
                    self._send_json(400, {'success': False, 'error': 'Keine Datei empfangen'})
                    return
                if content_length > 25 * 1024 * 1024:
                    self.send_error(413)
                    return

                raw_filename = self.headers.get('X-Filename', 'antwort.csv')
                filename = urllib.parse.unquote(raw_filename)
                _, ext = os.path.splitext(filename)
                if ext.lower() not in ('.csv', '.xlsx', '.zip'):
                    logger.warning("Nicht unterstütztes Dateiformat hochgeladen: '%s'", ext)
                    self._send_json(400, {
                        'success': False,
                        'error': 'Nicht unterstütztes Format. Bitte CSV, XLSX oder ZIP des Google-Formulars hochladen.'
                    })
                    return

                file_bytes = self.rfile.read(content_length)
                logger.info("Antwortdatei-Upload: '%s' (%d Bytes)", filename, content_length)
                with tempfile.NamedTemporaryFile(delete=False, suffix=ext.lower()) as tmp:
                    tmp.write(file_bytes)
                    tmp_path = tmp.name

                try:
                    from forms_import import parse_answers_file, to_gottesdienst
                    rows = parse_answers_file(tmp_path, filename)
                    if not rows:
                        logger.warning("Keine Antworten in Datei '%s' gefunden", filename)
                        self._send_json(400, {'success': False, 'error': 'Keine Antworten in der Datei gefunden.'})
                        return
                    dienst = to_gottesdienst(rows[-1])
                    logger.info("Import erfolgreich: '%s' (%d Antworten)", filename, len(rows))
                    self._send_json(200, {
                        'success': True,
                        'filename': filename,
                        'anzahl_antworten': len(rows),
                        'gottesdienst': dienst
                    })
                except ValueError as ve:
                    logger.warning("Validierungsfehler beim Import von '%s': %s", filename, ve)
                    self._send_json(400, {'success': False, 'error': str(ve)})
                except Exception as imp_err:
                    logger.error("Fehler beim Import von '%s': %s", filename, imp_err)
                    self._send_json(500, {'success': False, 'error': f'Fehler beim Import: {str(imp_err)}'})
                finally:
                    if os.path.exists(tmp_path):
                        try:
                            os.remove(tmp_path)
                        except Exception:
                            pass
            except Exception as e:
                logger.exception("Unerwarteter Serverfehler bei /api/import/answers: %s", e)
                self._send_json(500, {'success': False, 'error': f'Serverfehler: {str(e)}'})
            return

        logger.warning("Unbekannter POST-Endpunkt aufgerufen: %s", self.path)
        self.send_error(404, 'Endpoint not found')

    def do_GET(self):
        """
        Behandelt GET-Anfragen:
          - Blockiert Punkt-Dateien ('/.*'), '.py' und 'api.env' mit 404 (von jeder IP)
          - /api/form-responses: Holt aktuelle Antworten via Google Forms API
          - Sonstige Pfade: Statische Dateien ausliefern
        """
        if is_blocked_path(self.path):
            logger.warning("Blockierter Pfad abgelehnt (404): %s von %s", self.path, self.client_address[0])
            self.send_error(404, 'File not found')
            return
        if self.path == '/api/form-responses':
            logger.info("GET /api/form-responses aufgerufen von %s", self.client_address[0])
            try:
                dienst = forms_import.fetch_latest_response()
                logger.info("Google Forms API-Abfrage erfolgreich abgeschlossen")
                self._send_json(200, {
                    'success': True,
                    'quelle': 'google-api',
                    'gottesdienst': dienst
                })
            except forms_import.FormEmptyError:
                logger.warning("Google Forms API: Noch keine Antworten vorhanden")
                self._send_json(409, {'success': False, 'error': 'Noch keine Antworten im Google-Formular.'})
            except FileNotFoundError as fe:
                logger.error("Google Forms API Token-Datei fehlt: %s", fe)
                self._send_json(500, {'success': False, 'error': str(fe)})
            except Exception as e:
                logger.exception("Google Forms API-Abfrage fehlgeschlagen: %s", e)
                self._send_json(500, {'success': False, 'error': f'Google-Abfrage fehlgeschlagen: {str(e)}'})
            return
        super().do_GET()

    def _send_json(self, status_code, data):
        """Hilfsmethode zum Senden einer UTF-8-kodierten JSON-HTTP-Antwort mit CORS-Headern."""
        response_bytes = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status_code)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Content-Length', str(len(response_bytes)))
        self.send_header('Access-Control-Allow-Origin', self._get_cors_origin())
        self.end_headers()
        self.wfile.write(response_bytes)

def format_bytes(b):
    """Formatiert eine Bytezahl in lesbare Einheiten (B, KB, MB, GB, TB, PB)."""
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if b < 1024.0:
            return f"{b:.2f} {unit}"
        b /= 1024.0
    return f"{b:.2f} PB"

def format_uptime(seconds):
    """Formatiert Sekunden in HH:MM:SS."""
    hours, rem = divmod(int(seconds), 3600)
    minutes, secs = divmod(rem, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"

def draw_dashboard(port, lan_ip):
    """
    Rendert in einer Endlosschleife ein farbiges Terminal-Dashboard im
    Alternate Screen Buffer mit Status, Uptime und Anfragestatistiken.
    """
    # Wechseln in den "Alternate Screen Buffer", damit die normale Terminal-Historie nicht zugespammt wird!
    sys.stdout.write('\033[?1049h\033[2J\033[H')
    sys.stdout.flush()
    
    while True:
        with stats_lock:
            uptime = format_uptime(time.time() - stats["start_time"])
            reqs = stats["total_requests"]
            s_200 = stats["status_codes"].get(200, 0)
            s_304 = stats["status_codes"].get(304, 0)
            s_404 = stats["status_codes"].get(404, 0)
            s_other = sum(v for k, v in stats["status_codes"].items() if k not in (200, 304, 404))
            traffic = format_bytes(stats["traffic"])
            
            last_req = "Noch keine"
            if stats["last_request_time"]:
                diff = int(time.time() - stats["last_request_time"])
                last_req = f"vor {diff}s"
                
            ips = ", ".join(list(stats["connected_ips"])[-3:]) or "Keine"
            # Only keep the last 10 activities to save vertical space
            recent = list(stats["recent_activity"])[-10:]

        out = '\033[H' # Cursor to top-left
        
        # Helper to format a line with clear-to-end-of-line (\033[K) and newline
        def line(text=""):
            return text + '\033[K\n'
        
        # Banner (ohne leere Zeilen am Anfang/Ende)
        out += line(f"{C_WHITE} ██████╗ ██████╗ ███████╗     ████████╗███████╗██╗  ██╗████████╗██████╗  ██████╗ ██╗  ██╗{C_RESET}")
        out += line(f"{C_WHITE}██╔════╝ ██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝{C_RESET}")
        out += line(f"{C_WHITE}██║  ███╗██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝ {C_RESET}")
        out += line(f"{C_WHITE}██║   ██║██╔══██╗╚════██║        ██║   ██╔══╝   ██╔██╗    ██║   ██╔══██╗██║   ██║ ██╔██╗ {C_RESET}")
        out += line(f"{C_WHITE}╚██████╔╝██████╔╝███████║███████╗██║   ███████╗██╔╝ ██╗   ██║   ██████╔╝╚██████╔╝██╔╝ ██╗{C_RESET}")
        out += line(f"{C_WHITE} ╚═════╝ ╚═════╝ ╚══════╝╚══════╝╚═╝   ╚══════╝╚═╝  ╚═╝   ╚═╝   ╚═════╝  ╚═════╝ ╚═╝  ╚═╝{C_RESET}")
        out += line(f"{C_CYAN}{'='*75}{C_RESET}")
        out += line(f" {C_BOLD}STATUS:{C_RESET} {C_GREEN}ONLINE{C_RESET} auf Port {port}       {C_BOLD}LAUFZEIT:{C_RESET} {uptime}")
        out += line(f" {C_BOLD}LOKAL:{C_RESET}  http://localhost:{port}/controlui/control.html")
        out += line(f" {C_BOLD}LAN:{C_RESET}    http://{lan_ip}:{port}/controlui/control.html")
        out += line(f"{C_CYAN}{'='*75}{C_RESET}")
        out += line(f" {C_BOLD}STATISTIKEN:{C_RESET}")
        out += line(f"   Requests: {reqs}    | Traffic: {traffic:<10} | Letzter: {last_req}")
        out += line(f"   Status:   {C_GREEN}200 OK: {s_200}{C_RESET}  |  {C_YELLOW}304 Cache: {s_304}{C_RESET}  |  {C_RED}404 Fehler: {s_404}{C_RESET}  |  Andere: {s_other}")
        out += line(f"   Clients:  {ips}")
        out += line(f"{C_CYAN}{'='*75}{C_RESET}")
        out += line(f" {C_BOLD}LETZTE AKTIVITÄTEN (Max 10):{C_RESET}")
        
        if not recent:
            out += line("   Warte auf Anfragen...")
            for _ in range(9):
                out += line()
        else:
            for r in recent:
                if " 200 " in r:
                    r = r.replace(" 200 ", f" {C_GREEN}200{C_RESET} ")
                elif " 304 " in r:
                    r = r.replace(" 304 ", f" {C_YELLOW}304{C_RESET} ")
                elif " 404 " in r:
                    r = r.replace(" 404 ", f" {C_RED}404{C_RESET} ")
                else:
                    parts = r.split(" ")
                    if len(parts) >= 3 and parts[1].isdigit():
                        r = r.replace(f" {parts[1]} ", f" {C_YELLOW}{parts[1]}{C_RESET} ")
                
                # Begrenzen der Länge, damit lange URLs das Terminal nicht umbrechen lassen
                if len(r) > 105:
                    r = r[:102] + "..."
                out += line(f"   {r}")
            
            # Auffüllen mit leeren Zeilen für fixe Höhe
            for _ in range(10 - len(recent)):
                out += line()

        out += line(f"{C_CYAN}{'='*75}{C_RESET}")
        out += line(f" {C_BOLD}Drücke Strg+C um den Server zu beenden{C_RESET}")
        out += '\033[J' # Alles darunter säubern

        sys.stdout.write(out)
        sys.stdout.flush()
        
        time.sleep(1)

def main():
    """
    Hauptprogramm: Konfiguriert Logging, bindet den HTTPServer an Port (Standard: 8080)
    und startet den Hintergrund-Thread für das Terminal-Dashboard.
    """
    if not logging.getLogger().handlers:
        logging.basicConfig(
            level=logging.INFO,
            format="%(asctime)s [%(levelname)s] [%(name)s] %(message)s"
        )

    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
            
    lan_ip = get_lan_ip()
    bind_host = get_bind_host()
    server_address = (bind_host, port)
    
    try:
        httpd = HTTPServer(server_address, StatsHandler)
        httpd.bind_host = bind_host
        logger.info("Server-Start auf %s:%d (LAN-Adresse: %s)", bind_host, port, lan_ip)
    except OSError as e:
        logger.error("Fehler beim Starten des Servers auf Port %d: %s", port, e)
        print(f"Fehler beim Starten des Servers auf Port {port}: {e}")
        sys.exit(1)
        
    # Suppress output of broken pipe errors to avoid dirtying the dashboard
    sys.stderr = open(os.devnull, 'w')
    
    t = threading.Thread(target=draw_dashboard, args=(port, lan_ip), daemon=True)
    t.start()
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        logger.info("Server wird heruntergefahren (KeyboardInterrupt)")
        sys.stdout.write('\033[?1049l') # Switch back to normal screen buffer
        print("Server wurde beendet.")
        sys.stdout.flush()
        
        if os.name == 'nt':
            try:
                import subprocess
                ppid = os.getppid()
                out = subprocess.check_output(f'tasklist /FI "PID eq {ppid}" /NH /FO CSV', shell=True).decode()
                if 'cmd.exe' in out.lower():
                    # Kill the parent cmd.exe to suppress "Terminate batch job (Y/N)?"
                    subprocess.call(['taskkill', '/F', '/PID', str(ppid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass
                
        os._exit(0)

if __name__ == '__main__':
    main()
