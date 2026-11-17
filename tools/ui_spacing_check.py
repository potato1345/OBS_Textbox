#!/usr/bin/env python3
"""
ui_spacing_check.py — Pixel-basierter UI-Spacing-Audit für Textbox Studio.

Liest ein PNG-Screenshot, segmentiert Content-Zeilen per Luminanz-Schwellwert
und prüft Gaps, unteren Rand sowie Border-Runs gegen die Spezifikation in
docs/DESIGN.md (JSON-Block).

Usage:
  python tools/ui_spacing_check.py <image.png> [--max-gap N] [--max-bottom N]

Exit-Code: 0 = PASS, 1 = FAIL
"""

import argparse
import json
import re
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Defaults aus docs/DESIGN.md (JSON-Block am Dateiende)
# ---------------------------------------------------------------------------
_DESIGN_SPEC = {
    "spacing_scale": [4, 6, 8, 12],
    "max_content_gap": 60,
    "max_bottom_residual": 60,
    "border_color": [60, 64, 77],
    "bg_color": [29, 31, 38],
    "header_accent": [0, 183, 195],
    "content_luminance_threshold": 230,
    "dialog_width": 560,
    "dock_width": 358,
}


def _load_design_spec(design_md: str | None = None) -> dict:
    """Parse den letzten ```json-Block in docs/DESIGN.md."""
    if design_md is None:
        md_path = Path(__file__).resolve().parents[1] / "docs" / "DESIGN.md"
        if not md_path.is_file():
            return dict(_DESIGN_SPEC)
        design_md = md_path.read_text(encoding="utf-8")
    m = re.search(r"```json\n(.*?)\n```", design_md, re.DOTALL)
    if not m:
        return dict(_DESIGN_SPEC)
    try:
        return json.loads(m.group(1))
    except json.JSONDecodeError:
        return dict(_DESIGN_SPEC)


def _luminance(r: int, g: int, b: int) -> float:
    """Relative luminance per ITU-R BT.601 (schnell, ausreichend)."""
    return 0.299 * r + 0.587 * g + 0.114 * b


def _within_tolerance(pixel, target, tol: int) -> bool:
    """Prüfe, ob alle RGB-Komponenten innerhalb tol des target liegen."""
    return all(abs(a - b) <= tol for a, b in zip(pixel, target))


# ---------------------------------------------------------------------------
# Core checks
# ---------------------------------------------------------------------------


def analyze_spacing(
    img_path: str,
    max_gap: int | None = None,
    max_bottom: int | None = None,
) -> int:
    """
    Führe alle Checks aus. Return 0 (PASS) oder 1 (FAIL).
    """
    # PIL ist optional, da nur für diesen Befehl gebraucht.
    try:
        from PIL import Image
    except ImportError:
        print("FEHLER: Pillow (PIL) ist nicht installiert.")
        print("  pip install Pillow")
        return 1

    spec = _load_design_spec()
    if max_gap is None:
        max_gap = spec.get("max_content_gap", 60)
    if max_bottom is None:
        max_bottom = spec.get("max_bottom_residual", 60)

    lum_thresh = spec.get("content_luminance_threshold", 230)
    border_color = tuple(spec.get("border_color", [60, 64, 77]))
    border_tol = 30

    im = Image.open(img_path).convert("RGB")
    w, h = im.size
    pixels = im.load()

    # ------------------------------------------------------------------
    # 1. Content-segment rows
    # ------------------------------------------------------------------
    # Interior columns: [24, w-24)
    interior_start = min(24, w // 2)
    interior_end = max(w - 24, w // 2)

    is_content = []
    for y in range(h):
        max_lum = 0.0
        for x in range(interior_start, interior_end):
            r, g, b = pixels[x, y]
            lum = _luminance(r, g, b)
            if lum > max_lum:
                max_lum = lum
        is_content.append(max_lum > lum_thresh)

    # Segment consecutive content rows
    segments = []  # list of (start_y, end_y_exclusive)
    in_seg = False
    seg_start = 0
    for y, c in enumerate(is_content):
        if c and not in_seg:
            seg_start = y
            in_seg = True
        elif not c and in_seg:
            segments.append((seg_start, y))
            in_seg = False
    if in_seg:
        segments.append((seg_start, h))

    if not segments:
        print("Keine Content-Segmente gefunden (Bild möglicherweise leer?).")
        return 1

    # ------------------------------------------------------------------
    # 2. Gaps between segments
    # ------------------------------------------------------------------
    gaps = []
    for i in range(1, len(segments)):
        gap = segments[i][0] - segments[i - 1][1]
        gaps.append(gap)

    # ------------------------------------------------------------------
    # 3. Bottom residual
    # ------------------------------------------------------------------
    bottom_residual = h - segments[-1][1]

    # ------------------------------------------------------------------
    # 4. Border runs (left + right columns)
    # ------------------------------------------------------------------
    left_border_count = 0
    right_border_count = 0
    left_any = False
    right_any = False
    for y in range(h):
        left_px = pixels[0, y] if w > 0 else (0, 0, 0)
        right_px = pixels[w - 1, y] if w > 0 else (0, 0, 0)
        # Check left side (cols 0..7)
        left_ok = any(
            _within_tolerance(pixels[x, y], border_color, border_tol)
            for x in range(min(8, w))
        )
        # Check right side (cols w-8..w-1)
        right_ok = any(
            _within_tolerance(pixels[x, y], border_color, border_tol)
            for x in range(max(0, w - 8), w)
        )
        if left_ok:
            left_border_count += 1
            left_any = True
        if right_ok:
            right_border_count += 1
            right_any = True

    border_ratio_left = left_border_count / h if h > 0 else 0
    border_ratio_right = right_border_count / h if h > 0 else 0
    border_threshold = 0.7
    border_left_ok = border_ratio_left >= border_threshold
    border_right_ok = border_ratio_right >= border_threshold

    # ------------------------------------------------------------------
    # Compact table output
    # ------------------------------------------------------------------
    print(f"{'Segment':<10} {'Start':>6} {'Ende':>6} {'Höhe':>6}")
    print("-" * 32)
    for idx, (s, e) in enumerate(segments):
        print(f"{idx:<10} {s:>6} {e:>6} {e - s:>6}")

    print()
    if gaps:
        print(f"{'Gap':<10} {'von':>6} {'bis':>6} {'px':>6}")
        print("-" * 32)
        for idx, g in enumerate(gaps):
            print(f"{'gap' + str(idx + 1):<10} {segments[idx][1]:>6} {segments[idx + 1][0]:>6} {g:>6}")
    else:
        print("Keine Gaps (nur ein Segment).")

    print()
    print(f"Unterer Rand (bottom residual): {bottom_residual} px (Limit: {max_bottom} px)")
    print(f"Border-Run links:  {left_border_count:>4}/{h} px ({border_ratio_left:.1%}, benötigt >= {border_threshold:.0%})")
    print(f"Border-Run rechts: {right_border_count:>4}/{h} px ({border_ratio_right:.1%}, benötigt >= {border_threshold:.0%})")

    # ------------------------------------------------------------------
    # FAIL checks
    # ------------------------------------------------------------------
    failed = False

    for idx, g in enumerate(gaps):
        if g > max_gap:
            print(f"\nFAIL: gap{idx + 1} = {g} px > max_content_gap ({max_gap} px)")
            failed = True

    if bottom_residual > max_bottom:
        print(f"\nFAIL: bottom_residual = {bottom_residual} px > max_bottom_residual ({max_bottom} px)")
        failed = True

    if not border_left_ok:
        if not left_any:
            print("\nWARN: border nicht im Crop enthalten (Fensterframe außerhalb des Crops) — linke Seite übersprungen")
        else:
            print(f"\nFAIL: border run left ({border_ratio_left:.1%}) < {border_threshold:.0%} der Höhe")
            failed = True

    if not border_right_ok:
        if not right_any:
            print("\nWARN: border nicht im Crop enthalten (Fensterframe außerhalb des Crops) — rechte Seite übersprungen")
        else:
            print(f"\nFAIL: border run right ({border_ratio_right:.1%}) < {border_threshold:.0%} der Höhe")
            failed = True

    if failed:
        print("\n❌ FAIL")
        return 1
    else:
        print("\n✅ PASS")
        return 0


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Textbox Studio UI Spacing Audit Tool",
    )
    parser.add_argument("image", help="PNG-Screenshot des zu prüfenden Fensters")
    parser.add_argument("--max-gap", type=int, default=None,
                        help="Maximal erlaubter Gap zwischen Content-Segmenten (Default: aus DESIGN.md)")
    parser.add_argument("--max-bottom", type=int, default=None,
                        help="Maximal erlaubter unterer Rand (Default: aus DESIGN.md)")
    args = parser.parse_args()

    return analyze_spacing(args.image, args.max_gap, args.max_bottom)


if __name__ == "__main__":
    sys.exit(main())