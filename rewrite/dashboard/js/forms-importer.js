/**
 * @file forms-importer.js
 * Schnittstelle für den Liedplan-Import: Google Forms API, CSV/XLSX/ZIP-Uploads
 * und die gemeinsame Übernahme-Logik für alle Import-Quellen (AI, Datei, Forms).
 */

import { getState, updateState } from './storage.js';
import { pushStateToOBS } from './obs-sync.js';
import { createDefaultBox, sanitizeBoxId, uniqueBoxId } from './box-utils.js';

/**
 * Ruft die neueste Antwort direkt über das Backend (Google Forms API) ab.
 */
export async function fetchLatestFormsService() {
    const res = await fetch('/api/forms/latest');
    const json = await res.json().catch(() => ({}));

    if (!res.ok || !json.success) {
        throw new Error(json.error || `Fehler beim Laden der Antworten (HTTP ${res.status})`);
    }

    return json.service;
}

/**
 * Lädt ALLE Einreichungen des Liedplan-Formulars über das Backend
 * (Google Forms API, neueste zuerst).
 * @returns {Promise<{services: Array, count: number}>}
 */
export async function fetchAllFormsServices() {
    const res = await fetch('/api/forms/responses');
    const json = await res.json().catch(() => ({}));

    if (!res.ok || !json.success) {
        throw new Error(json.error || `Fehler beim Laden der Einreichungen (HTTP ${res.status})`);
    }

    return { services: json.services || [], count: json.responses || 0 };
}

/**
 * Lädt eine Antwort-Export-Datei (CSV, XLSX, ZIP) an das Backend hoch.
 * @returns {Promise<{services: Array, count: number, filename: string, service: Object}>}
 */
export async function uploadAnswersFile(file) {
    const res = await fetch('/api/forms/upload', {
        method: 'POST',
        headers: {
            'Content-Type': file.type || 'application/octet-stream',
            'X-Filename': encodeURIComponent(file.name || 'antworten.csv')
        },
        body: file
    });

    const json = await res.json().catch(() => ({}));
    if (!res.ok || !json.success) {
        throw new Error(json.error || `Fehler beim Analysieren der Datei (HTTP ${res.status})`);
    }

    return {
        services: json.services || [],
        count: json.responses || 0,
        filename: json.filename,
        service: json.service
    };
}

/**
 * Mappt die 12 Programmpunkte eines Gottesdienst-Objekts auf Import-Zeilen.
 * @param {Object} service { woche, tag, organist, zeitstempel, punkte: [{ name, gl, verse, titel }] }
 * @returns {Array<{zeile1: string, zeile2: string, zeile3: string}>}
 */
export function serviceToItems(service) {
    if (!service || !Array.isArray(service.punkte)) {
        throw new Error('Ungültige Gottesdienst-Daten.');
    }
    return service.punkte
        .filter(p => p.name || p.gl || p.verse || p.titel)
        .map(p => {
            let z2 = p.gl || '';
            if (p.verse && p.verse.trim()) {
                z2 = z2 ? `${z2}, ${p.verse.trim()}` : p.verse.trim();
            }
            return { zeile1: p.name || '', zeile2: z2, zeile3: p.titel || '' };
        });
}

/**
 * Übernimmt importierte Programmpunkte (AI, Datei oder Forms) in das Dashboard.
 * Gemeinsame Logik für alle Quellen: saubere Box-IDs mit Kollisionsbehandlung
 * und zentrale Box-Factory (F3/F4).
 * @param {Array<{zeile1: string, zeile2: string, zeile3: string}>} items
 * @param {Object} options { replaceExisting: boolean }
 */
export function applyItemsToDashboard(items, options = { replaceExisting: true }) {
    if (!Array.isArray(items) || items.length === 0) {
        throw new Error('Keine importierbaren Programmpunkte vorhanden.');
    }

    updateState(state => {
        if (options.replaceExisting) {
            state.boxes = [];
        }
        const used = new Set(state.boxes.map(b => b.id));

        items.forEach((it, idx) => {
            const baseId = sanitizeBoxId(it.zeile1, `punkt_${idx + 1}`);
            const existing = state.boxes.find(b => b.id === baseId);
            if (existing) {
                // Passende Box vorhanden (Erstimport oder Replace=false): Inhalt aktualisieren
                existing.zeile1 = it.zeile1 || '';
                existing.zeile2 = it.zeile2 || '';
                existing.zeile3 = it.zeile3 || '';
                return;
            }
            const id = uniqueBoxId(baseId, used);
            used.add(id);
            state.boxes.push(createDefaultBox(id, it.zeile1 || '', it.zeile2 || '', it.zeile3 || ''));
        });
    }, 'apply_import');

    if (getState().autoSend) {
        pushStateToOBS();
    }
}
