"""
Konfigurationsverwaltung für Version 2 (rewrite/server/).

Zweck:
  - Lädt Konfigurationseinstellungen hierarchisch:
      Standardwerte < config.json < Umgebungsvariablen < CLI-Argumente
  - Stellt typisierte Konfigurationswerte (Host, Port, Token-Pfade, Upload-Limits)
    über die Dataclass Config bereit.
  - Ermöglicht das Auslesen des OpenRouter-API-Keys aus api.env (ohne Logging von Secrets).

Zusammenspiel:
  - Wird von __main__.py und http_app.py zur Laufzeit verwendet.
"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass, field
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

DEFAULT_FORM_ID = "1jreAuzd-jt1x48WDmjaP7v0OusdVO2lb55Kt4JrhEgY"
DEFAULT_TOKEN_PATH = os.path.join("%LOCALAPPDATA%", "hermes", "google_token.json")


@dataclass
class Config:
    """
    Zentrale Konfiguration für den v2 HTTP-Server und Importdienste.
    """
    host: str = "0.0.0.0"
    port: int = 8080
    google_token_path: str = DEFAULT_TOKEN_PATH
    google_form_id: str = DEFAULT_FORM_ID
    max_upload_mb: int = 25
    root: Path = field(default=PROJECT_ROOT)

    @property
    def token_path(self) -> Path:
        """Gibt den aufgelösten Pfad zur Google-OAuth-Token-Datei zurück."""
        return Path(os.path.expandvars(os.path.expanduser(self.google_token_path)))

    @property
    def max_upload_bytes(self) -> int:
        """Gibt die maximale Upload-Größe in Bytes zurück."""
        return self.max_upload_mb * 1024 * 1024


_ENV_MAP = {
    "OBS_TEXTBOX_HOST": ("host", str),
    "OBS_TEXTBOX_PORT": ("port", int),
    "OBS_TEXTBOX_GOOGLE_TOKEN": ("google_token_path", str),
    "OBS_TEXTBOX_FORM_ID": ("google_form_id", str),
    "OBS_TEXTBOX_MAX_UPLOAD_MB": ("max_upload_mb", int),
}


def load_config(config_file: Path | None = None) -> Config:
    """
    Lädt die Server-Konfiguration aus config.json (falls vorhanden) und
    überschreibt sie mit gesetzten Umgebungsvariablen.
    """
    cfg = Config()
    path = config_file or (PROJECT_ROOT / "config.json")
    if path.is_file():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            raise SystemExit(f"config.json ist ungültig: {exc}") from exc
        for key, value in data.items():
            if key.startswith("_"):
                continue  # Kommentarfelder
            if hasattr(cfg, key) and key != "root":
                try:
                    setattr(cfg, key, type(getattr(cfg, key))(value))
                except (ValueError, TypeError) as exc:
                    raise SystemExit(
                        f"config.json: ungültiger Wert für '{key}': {value!r} "
                        f"(erwartet {type(getattr(cfg, key)).__name__})"
                    ) from exc

    for env_name, (attr, cast) in _ENV_MAP.items():
        raw = os.environ.get(env_name)
        if raw:
            try:
                setattr(cfg, attr, cast(raw))
            except ValueError:
                pass
    return cfg


def read_openrouter_key(root: Path = PROJECT_ROOT) -> str | None:
    """Liest den OpenRouter-Key aus api.env (Format: OPENROUTER_API_KEY=sk-or-...)."""
    env_file = root / "api.env"
    if not env_file.is_file():
        return None
    for line in env_file.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        value = line.split("=", 1)[1].strip() if "=" in line else line
        value = value.strip("'\"")
        if value.startswith("sk-or-"):
            return value
    return None
