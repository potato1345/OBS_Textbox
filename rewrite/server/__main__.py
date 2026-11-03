"""Startpunkt: ``python -m server [--port 8080] [--host 0.0.0.0] [--plain]``."""

from __future__ import annotations

import argparse
import os
import socket
import subprocess
import sys

from .config import load_config
from .console import ConsoleDashboard, RequestStats, enable_ansi
from .http_app import create_server


def lan_address() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.connect(("10.255.255.255", 1))  # kein echter Traffic (UDP)
            return probe.getsockname()[0]
    except OSError:
        return "127.0.0.1"


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="python -m server", description="OBS Textbox Server")
    parser.add_argument("port_positional", nargs="?", type=int, help=argparse.SUPPRESS)
    parser.add_argument("--port", type=int, help="Port (Standard 8080)")
    parser.add_argument("--host", help="Bind-Adresse (Standard 0.0.0.0, nur lokal: 127.0.0.1)")
    parser.add_argument("--plain", action="store_true", help="Normales Log statt Live-Dashboard")
    return parser.parse_args(argv)


def close_parent_batch_window() -> None:
    """Unter Windows: Elternprozess cmd.exe beenden, um „Batchvorgang abbrechen (J/N)?“ zu vermeiden."""
    if os.name != "nt" or os.environ.get("OBS_TEXTBOX_FROM_BAT") != "1":
        return
    try:
        subprocess.call(["taskkill", "/F", "/PID", str(os.getppid())],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError:
        pass


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    config = load_config()
    if args.host:
        config.host = args.host
    if args.port or args.port_positional:
        config.port = args.port or args.port_positional

    plain = args.plain or not sys.stdout.isatty()
    stats = None if plain else RequestStats()

    try:
        server = create_server(config, stats)
    except OSError as err:
        print(f"Server konnte nicht auf Port {config.port} starten: {err}", file=sys.stderr)
        return 1

    dashboard = None
    if stats is not None:
        enable_ansi()
        dashboard = ConsoleDashboard(stats, config.port, lan_address())
        dashboard.start()
    else:
        print(f"OBS Textbox Server läuft: http://localhost:{config.port}/dashboard/  (Strg+C zum Beenden)")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if dashboard is not None:
            dashboard.stop()
            dashboard.join(timeout=2)
        print("Server beendet.")
        sys.stdout.flush()
    close_parent_batch_window()
    return 0


if __name__ == "__main__":
    sys.exit(main())
