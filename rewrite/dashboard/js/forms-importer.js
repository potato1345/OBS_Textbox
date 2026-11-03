/**
 * @file forms-importer.js
 * Schnittstelle für den Liedplan-Import: Google Forms API, CSV/XLSX/ZIP-Uploads
 * und die gemeinsame Übernahme-Logik für alle Import-Quellen (AI, Datei, Forms).
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Zusammenspiel:
 *   - Kommuniziert mit den Server-Endpunkten /api/forms/*
 *   - Mappt Gottesdienst-Datenstrukturen auf Boxen-Inhalte.
 *   - Aktualisiert den Zustand via storage.js und synchronisiert bei Bedarf mit OBS.
 */

import { getState, updateState } from './storage.js';
import { pushStateToOBS } from './obs-sync.js';
import { LP_DEBUG, createDefaultBox, sanitizeBoxId, uniqueBoxId } from './box-utils.js';

/**
 * Ruft die neueste Antwort direkt über das Backend (Google Forms API) ab.
 *
 * @returns {Promise<Object>} Das Gottesdienst-Objekt mit Metadaten und 12 Programmpunkten.
 * @throws {Error} Wenn der Abruf fehlschlägt oder das Backend einen Fehler meldet.
 */
export async function fetchLatestFormsService() {
    if (LP_DEBUG) console.debug('[liedplan] Frage neuesten Google-Forms-Liedplan ab (/api/forms/latest)');
    const res = await fetch('/api/forms/latest');
    const json = await res.json().catch(() => ({}));

    if (!res.ok || !json.success) {
        throw new Error(json.error || `Fehler beim Laden der Antworten (HTTP ${res.status})`);
    }

    if (LP_DEBUG) console.debug('[liedplan] Neuesten Liedplan erfolgreich empfangen:', json.service?.woche);
    return json.service;
}

/**
 * Lädt ALLE Einreichungen des Liedplan-Formulars über das Backend
 * (Google Forms API, neueste zuerst).
 *
 * @returns {Promise<{services: Array, count: number}>} Liste aller Gottesdienste und Gesamtzahl.
 * @throws {Error} Bei Netzwerk- oder API-Fehlern.
 */
export async function fetchAllFormsServices() {
    if (LP_DEBUG) console.debug('[liedplan] Frage alle Formular-Einreichungen ab (/api/forms/responses)');
    const res = await fetch('/api/forms/responses');
    const json = await res.json().catch(() => ({}));

    if (!res.ok || !json.success) {
        throw new Error(json.error || `Fehler beim Laden der Einreichungen (HTTP ${res.status})`);
    }

    if (LP_DEBUG) console.debug('[liedplan] Einreichungen geladen:', json.responses);
    return { services: json.services || [], count: json.responses || 0 };
}

/**
 * Lädt eine Antwort-Export-Datei (CSV, XLSX, ZIP) an das Backend hoch.
 *
 * @param {File} file - Hochzuladende Export-Datei von Google Forms.
 * @returns {Promise<{services: Array, count: number, filename: string, service: Object}>}
 * @throws {Error} Wenn das Parsen oder Hochladen fehlschlägt.
 */
export async function uploadAnswersFile(file) {
    if (LP_DEBUG) console.debug('[liedplan] Lade Exportdatei hoch:', file.name, file.size, 'Bytes');
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

    if (LP_DEBUG) console.debug('[liedplan] Exportdatei erfolgreich verarbeitet:', json.filename, 'Einträge:', json.responses);
    return {
        services: json.services || [],
        count: json.responses || 0,
        filename: json.filename,
        service: json.service
    };
}

/**
 * Mappt die 12 Programmpunkte eines Gottesdienst-Objekts auf Import-Zeilen.
 *
 * @param {Object} service - Gottesdienst { woche, tag, organist, zeitstempel, punkte: [{ name, gl, verse, titel }] }.
 * @returns {Array<{zeile1: string, zeile2: string, zeile3: string}>} Array von Zeilenobjekten für die Karten.
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
 *
 * @param {Array<{zeile1: string, zeile2: string, zeile3: string}>} items - Zu übernehmende Elemente.
 * @param {Object} [options={ replaceExisting: true }] - Optionen (z. B. bestehende Boxen überschreiben).
 */
export function applyItemsToDashboard(items, options = { replaceExisting: true }) {
    if (!Array.isArray(items) || items.length === 0) {
        throw new Error('Keine importierbaren Programmpunkte vorhanden.');
    }

    if (LP_DEBUG) console.debug('[liedplan] Übernehme', items.length, 'Punkte ins Dashboard (replace =', options.replaceExisting, ')');

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
