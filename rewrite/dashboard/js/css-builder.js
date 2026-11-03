/**
 * @file css-builder.js
 * Generiert den CSS-Injektionsblock, der über OBS WebSocket in die Browser-Quellen
 * geschrieben wird. Unterstützt sichere String-Maskierung und Fallback für Regenbogeneffekte.
 */

import { hexToRgba } from './color-profiles.js';

export const INJECT_START_TAG = '/* OBS_TEXTBOX_INJECT_START */';
export const INJECT_END_TAG = '/* OBS_TEXTBOX_INJECT_END */';

/**
 * Maskiert Zeichen für die sichere Verwendung innerhalb von CSS-String-Literalen (z.B. content: "...").
 */
export function escapeCssString(str) {
    if (str === null || str === undefined) return '';
    return String(str)
        .replace(/[\u0000-\u001F\u007F-\u009F]/g, '') // Steuerzeichen entfernen
        .replace(/\\/g, '\\\\')                         // Backslash maskieren
        .replace(/"/g, '\\"');                          // Anführungszeichen maskieren
}

/**
 * Erzeugt den vollständigen CSS-Block für eine Box.
 * Kompatibel sowohl mit dem neuen rewrite/textbox.html als auch mit dem originalen textbox.html.
 */
export function buildInjectedCss(box) {
    const hasText = !!(
        (box.zeile1 && box.zeile1.trim()) ||
        (box.zeile2 && box.zeile2.trim()) ||
        (box.zeile3 && box.zeile3.trim())
    );

    const boxBg = hexToRgba(box.boxColor || '#ffffff', box.boxColorOpacity ?? 255);
    const cornerA = hexToRgba(box.cornerColorA || '#fce647', box.cornerColorAOpacity ?? 255);
    const cornerB = hexToRgba(box.cornerColorB || '#fce647', box.cornerColorBOpacity ?? 255);
    const textCol = hexToRgba(box.textColor || '#000000', box.textColorOpacity ?? 255);

    const isRainbow = !!box.rainbowCorners;
    const showAnim = box.showAnim ? 1 : 0;
    const animFile = escapeCssString(box.animFile || '');
    const animScale = Number(box.animScale) || 3;
    const animX = Number(box.animX) || 0;
    const animY = Number(box.animY) || 0;

    let css = `\n${INJECT_START_TAG}\n:root {\n`;
    css += `  --zeile-1: "${escapeCssString(box.zeile1 || '')}";\n`;
    css += `  --zeile-2: "${escapeCssString(box.zeile2 || '')}";\n`;
    css += `  --zeile-3: "${escapeCssString(box.zeile3 || '')}";\n`;
    css += `  --anim-file: "${animFile}";\n`;
    css += `  --show-anim: ${showAnim};\n`;
    css += `  --anim-scale: ${animScale};\n`;
    css += `  --anim-x: ${animX}px;\n`;
    css += `  --anim-y: ${animY}px;\n`;
    css += `  --box-bg: ${boxBg};\n`;
    css += `  --triangle-color-a: ${cornerA};\n`;
    css += `  --triangle-color-b: ${cornerB};\n`;
    css += `  --text-color: ${textCol};\n`;
    css += `  --box-opacity: ${hasText ? 1 : 0};\n`;
    css += `  --rainbow-corners: ${isRainbow ? 1 : 0};\n`;
    css += `}\n`;
    css += `html, body { background-color: rgba(0, 0, 0, 0) !important; }\n`;

    // Abwärtskompatibilität für das originale v1 textbox.html (nutzte ::before/::after für Regenbogen)
    if (isRainbow) {
        css += `.textbox-container { background-image: none !important; }\n`;
        css += `.textbox-container::before {\n`;
        css += `  content: "" !important; display: block !important; position: absolute; top: 0; left: 0;\n`;
        css += `  width: 50px; height: 50px; border-top-left-radius: 4px;\n`;
        css += `  background: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%);\n`;
        css += `  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n`;
        css += `}\n`;
        css += `.textbox-container::after {\n`;
        css += `  content: "" !important; display: block !important; position: absolute; bottom: 0; right: 0;\n`;
        css += `  width: 50px; height: 50px; border-bottom-right-radius: 4px;\n`;
        css += `  background: linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);\n`;
        css += `  pointer-events: none; z-index: 0; animation: rainbow-rotate 12s linear infinite;\n`;
        css += `}\n`;
        css += `.text-line { position: relative; z-index: 1; }\n`;
        css += `@keyframes rainbow-rotate {\n`;
        css += `  from { filter: hue-rotate(0deg); }\n`;
        css += `  to   { filter: hue-rotate(360deg); }\n`;
        css += `}\n`;
    }

    css += `${INJECT_END_TAG}`;
    return css;
}

/**
 * Ersetzt den alten Injektionsblock in einem bestehenden CSS-String durch den neuen.
 */
export function applyInjectedCss(existingCss, newBlock) {
    const startPattern = INJECT_START_TAG.replace(/[*\\/]/g, '\\$&');
    const endPattern = INJECT_END_TAG.replace(/[*\\/]/g, '\\$&');
    const pattern = new RegExp(`${startPattern}[\\s\\S]*?${endPattern}`, 'g');
    const cleaned = (existingCss || '').replace(pattern, '').trim();
    return cleaned + '\n' + newBlock;
}
