/* =====================================================
 * GOOGLE FORM IMPORT
 * Importiert Antwort-Exports (CSV/XLSX/ZIP) des Formulars
 * „Liedplan – Gottesdienstprogramm" in die Dashboard-Karten —
 * deterministisch, ohne AI. Alternativ: neueste Antwort
 * direkt per Google API (GET /api/form-responses).
 * ===================================================== */

/**
 * Mappt den Gottesdienst-JSON des Servers auf die Kartenstruktur
 * (12 Karten: zeile1 = Programmpunkt, zeile2 = GL-Nummer + Verse, zeile3 = Liedtitel).
 * @param {Object} d { woche, tag, organist, zeitstempel, punkte: [{name, gl, verse, titel}] }
 * @returns {Array<{zeile1: string, zeile2: string, zeile3: string}>}
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

function gfSetStatus(text, isError) {
    const el = document.getElementById('gf-status');
    if (!el) return;
    el.textContent = text || '';
    el.style.color = isError ? '#ff6b6b' : '#51cf66';
}

/**
 * Füllt die Karten aus einer Gottesdienst-Antwort.
 * @param {Object} dienst Gottesdienst-JSON
 * @param {string} quelle Kurzbezeichnung der Quelle für die Statusmeldung
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

function openGFImportModal() {
    const m = document.getElementById('gf-import-modal');
    if (!m) return;
    gfSetStatus('');
    const input = document.getElementById('gf-file-input');
    if (input) input.value = '';
    m.hidden = false;
}

function closeGFImportModal() {
    const m = document.getElementById('gf-import-modal');
    if (m) m.hidden = true;
}

/**
 * Importiert die gewählte Datei (CSV/XLSX/ZIP) per POST /api/import/answers.
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
 * Holt die neueste Antwort direkt von Google (GET /api/form-responses).
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
