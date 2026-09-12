import os
import sys
import time
import socket
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler
from collections import defaultdict
from datetime import datetime

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
██╔═══██╗██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝
██║   ██║██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝ 
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
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

class StatsHandler(SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass # Suppress default terminal spam
        
    def log_error(self, format, *args):
        pass # Suppress stderr errors (e.g. broken pipe)
        
    def log_request(self, code='-', size='-'):
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

def format_bytes(b):
    for unit in ['B', 'KB', 'MB', 'GB', 'TB']:
        if b < 1024.0:
            return f"{b:.2f} {unit}"
        b /= 1024.0
    return f"{b:.2f} PB"

def format_uptime(seconds):
    hours, rem = divmod(int(seconds), 3600)
    minutes, secs = divmod(rem, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"

def draw_dashboard(port, lan_ip):
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
        out += line(f"{C_WHITE}██╔═══██╗██╔══██╗██╔════╝     ╚══██╔══╝██╔════╝╚██╗██╔╝╚══██╔══╝██╔══██╗██╔═══██╗╚██╗██╔╝{C_RESET}")
        out += line(f"{C_WHITE}██║   ██║██████╔╝███████╗        ██║   █████╗   ╚███╔╝    ██║   ██████╔╝██║   ██║ ╚███╔╝{C_RESET}")
        out += line(f"{C_WHITE}██║   ██║██╔══██╗╚════██║        ██║   ██╔══╝   ██╔██╗    ██║   ██╔══██╗██║   ██║ ██╔██╗{C_RESET}")
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
    port = 8080
    if len(sys.argv) > 1:
        try:
            port = int(sys.argv[1])
        except ValueError:
            pass
            
    lan_ip = get_lan_ip()
    server_address = ('', port)
    
    try:
        httpd = HTTPServer(server_address, StatsHandler)
    except OSError as e:
        print(f"Fehler beim Starten des Servers auf Port {port}: {e}")
        sys.exit(1)
        
    # Suppress output of broken pipe errors to avoid dirtying the dashboard
    sys.stderr = open(os.devnull, 'w')
    
    t = threading.Thread(target=draw_dashboard, args=(port, lan_ip), daemon=True)
    t.start()
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
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

