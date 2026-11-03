"""Live-Statusanzeige im Terminal (Requests, Traffic, letzte Aktivitäten)."""

from __future__ import annotations

import os
import sys
import threading
import time
from collections import Counter, deque
from dataclasses import dataclass, field
from datetime import datetime

RESET, BOLD = "\033[0m", "\033[1m"
GREEN, YELLOW, RED, CYAN, WHITE, DIM = (
    "\033[92m", "\033[93m", "\033[91m", "\033[96m", "\033[97m", "\033[2m"
)

LOGO = r"""
  ___  ___ ___   _____       _   _
 / _ \| _ ) __| |_   _|____ _| |_| |__  _____ __
| (_) | _ \__ \   | |/ -_) \ /  _| '_ \/ _ \ \ /
 \___/|___/___/   |_|\___/_\_\\__|_.__/\___/_\_\
""".strip("\n")


def enable_ansi() -> None:
    """Aktiviert ANSI-Escape-Sequenzen in der Windows-Konsole."""
    if os.name != "nt":
        return
    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(handle, mode.value | 0x0004)
    except Exception:
        pass


def human_bytes(amount: float) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if amount < 1024:
            return f"{amount:.1f} {unit}" if unit != "B" else f"{int(amount)} B"
        amount /= 1024
    return f"{amount:.1f} TB"


def human_duration(seconds: float) -> str:
    seconds = int(seconds)
    return f"{seconds // 3600:02d}:{seconds % 3600 // 60:02d}:{seconds % 60:02d}"


@dataclass
class RequestStats:
    started: float = field(default_factory=time.time)
    total: int = 0
    traffic: int = 0
    last_request: float | None = None
    status: Counter = field(default_factory=Counter)
    clients: dict = field(default_factory=dict)
    recent: deque = field(default_factory=lambda: deque(maxlen=10))
    lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    last_aging: float = field(default=0.0, repr=False)

    def record(self, method: str, path: str, status: int, size: int, client: str) -> None:
        if client == "::1":
            client = "127.0.0.1"
        now = time.time()
        with self.lock:
            self.total += 1
            self.traffic += max(size, 0)
            self.last_request = now
            self.status[status] += 1
            self.clients[client] = now
            # Aging (stündlich prüfen, nicht pro Request): Clients, die länger als
            # 24 h inaktiv waren, aus dem Dict ausmisten.
            if now - self.last_aging > 3600:
                self.last_aging = now
                stale = [ip for ip, last in self.clients.items() if now - last > 24 * 3600]
                for ip in stale:
                    del self.clients[ip]
            stamp = datetime.now().strftime("%H:%M:%S")
            self.recent.append((stamp, status, method, path, client))

    def snapshot(self) -> dict:
        with self.lock:
            return {
                "uptime": time.time() - self.started,
                "total": self.total,
                "traffic": self.traffic,
                "last": self.last_request,
                "status": dict(self.status),
                "clients": sorted(self.clients, key=self.clients.get, reverse=True)[:3],
                "recent": list(self.recent),
            }


def _status_color(code: int) -> str:
    if 200 <= code < 300:
        return GREEN
    if code == 304 or 300 <= code < 400:
        return YELLOW
    return RED


class ConsoleDashboard(threading.Thread):
    """Zeichnet einmal pro Sekunde eine feste Statusseite im Alternate-Screen."""

    def __init__(self, stats: RequestStats, port: int, lan_ip: str, width: int = 76) -> None:
        super().__init__(daemon=True, name="console-dashboard")
        self.stats, self.port, self.lan_ip, self.width = stats, port, lan_ip, width
        self._stop = threading.Event()

    def stop(self) -> None:
        self._stop.set()

    def run(self) -> None:
        sys.stdout.write("\033[?1049h\033[?25l\033[2J")
        try:
            while not self._stop.is_set():
                sys.stdout.write(self.render())
                sys.stdout.flush()
                self._stop.wait(1.0)
        finally:
            sys.stdout.write("\033[?25h\033[?1049l")
            sys.stdout.flush()

    def render(self) -> str:
        snap = self.stats.snapshot()
        rule = f"{CYAN}{'─' * self.width}{RESET}"
        lines = [f"{WHITE}{row}{RESET}" for row in LOGO.splitlines()]
        last = "–" if snap["last"] is None else f"vor {int(time.time() - snap['last'])}s"
        codes = snap["status"]
        ok = sum(v for k, v in codes.items() if 200 <= k < 300)
        cached = codes.get(304, 0)
        errors = sum(v for k, v in codes.items() if k >= 400)
        lines += [
            rule,
            f" {BOLD}Status{RESET}   {GREEN}● ONLINE{RESET}  Port {self.port}"
            f"      {BOLD}Laufzeit{RESET} {human_duration(snap['uptime'])}",
            f" {BOLD}Lokal{RESET}    http://localhost:{self.port}/dashboard/",
            f" {BOLD}LAN{RESET}      http://{self.lan_ip}:{self.port}/dashboard/",
            f" {BOLD}Overlay{RESET}  http://localhost:{self.port}/textbox.html",
            rule,
            f" Requests {snap['total']:<6}  Traffic {human_bytes(snap['traffic']):<10}  Letzter {last}",
            f" {GREEN}2xx {ok}{RESET}   {YELLOW}304 {cached}{RESET}   {RED}Fehler {errors}{RESET}",
            f" Clients  {', '.join(snap['clients']) or '–'}",
            rule,
            f" {BOLD}Letzte Aktivitäten{RESET}",
        ]
        recent = snap["recent"]
        for stamp, status, method, path, client in recent:
            text = f"{method} {path}"
            if len(text) > self.width - 30:
                text = text[: self.width - 33] + "..."
            lines.append(
                f"  {DIM}{stamp}{RESET} {_status_color(status)}{status}{RESET} {text} {DIM}({client}){RESET}"
            )
        if not recent:
            lines.append(f"  {DIM}Warte auf Anfragen…{RESET}")
        lines += [""] * (10 - max(len(recent), 1))
        lines += [rule, f" {DIM}Strg+C beendet den Server{RESET}"]
        return "\033[H" + "".join(f"{line}\033[K\n" for line in lines) + "\033[J"
