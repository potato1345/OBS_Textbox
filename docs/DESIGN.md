# Textbox Studio — UI-Design-Spec (v1, 17.11.2026)

Verbindliche Referenz für UI-Audits dieses Plugins. Abweichungen werden pixelgemessen
(`tools/ui_spacing_check.py`) und im Layout-Code gefixt. Ursprung: Spacing-Fix 17.11
(Commit 73e8cc4) — Surplus-Space verteilte sich zwischen Widgets, weil Dialog auf
Screen-Höhe resized wurde ohne trailing Stretch.

## Spacing-Raster (verbindlich)
| Token | Wert | Verwendung |
|---|---|---|
| `s1` | 4px | Label → Eingabefeld, row-interne Grups |
| `s2` | 6px | horizontaler Gap in Reihen (Input+Button, Icon+Input) |
| `s3` | 8px | vertikaler Rhythmus innerhalb Sektionen (Feld→Feld, Gruppe→Button) |
| `s4` | 12px | zwischen Sektionen/QGroupBoxen, root-Layout spacing |
| `m-root` | 16px | Dialog root ContentsMargins |
| `m-group` | 12px oben/unten, 8px seitlich | QGroupBox ContentsMargins |

## Dialog-Regeln
- Dialoghöhe = `min(screen_avail, content sizeHint + 8)` — **niemals** Screen-Höhe.
- Root-Layout endet mit `addStretch(1)`: Überschuss darf nur unten landen.
- Root-level QGroupBoxen: vertical size policy `Preferred`.
- Abstand benachbarter Content-Segmente (pixelgemessen): ≤ 60px; ein Gap > spacing+40
  ist ein FAIL (Surplus-Verteilung).
- Statuszeile = letztes Content-Element; Restfläche zum Fensterboden ≤ 60px.
- Passwörter/Keys immer als Punkte maskiert (QLineEdit PasswordEchoOnEdit), nie Klartext.

## Fenster-Metriken
- Dock: 358px breit (min). Docked in OBS oder floating als **natives** Fenster
  (Titelbar „Textbox Studio“, verschiebbar/resizable über OS; Ecken via DWM 8px).
  Kein `FramelessWindowHint`, kein `WA_TranslucentBackground`.
- Border: 1px durchgehend `#3C404D` links+rechts (Pixel-Run-Scan, Höhe = Fensterhöhe).
- Import-Dialog „Datenarchiv“: Breite 560 (min 540), Content-fit-Höhe.

## Theme-Tokens (OBS Yami Dark)
| Rolle | Wert |
|---|---|
| Background | `#1D1F26` |
| Menu/Tooltip | `#272A33` |
| Border | `#3C404D` |
| Hover | `#464B59` |
| Accent selected | `#284CB8`, selected-border `#476BD7` |
| Text | `#FFFFFF`, disabled `#969696` |
| Header-Akzent (Dialog-Titel) | türkis ≈ `#00B7C3` (gemessen RGB 0,183,195) |
| Font | Open Sans / Segoe UI, 11pt; Feld-Labels UPPERCASE |

## Audit-Procedure (zukünftig automatisch)
1. Ziel-Fenster (Dock/Dialog) per `computer_use` öffnen, whole-screen-cappen, per
   `GetWindowRect` cropen **inklusive OS-Frame** (Qt-modal dialogs blocken
   window-scoped captures; ohne Frame im Crop meldet der Border-Check nur WARN).
2. `python tools/ui_spacing_check.py <crop.png>` — liefert Segmenthöhen + Gaps,
   Border-Run-Check (`#3C404D`), untere Restfläche, PASS/FAIL gegen dieses Raster.
3. Bei FAIL: Layout-Fix in `plugin/src/textbox-dock.cpp` (Raster einhalten),
   rebuild, ctest, deploy, re-audit bis PASS.

```json
{"spacing_scale":[4,6,8,12],"max_content_gap":60,"max_bottom_residual":60,
 "border_color":[60,64,77],"bg_color":[29,31,38],"header_accent":[0,183,195],
 "content_luminance_threshold":230,"dialog_width":560,"dock_width":358}
```

## Browser-Reload-Regel (obs-browser)
- `obs-browser` registriert als Source-Procs nur `javascript_event`; es gibt KEINen „reload"-Proc.
  Ein Template-Wechsel muss das Browser-Dokument deshalb über `obs_source_update` mit einer
  geänderten Setting neu laden (CSS-Nonce-Kommentar im `css`-Setting erzwingt destroy+recreate).
- Live-Pushes (Text/Farbe) laufen über `javascript_event` ohne Reload.
- Audit: Nach jedem Template-Wechsel muss die Browser-Quelle das neue Dokument zeigen
  (Preview-Screenshot: Layout unterscheidet sich sichtbar vom alten Template).

## Template-Token-Vertrag (HTML-Vorlagen)
`write_card` substituiert exakt diesen Token-Set (Quelle: `substitute_tokens` in
`plugin/src/textbox-html.cpp`): `{{zeile1}} {{zeile2}} {{zeile3}} {{zeile-N}}
{{zeile-count}} {{text_color}} {{box_color}} {{corner_a}} {{corner_b}}
{{corner_op_a}} {{corner_op_b}} {{text_opacity}} {{box_opacity}} {{rainbow}}
{{anim_src}} {{anim_scale}} {{anim_x}} {{anim_y}} {{show_anim}} {{card_id}}
{{clear}}`.

Regeln:
- Templates dürfen nur diese Tokens referenzieren. Positionen/Schriftgrößen sind
  fest pro Template (`--l1-x: 50px` usw.), keine Tokens — ein unsubstituiertes
  `{{token}}` in einer CSS-Length bricht die Eigenschaft (der var()-Fallback
  greift bei ungültigem Wert NICHT).
- Audit: `grep -o '{{[a-z0-9_]*}}' plugin/templates/*.html` — jeder Fund außerhalb
  des Token-Sets oder nach Substitution = FAIL. Jeder neue Token braucht einen
  Eintrag in `substitute_tokens` + Test 9b.
