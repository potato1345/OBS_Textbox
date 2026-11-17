# Native OBS UI Styling Reference

## Architecture Overview

OBS Studio's UI styling has three layers:

1. **OBSProxyStyle** (C++ `QProxyStyle`)
   - Base style: `"windowsvista"` on Windows, `"Fusion"` on Linux
   - Only overrides: disable ComboBox wheel scrolling, force native popup on macOS
   - Result: lets Qt's native style handle basic metrics, OBS theme handles appearance via QSS
   - Invisible-cursor variant: sets `PM_TextCursorWidth = 0`

2. **Theme File Format** (`.obt` base + `.ovt` variant)
   - Files live in `<data>/themes/` or `<config>/obs-studio/themes/`
   - Sections inside:
     - `@OBSThemeMeta` — id, name, author, dark flag, extends
     - `@OBSThemeVars` — variables: colors, sizes, math expressions
     - QSS (standard Qt stylesheet) referencing variables via `var(--name)`
   - OBS parser resolves variables, evaluates calc/max/min, rounds px values, then calls `setStyleSheet()` + `setPalette()` (methods on `OBSApp` inheriting from a `QApplication` subclass)

3. **Run-time styling** (manual `setStyleSheet()` calls)
   - Docks, dialogs, and custom widgets use `setProperty()`, `unpolish()`/`polish()`, and sometimes `setStyleSheet()` calls in C++
   - OBS's own dialogs often style via QSS rules defined in the Yami theme (selector-per-widget)

## Yami Base Theme (com.obsproject.Yami)

### Color Tokens (from `@OBSThemeVars`)

```
--blue1: #718CDC   lighter
--blue2: #476BD7   light
--blue3: #284CB8   primary
--blue4: #213E97   dark
--blue5: #1A3278   darker
--blue6: #162A64   darkest

--red1: #E85E75    light error
--red3: #C01C37    danger

--grey1: #5B6273    borders/hover
--grey2: #4E5566
--grey3: #464B59    button hover
--grey4: #3C404D    button bg, borders, inputs
--grey5: #323540
--grey6: #272A33    panel bg, base
--grey7: #1D1F26    window bg
--grey8: #13141A    preview bg

--white1: #FFFFFF   text
--white4: #B4B4B4  slider handle hover
--white5: #969696   muted text, disabled

--green1–6, --teal1–6, --yellow1–6, --purple1–6, --pink1–6
```

### Size & Layout Tokens

```
--font_base:     calc(1pt * var(--font_base_value))        // default: 14pt
--font_small:    max(7pt, calc(0.8pt * var(--font_base_value)))
--padding_base:   calc(0.5px * var(--padding_base_value))  // default: 2px
--spacing_base:  min(max(1px, calc(0.4 * ...)), 2px)
--scrollbar_size: 12px
--border_radius:  4px
--input_height:   calc(...)                                // dynamic, ~24px base
```

### Key QSS Patterns in Yami

**Widget BGs:** QPushButton uses `--grey4` bg by default. Hover → `--grey3`. Disabled → `--grey6`.

**Inputs (QLineEdit, QComboBox, QSpinBox):** `--grey4` bg, 1px border same color. Hover → `--grey7` bg + `--grey1` border. Focus → `--primary` border.

**Primary action buttons:** Used when `QPushButton:checked` → `--primary` bg, `--primary_light` border.

**Scrollbars:** 12px wide, `--grey6` track, `--grey4` handle. Hover → `--grey3`. 2px margin, 2px radius.

**Slider:** 4px groove `--grey4`, sub-page fill `--blue2`, white 20×10 handle. No art-deco styling.

**Dock widgets:** Title bg `--grey5`, border `--grey4`, radius 4px top-only. Content area `--bg_base` + border.

**TabBar:** Tabs bg `--grey6`, selected `--primary` bg, `--primary_light` border.

**ComboBox dropdown:** `--grey6` bg, items pad 4px 16px, selected/hover → `--primary` bg.

## How Native-Looking Plugins Style Themselves

### obs-media-controls (best-practice example)

- Uses **zero custom QSS** — inherits everything from OBS theme
- For tool buttons, uses `setProperty("themeID", "...")` + `setProperty("class", "...")` to pick up OBS-defined icon selectors (e.g., `class: "icon-media-play"` maps to Yami's `.icon-media-play { qproperty-icon: url(...); }`)
- After changing property, calls:
  ```cpp
  widget->style()->unpolish(widget);
  widget->style()->polish(widget);
  ```
- Custom slider style (`SliderAbsoluteSetStyle`): merely overrides `SH_Slider_AbsoluteSetButtons` (click-anywhere-to-seek) — no visual changes

### Our Current textbox-theme.hpp (assessment)

- **Good:** Uses correct Yami color tokens (#1D1F26, #3C404D, #284CB8, #272A33, #969696)
- **Good:** Uses objectName selectors (QWidget#textboxDock) — matches OBS pattern
- **Good:** Scrollbar and slider styles closely match Yami QSS
- **Problem:** Hardcodes all values instead of using variables. When OBS updates Yami colors, our plugin doesn't adapt
- **Problem:** Uses `setStyleSheet()` for the entire dock — overrides OBS theme inheritance for sub-widgets
- **Problem:** Font family hardcoded to 'Open Sans', 'Segoe UI' — no `obsFontScale` awareness
- **Problem:** Dialog backgrounds hardcoded — breaks with user-installed alternative themes (Light, Acri, Rachni)

## Recommendations for Textbox

1. **Minimize overrides** — only style what differs from OBS defaults (cards, ID bars, specific layout frames)
2. **Let OBS theme style basics** — remove QSS for QLineEdit, QComboBox, QPushButton, QScrollBar, QSlider — OBS's Yami already covers these identically
3. **Use property-based theming** — follow obs-media-controls pattern: `.icon-*` classes, `setProperty("class")`, polish/unpolish
4. **Dynamic font size** — don't hardcode `font-size: 11pt`; use relative sizes or check `OBSBasic::GetBaseFont()`
5. **If hardcoding remains** — keep a reference mapping comment linking each color to its Yami token name (already done partially)
6. **Dock-specific overrides only** — cards, ID bar, flash border, style-section collapse — keep the rest transparent