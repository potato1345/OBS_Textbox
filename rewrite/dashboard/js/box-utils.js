/**
 * @file box-utils.js
 * Reine (DOM-/State-freie) Hilfsfunktionen für Textbox-Objekte:
 * Box-Factory, ID-Sanitizer mit Kollisionsbehandlung und HTML-Escaping.
 * Hier zentral gehalten, damit AI-Import, Forms-Import und Dashboard-Karten
 * dieselbe Logik verwenden (F4/F3, Review 2026-10-22).
 */

/**
 * Erzeugt eine neue Box mit den Standardwerten.
 * @param {string} id Eindeutige Box-ID (muss in state.boxes eindeutig sein)
 * @param {string} zeile1
 * @param {string} zeile2
 * @param {string} zeile3
 */
export function createDefaultBox(id, zeile1 = '', zeile2 = '', zeile3 = '') {
    return {
        id,
        zeile1,
        zeile2,
        zeile3,
        boxColor: '#ffffff',
        boxColorOpacity: 255,
        cornerColorA: '#fce647',
        cornerColorAOpacity: 255,
        cornerColorB: '#fce647',
        cornerColorBOpacity: 255,
        textColor: '#000000',
        textColorOpacity: 255,
        rainbowCorners: false,
        animFile: '',
        showAnim: false,
        animScale: 3,
        animX: 0,
        animY: 0
    };
}

/**
 * Macht einen beliebigen Text (z. B. einen Programmpunktnamen) zu einer
 * sicheren Box-ID: nur a-z, 0-9 und Unterstrich, keine führenden/haften _
 * @param {string} name
 * @param {string} fallback Wird verwendet, wenn nichts Übrig bleibt
 */
export function sanitizeBoxId(name, fallback = 'box') {
    const cleaned = String(name || '')
        .toLowerCase()
        .replace(/[^a-z0-9]/g, '_')
        .replace(/_+/g, '_')
        .replace(/^_|_$/g, '');
    return cleaned || fallback;
}

/**
 * Stellt sicher, dass `id` gegen eine Liste bereits genutzter IDs eindeutig
 * ist; hängt bei Kollision _2, _3, … an (F3).
 * @param {string} id
 * @param {Iterable<string>} usedIds
 */
export function uniqueBoxId(id, usedIds) {
    const used = new Set(usedIds);
    if (!used.has(id)) return id;
    let n = 2;
    while (used.has(`${id}_${n}`)) n++;
    return `${id}_${n}`;
}

/**
 * Maskiert einen Wert für die sichere Verwendung in HTML (Text UND Attribute).
 * @param {*} value
 */
export function escapeHtml(value) {
    return String(value ?? '')
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;')
        .replace(/'/g, '&#39;');
}
