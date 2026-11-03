/**
 * @file color-profiles.js
 * Verwaltung der Dashboard-Farbprofile, Konvertierung (Hex/RGBA)
 * und YAML-Import/Export für Farbpaletten.
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Ermöglicht dynamische Themes für die Bedienoberfläche und Serialisierung
 * als YAML für einfachen Austausch und Sicherung.
 */

import { LP_DEBUG } from './box-utils.js';

/**
 * Standard-Themes des Dashboards (OBS Dark, OBS Light, Midnight Blue, etc.).
 * @type {Array<{name: string, isDefault: boolean, colors: Object}>}
 */
export const DEFAULT_COLOR_PROFILES = [
    {
        name: 'OBS Dark',
        isDefault: true,
        colors: {
            bgPrimary: '#1C1F26', bgPrimaryOpacity: 255,
            bgSecondary: '#323a44', bgSecondaryOpacity: 255,
            panelBg: '#272A33', panelBgOpacity: 255,
            border: '#3C404D', borderOpacity: 255,
            cardOuterBg: '#3C404D', cardOuterBgOpacity: 255,
            panelBgAlt: '#414852', panelBgAltOpacity: 255,
            btnHoverBg: '#414852', btnHoverBgOpacity: 255,
            textColor: '#eff0f1', textColorOpacity: 255,
            accentBlue: '#3daee9', accentBlueOpacity: 255
        }
    },
    {
        name: 'OBS Light',
        isDefault: true,
        colors: {
            bgPrimary: '#f0f0f0', bgPrimaryOpacity: 255,
            bgSecondary: '#ffffff', bgSecondaryOpacity: 255,
            panelBg: '#e8e8e8', panelBgOpacity: 255,
            border: '#c0c0c0', borderOpacity: 255,
            cardOuterBg: '#d5d5d5', cardOuterBgOpacity: 255,
            panelBgAlt: '#dcdcdc', panelBgAltOpacity: 255,
            btnHoverBg: '#d0d0d0', btnHoverBgOpacity: 255,
            textColor: '#1a1a1a', textColorOpacity: 255,
            accentBlue: '#2980b9', accentBlueOpacity: 255
        }
    },
    {
        name: 'Midnight Blue',
        isDefault: true,
        colors: {
            bgPrimary: '#0d1117', bgPrimaryOpacity: 255,
            bgSecondary: '#161b22', bgSecondaryOpacity: 255,
            panelBg: '#1c2333', panelBgOpacity: 255,
            border: '#30363d', borderOpacity: 255,
            cardOuterBg: '#252d3a', cardOuterBgOpacity: 255,
            panelBgAlt: '#2a3342', panelBgAltOpacity: 255,
            btnHoverBg: '#2d3748', btnHoverBgOpacity: 255,
            textColor: '#c9d1d9', textColorOpacity: 255,
            accentBlue: '#58a6ff', accentBlueOpacity: 255
        }
    },
    {
        name: 'Forest Green',
        isDefault: true,
        colors: {
            bgPrimary: '#1a2318', bgPrimaryOpacity: 255,
            bgSecondary: '#2a3528', bgSecondaryOpacity: 255,
            panelBg: '#222d20', panelBgOpacity: 255,
            border: '#3a4a36', borderOpacity: 255,
            cardOuterBg: '#344530', cardOuterBgOpacity: 255,
            panelBgAlt: '#3d4f38', panelBgAltOpacity: 255,
            btnHoverBg: '#455a40', btnHoverBgOpacity: 255,
            textColor: '#d4e0d0', textColorOpacity: 255,
            accentBlue: '#6bcf7f', accentBlueOpacity: 255
        }
    },
    {
        name: 'Sunset Warm',
        isDefault: true,
        colors: {
            bgPrimary: '#1f1510', bgPrimaryOpacity: 255,
            bgSecondary: '#2d1f18', bgSecondaryOpacity: 255,
            panelBg: '#261a14', panelBgOpacity: 255,
            border: '#4a3228', borderOpacity: 255,
            cardOuterBg: '#3d2a20', cardOuterBgOpacity: 255,
            panelBgAlt: '#4a3428', panelBgAltOpacity: 255,
            btnHoverBg: '#553d30', btnHoverBgOpacity: 255,
            textColor: '#f0ddd0', textColorOpacity: 255,
            accentBlue: '#f59e42', accentBlueOpacity: 255
        }
    },
    {
        name: 'High Contrast',
        isDefault: true,
        colors: {
            bgPrimary: '#000000', bgPrimaryOpacity: 255,
            bgSecondary: '#1a1a1a', bgSecondaryOpacity: 255,
            panelBg: '#0d0d0d', panelBgOpacity: 255,
            border: '#ffffff', borderOpacity: 255,
            cardOuterBg: '#222222', cardOuterBgOpacity: 255,
            panelBgAlt: '#2a2a2a', panelBgAltOpacity: 255,
            btnHoverBg: '#333333', btnHoverBgOpacity: 255,
            textColor: '#ffffff', textColorOpacity: 255,
            accentBlue: '#00e5ff', accentBlueOpacity: 255
        }
    }
];

/**
 * Liste der konfigurierbaren Farb- und Deckkraft-Schlüssel eines Farbprofils.
 * @type {string[]}
 */
export const COLOR_KEYS = [
    'bgPrimary', 'bgPrimaryOpacity',
    'bgSecondary', 'bgSecondaryOpacity',
    'panelBg', 'panelBgOpacity',
    'border', 'borderOpacity',
    'cardOuterBg', 'cardOuterBgOpacity',
    'panelBgAlt', 'panelBgAltOpacity',
    'btnHoverBg', 'btnHoverBgOpacity',
    'textColor', 'textColorOpacity',
    'accentBlue', 'accentBlueOpacity'
];

/**
 * Konvertiert Hex-Farbcode (#RGB oder #RRGGBB) und Opacity (0-255) in valides CSS rgb/rgba.
 *
 * @param {string} hex - Farbcode in Hexadezimaldarstellung (#FFF oder #FFFFFF).
 * @param {number|string} [opacity255=255] - Deckkraft im Bereich 0 (transparent) bis 255 (deckend).
 * @returns {string} CSS-Farbdefinition als rgb(...) oder rgba(...).
 */
export function hexToRgba(hex, opacity255 = 255) {
    const raw = String(hex || '#000000').trim();
    const match = raw.match(/^#?([a-f\d]{3}|[a-f\d]{6})$/i);
    if (!match) return raw;

    let hexVal = match[1];
    if (hexVal.length === 3) {
        hexVal = hexVal.split('').map(c => c + c).join('');
    }

    const intVal = parseInt(hexVal, 16);
    const r = (intVal >> 16) & 255;
    const g = (intVal >> 8) & 255;
    const b = intVal & 255;

    const opNum = Number(opacity255);
    const validOp = (!isNaN(opNum) && opNum >= 0 && opNum <= 255) ? opNum : 255;
    const alpha = parseFloat((validOp / 255).toFixed(3));

    return alpha >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${alpha})`;
}

/**
 * Wandelt ein Profil-Objekt in einen sauberen YAML-String um.
 *
 * @param {object} profile - Profil-Objekt mit name und colors-Dictionary.
 * @returns {string} Formatierter YAML-String.
 */
export function colorProfileToYAML(profile) {
    let yaml = `# OBS Textbox Farbprofil\n`;
    const escapedName = String(profile.name ?? '').replace(/\\/g, '\\\\').replace(/"/g, '\\"');
    yaml += `name: "${escapedName}"\n`;
    yaml += `colors:\n`;
    const c = profile.colors || {};

    for (const key of COLOR_KEYS) {
        const val = c[key] !== undefined ? c[key] : (key.endsWith('Opacity') ? 255 : '#000000');
        if (typeof val === 'number') {
            yaml += `  ${key}: ${val}\n`;
        } else {
            yaml += `  ${key}: "${val}"\n`;
        }
    }
    return yaml;
}

/**
 * Parst einen YAML-String in ein Farbprofil-Objekt.
 *
 * @param {string} yamlString - YAML-Text mit Profildefinition.
 * @returns {{name: string, colors: Object}} Geparstes Profilobjekt.
 */
export function yamlToColorProfile(yamlString) {
    const lines = yamlString.split('\n');
    const profile = { name: '', colors: {} };
    let inColors = false;

    for (const rawLine of lines) {
        const line = rawLine.trim();
        if (!line || line.startsWith('#')) continue;

        if (line.startsWith('name:')) {
            const val = line.substring(5).trim();
            profile.name = val.replace(/^["']|["']$/g, '').replace(/\\(["\\])/g, '$1');
            continue;
        }

        if (line === 'colors:') {
            inColors = true;
            continue;
        }

        if (inColors && line.includes(':')) {
            const colonIdx = line.indexOf(':');
            const key = line.substring(0, colonIdx).trim();
            let val = line.substring(colonIdx + 1).trim();
            val = val.replace(/^["']|["']$/g, '');

            if (/^\d+$/.test(val)) {
                profile.colors[key] = parseInt(val, 10);
            } else {
                profile.colors[key] = val;
            }
        }
    }

    return profile;
}

/**
 * Prüft ob ein Profilname zu den unveränderbaren Standard-Profilen gehört.
 *
 * @param {string} name - Name des zu prüfenden Profils.
 * @returns {boolean} True, wenn es sich um ein Standardprofil handelt.
 */
export function isDefaultColorProfile(name) {
    return DEFAULT_COLOR_PROFILES.some(p => p.name.toLowerCase() === (name || '').toLowerCase());
}

/**
 * Wendet ein Set von UI-Farben auf das :root-Element des Dashboards an.
 *
 * @param {object} [colors={}] - Dictionary mit Farbwerten für CSS-Variablen.
 */
export function applyThemeVariables(colors = {}) {
    if (LP_DEBUG) console.debug('[liedplan] Wende Theme-Variablen an');
    const root = document.documentElement;
    root.style.setProperty('--bg-primary', hexToRgba(colors.bgPrimary || '#1C1F26', colors.bgPrimaryOpacity ?? 255));
    root.style.setProperty('--bg-secondary', hexToRgba(colors.bgSecondary || '#323a44', colors.bgSecondaryOpacity ?? 255));
    root.style.setProperty('--panel-bg', hexToRgba(colors.panelBg || '#272A33', colors.panelBgOpacity ?? 255));
    root.style.setProperty('--border', hexToRgba(colors.border || '#3C404D', colors.borderOpacity ?? 255));
    root.style.setProperty('--card-outer-bg', hexToRgba(colors.cardOuterBg || '#3C404D', colors.cardOuterBgOpacity ?? 255));
    root.style.setProperty('--panel-bg-alt', hexToRgba(colors.panelBgAlt || '#414852', colors.panelBgAltOpacity ?? 255));
    root.style.setProperty('--btn-hover-bg', hexToRgba(colors.btnHoverBg || '#414852', colors.btnHoverBgOpacity ?? 255));
    root.style.setProperty('--text-primary', hexToRgba(colors.textColor || '#eff0f1', colors.textColorOpacity ?? 255));
    root.style.setProperty('--accent-blue', hexToRgba(colors.accentBlue || '#3daee9', colors.accentBlueOpacity ?? 255));
}
