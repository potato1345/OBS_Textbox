/**
 * @file gf-import.js
 * Google Forms Import-Logik für das v1 Web-Dashboard (main/controlui/).
 *
 * Zweck:
 *   - Ermöglicht den dateibasierten Import von Google-Forms-Antworten (CSV, XLSX, ZIP)
 *     über den Server-Endpunkt POST /api/import/answers.
 *   - Ermöglicht den direkten API-Abruf der neuesten Formularantwort über GET /api/form-responses.
 *   - Transformiert die 12 Programmpunkte der Gottesdienst-Struktur in Dashboard-Karten.
 *
 * Zusammenspiel:
 *   - Wird in main/controlui/control.html eingebunden.
 *   - Kommuniziert mit main/server.py (bzw. forms_import.py) und ruft applyLLMResultToCards() in control.js auf.
 */

/**
 * Mappt den Gottesdienst-JSON des Servers auf die Kartenstruktur
 * (12 Karten: zeile1 = Programmpunkt, zeile2 = GL-Nummer + Verse, zeile3 = Liedtitel).
 * @param {Object} d Gottesdienst-Objekt { woche, tag, organist, zeitstempel, punkte: [{name, gl, verse, titel}] }
 * @returns {Array<{zeile1: string, zeile2: string, zeile3: string}>} Array für Dashboard-Karten
 */
function gottesdienstToItems(d) {
    return (d && d.punkte ? d.punkte : []).map(p => {
        const gl = p.gl ? ('GL ' + p.gl) : '';
        return {
            zeile1: p.name || '',
            zeile2: [gl, p.verse || ''].filter(Boolean).join(' | '),
            zeile3: p.titel || ''
        };
    });
}

/**
 * Aktualisiert den Statustext im Google-Forms-Import-Modal.
 * @param {string} text Anzuzeigende Nachricht
 * @param {boolean} [isError=false] Ob es sich um eine Fehlermeldung handelt (rot statt grün)
 */
function gfSetStatus(text, isError) {
    const el = document.getElementById('gf-status');
    if (!el) return;
    el.textContent = text || '';
    el.style.color = isError ? '#ff6b6b' : '#51cf66';
}

/**
 * Füllt die Dashboard-Karten aus einer validierten Gottesdienst-Antwort.
 * @param {Object} dienst Gottesdienst-JSON mit Metadaten und 12 Programmpunkten
 * @param {string} quelle Kurzbezeichnung der Quelle für die Erfolgsmeldung
 */
function applyGottesdienst(dienst, quelle) {
    const items = gottesdienstToItems(dienst);
    if (items.length === 0) {
        gfSetStatus('Keine Programmpunkte in der Antwort gefunden.', true);
        return;
    }
    if (typeof applyLLMResultToCards === 'function') {
        applyLLMResultToCards({ items: items }, { createNewCards: true, replaceExisting: true });
    }
    const head = [dienst.tag, dienst.organist].filter(Boolean).join(', ');
    const info = (head ? head + ' · ' : '') + 'Woche: ' + (dienst.woche || '—');
    gfSetStatus('✔ Importiert (' + (quelle || 'unbekannt') + '): ' + info + ', ' + items.length + ' Karten gefüllt.');
}

/**
 * Öffnet das Google-Forms-Import-Modal und setzt die Dateiauswahl zurück.
 */
function openGFImportModal() {
    const m = document.getElementById('gf-import-modal');
    if (!m) return;
    gfSetStatus('');
    const input = document.getElementById('gf-file-input');
    if (input) input.value = '';
    m.hidden = false;
}

/**
 * Schließt das Google-Forms-Import-Modal.
 */
function closeGFImportModal() {
    const m = document.getElementById('gf-import-modal');
    if (m) m.hidden = true;
}

/**
 * Liest die im Modal gewählte Datei ein und sendet sie an den Server (POST /api/import/answers).
 * Unterstützt CSV, XLSX und ZIP.
 * @returns {Promise<void>}
 */
async function importFormFile() {
    const input = document.getElementById('gf-file-input');
    const file = input && input.files && input.files[0];
    if (!file) {
        gfSetStatus('Bitte zuerst eine Datei auswählen (CSV, XLSX oder ZIP).', true);
        return;
    }
    gfSetStatus('Importiere ' + file.name + ' …');
    try {
        const resp = await fetch('/api/import/answers', {
            method: 'POST',
            headers: { 'X-Filename': encodeURIComponent(file.name) },
            body: file
        });
        const data = await resp.json();
        if (!resp.ok || !data.success) {
            gfSetStatus('Fehler: ' + (data.error || ('HTTP ' + resp.status)), true);
            return;
        }
        const info = data.anzahl_antworten > 1 ? 'Datei (' + data.anzahl_antworten + ' Antworten, neueste übernommen)' : 'Datei';
        applyGottesdienst(data.gottesdienst, info);
    } catch (e) {
        gfSetStatus('Serverfehler: ' + e.message, true);
    }
}

/**
 * Holt die neueste Formular-Antwort direkt per Server-Endpunkt GET /api/form-responses von der Google API.
 * @returns {Promise<void>}
 */
async function fetchLatestFromGoogle() {
    gfSetStatus('Hole neueste Antwort von Google …');
    try {
        const resp = await fetch('/api/form-responses');
        const data = await resp.json();
        if (!resp.ok || !data.success) {
            gfSetStatus('Fehler: ' + (data.error || ('HTTP ' + resp.status)), true);
            return;
        }
        applyGottesdienst(data.gottesdienst, 'Google');
    } catch (e) {
        gfSetStatus('Serverfehler: ' + e.message, true);
    }
}
