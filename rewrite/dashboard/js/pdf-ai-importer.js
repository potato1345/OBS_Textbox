/**
 * @file pdf-ai-importer.js
 * Extrahiert Text aus PDF/Word-Dateien und verarbeitet diesen via OpenRouter (kostenlose Modelle)
 * oder über einen deterministischen Offline-Fallback-Parser zu strukturierten Programmpunkten.
 */

export const DEFAULT_SYSTEM_PROMPT = `Du bist ein Parser für Gottesdienstprogramme (Liedpläne).
Du erhältst den Text eines Liedplans und erzeugst daraus die Programmpunkte der Feier, in Dokumentreihenfolge.

Regeln:
- Ignoriere Kopfzeilen: Ort/Titel (z. B. "Liedplan …"), Datum/Woche, Tagesangaben (Dienstag/Donnerstag/Sonntag), "Organist …", Personen-Namen.
- Ein Programmpunkt ist z. B.: Eröffnung/Einzug, Kyrie, Gloria, Credo, Halleluja, Zwischengesang, Gabenbereitung, Heilig, Agnus Dei, Kommunion, Lesung, Predigt, Gebet, Danklied, Schlusslied, Segen.
- Pro Programmpunkt genau drei Felder:
  zeile1 = Bezeichnung des Programmpunkts (z. B. "Eröffnung", "Kyrie", "Schlusslied").
  zeile2 = Gotteslob-Nummer mit Versen, exakt wie im Text (z. B. "144, 1+4+6"). Keine Nummer => "".
  zeile3 = Liedtitel, falls vorhanden (z. B. "Nun jauchzt dem Herren, aller Welt"). Kein Lied (z. B. nur Orgel) => "".
- "Kommunion Orgel" => zeile1="Kommunion", zeile2="Orgel", zeile3="".
- Zerlege Programmpunkt, Nummer und Titel auch dann korrekt, wenn sie auf einer Zeile stehen.

Gib AUSSCHLIESSLICH gültiges JSON zurück, ohne Markdown und ohne zusätzlichen Text:
{"items":[{"zeile1":"...","zeile2":"...","zeile3":"..."}]}`.trim();

let cachedSongReference = null;

/**
 * Zerlegt die Gotteslob-Referenztabelle (Markdown) in ein Objekt
 * { Liednummer: { title, rubrik} }. Die Header-Zeile wird per erster Zelle
 * ("Nr") erkannt und nicht per Zeilenindex referenziert, damit zusätzliche
 * Titel-/Kopfzeilen vor der Tabelle keinen Schaden anrichten.
 */
export function parseSongReference(markdownText) {
    const songRef = {};
    if (!markdownText) return songRef;

    const lines = markdownText.split('\n');
    let start = 0;

    // Header-Zeile suchen: erste Pipe-Zelle lautet "Nr". Nicht gefunden → ab 0 parsen.
    for (let h = 0; h < lines.length; h++) {
        const line = lines[h].trim();
        if (!line.startsWith('|')) continue;
        const firstCell = line.slice(1).split('|')[0].trim();
        if (/^Nr$/i.test(firstCell)) {
            start = h + 1;
            break;
        }
    }

    for (let i = start; i < lines.length; i++) {
        const line = lines[i].trim();
        if (!line || !line.startsWith('|')) continue;
        if (/^[\s\-|]+$/.test(line)) continue; // Separator-Zeile (| --- | --- | …)

        const parts = line.split('|').map(s => s.trim());
        if (parts.length >= 3) {
            const number = parts[1];
            const rawTitle = parts[2] || '';
            const title = rawTitle.replace(/\s*\([A-Za-z]+\)\s*$/, '').trim();
            const rubrik = parts[4] || '';

            if (number) {
                songRef[number] = { title, rubrik };
            }
        }
    }

    return songRef;
}

/**
 * Lädt die Gotteslob-Referenztabelle (lieder.md) und indiziert sie nach Liednummer.
 */
export async function loadSongReference() {
    if (cachedSongReference) return cachedSongReference;

    const paths = ['lieder.md', '/dashboard/lieder.md'];
    let markdownText = '';

    for (const p of paths) {
        try {
            const resp = await fetch(p);
            if (resp.ok) {
                markdownText = await resp.text();
                break;
            }
        } catch (e) {
            // Nächster Pfad
        }
    }

    cachedSongReference = parseSongReference(markdownText);
    return cachedSongReference;
}

/**
 * Extrahiert Text aus einer PDF via clientseitigem PDF.js.
 */
export async function extractTextWithPdfJs(file) {
    if (typeof window.pdfjsLib === 'undefined') {
        throw new Error('PDF.js ist nicht geladen.');
    }
    const arrayBuffer = await file.arrayBuffer();
    const pdf = await window.pdfjsLib.getDocument({ data: arrayBuffer }).promise;
    let fullText = '';

    for (let i = 1; i <= pdf.numPages; i++) {
        const page = await pdf.getPage(i);
        const textContent = await page.getTextContent();
        let pageText = '';

        for (const item of textContent.items) {
            if (item.hasEOL) {
                pageText = (pageText.endsWith(' ') ? pageText.slice(0, -1) : pageText) + '\n';
            } else {
                pageText += (item.str || '') + ' ';
            }
        }
        fullText += pageText + '\n';
    }

    return fullText.replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim();
}

/**
 * Konvertiert ein Dokument (DOCX, PDF) über das lokale Server-Backend (MarkItDown).
 */
export async function convertDocumentViaServer(file) {
    const res = await fetch('/api/documents/convert', {
        method: 'POST',
        headers: {
            'Content-Type': file.type || 'application/octet-stream',
            'X-Filename': encodeURIComponent(file.name || 'document')
        },
        body: file
    });

    const json = await res.json().catch(() => ({}));
    if (!res.ok || !json.success) {
        throw new Error(json.error || `Server-Konvertierung fehlgeschlagen (HTTP ${res.status})`);
    }
    return json.markdown || '';
}

/**
 * Extrahiert den Inhalt aus einem beliebigen Dokument (PDF, DOCX).
 */
export async function extractDocumentText(file) {
    const name = (file.name || '').toLowerCase();
    const isDocx = name.endsWith('.docx') || (file.type && file.type.includes('word'));

    if (isDocx) {
        return await convertDocumentViaServer(file);
    }

    // PDF: Zuerst lokal via PDF.js, bei Fehler Server-Fallback
    try {
        const text = await extractTextWithPdfJs(file);
        if (text && text.trim().length > 0) {
            return text;
        }
    } catch (e) {
        console.warn('[AI Importer] Lokales PDF.js fehlgeschlagen, nutze Server-Fallback:', e.message);
    }

    return await convertDocumentViaServer(file);
}

// -------------------------------------------------------------
// OpenRouter Free-Tier Aufrufe
// -------------------------------------------------------------

const FREE_EXCLUDE = id => id.includes('-vl') || id.includes('content-safety') || id.includes('2.6b');
const MODEL_PRIORITIES = [
    'gemma-4-31b', 'gemma-4-26b', 'nemotron-3-super-120b',
    'llama-3.3-70b', 'llama-4', 'deepseek', 'qwen-2.5', 'qwen3',
    'mistral', 'gemini', 'phi-4'
];

export async function discoverFreeOpenRouterModels(apiKey) {
    const res = await fetch('https://openrouter.ai/api/v1/models', {
        headers: { 'Authorization': `Bearer ${apiKey}` }
    });
    if (!res.ok) throw new Error(`Konnte Modellliste nicht abrufen (HTTP ${res.status})`);
    const data = await res.json();
    let free = (data.data || []).filter(m => typeof m.id === 'string' && m.id.endsWith(':free'));
    free = free.filter(m => !FREE_EXCLUDE(m.id));

    const score = (m) => {
        const id = m.id;
        for (let i = 0; i < MODEL_PRIORITIES.length; i++) {
            if (id.includes(MODEL_PRIORITIES[i])) return 1000 - i * 10;
        }
        return 100;
    };

    free.sort((a, b) => score(b) - score(a) || ((b.context_length || 0) - (a.context_length || 0)));
    return free.map(m => m.id);
}

async function callOpenRouterModelOnce(apiKey, model, systemPrompt, userText, timeoutMs = 70000) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);

    try {
        const res = await fetch('https://openrouter.ai/api/v1/chat/completions', {
            method: 'POST',
            signal: controller.signal,
            headers: {
                'Authorization': `Bearer ${apiKey}`,
                'Content-Type': 'application/json',
                'HTTP-Referer': window.location.origin || 'http://localhost',
                'X-Title': 'OBS-Textbox'
            },
            body: JSON.stringify({
                model,
                messages: [
                    { role: 'system', content: systemPrompt },
                    { role: 'user', content: userText }
                ],
                temperature: 0,
                max_tokens: 3500
            })
        });

        if (!res.ok) {
            const errJson = await res.json().catch(() => ({}));
            const msg = errJson?.error?.message || `HTTP ${res.status}`;
            const err = new Error(msg);
            err.status = res.status;
            err.retryable = [429, 502, 503, 504, 524, 529].includes(res.status);
            throw err;
        }

        const data = await res.json();
        const content = data.choices?.[0]?.message?.content;
        if (!content || !content.trim()) {
            const err = new Error('Leere Antwort vom Modell erhalten.');
            err.retryable = true;
            throw err;
        }

        return { content: content.trim(), modelUsed: data.model || model };
    } finally {
        clearTimeout(timer);
    }
}

export function parseJsonClean(raw) {
    let s = String(raw).trim();
    s = s.replace(/^```(json)?\s*/i, '').replace(/```\s*$/, '').trim();
    const start = s.indexOf('{');
    const end = s.lastIndexOf('}');
    if (start === -1 || end === -1 || end < start) {
        throw new Error('Kein valides JSON im Modell-Ergebnis gefunden.');
    }
    return JSON.parse(s.slice(start, end + 1));
}

export function enrichTitlesFromReference(items, songRef) {
    if (!Array.isArray(items) || !songRef) return items;

    // Basisnummern indizieren (z.B. "144" aus "144,1")
    const baseLookup = {};
    for (const [key, val] of Object.entries(songRef)) {
        const base = key.split(',')[0].trim();
        if (base && val.title && !baseLookup[base]) {
            baseLookup[base] = val.title;
        }
    }

    for (const item of items) {
        if (!item.zeile3 || !item.zeile3.trim()) {
            const numMatch = (item.zeile2 || '').match(/\b(\d{1,3})\b/);
            if (numMatch && baseLookup[numMatch[1]]) {
                item.zeile3 = baseLookup[numMatch[1]];
            }
        }
    }
    return items;
}

export async function processWithLLM(apiKey, documentText, systemPrompt, onProgress) {
    if (onProgress) onProgress('Ermittle verfügbare freie Modelle...');
    const freeModels = await discoverFreeOpenRouterModels(apiKey);
    if (!freeModels || freeModels.length === 0) {
        throw new Error('Keine kostenlosen Modelle auf OpenRouter verfügbar.');
    }

    const candidates = freeModels.slice(0, 6);
    const errors = [];

    for (const model of candidates) {
        for (let attempt = 1; attempt <= 2; attempt++) {
            if (onProgress) onProgress(`Modell ${model} (Versuch ${attempt}/2)...`);
            try {
                const { content, modelUsed } = await callOpenRouterModelOnce(
                    apiKey,
                    model,
                    systemPrompt || DEFAULT_SYSTEM_PROMPT,
                    `Liedplan-Text:\n${documentText}`
                );

                const parsed = parseJsonClean(content);
                if (!parsed || !Array.isArray(parsed.items) || parsed.items.length === 0) {
                    throw new Error('Antwort enthielt keine Programmpunkte (items).');
                }

                const songRef = await loadSongReference();
                enrichTitlesFromReference(parsed.items, songRef);
                return { parsed, modelUsed };
            } catch (err) {
                errors.push(`${model} [${attempt}]: ${err.message}`);
                const isRetryable = err.retryable || err.name === 'AbortError';
                if (!isRetryable) break;

                if (attempt === 1) {
                    if (onProgress) onProgress(`Warte 3s vor neuem Versuch...`);
                    await new Promise(r => setTimeout(r, 3000));
                }
            }
        }
    }

    throw new Error(`Alle AI-Modelle fehlgeschlagen:\n${errors.slice(-4).join('\n')}`);
}

// -------------------------------------------------------------
// Deterministischer lokaler Offline-Parser
// -------------------------------------------------------------

const PP_KEYWORDS = [
    'eröffnung', 'einführung', 'einzug', 'eintritt',
    'kyrie', 'gott danke', 'gloria', 'zwischengesang', 'halleluja',
    'credo', 'glaubensbekenntnis', 'gabenbereitung', 'opfergabe',
    'heilig', 'agnus dei', 'lamm gottes', 'kommunion', 'kommunionlied',
    'lesung', 'predigt', 'gebet', 'segen', 'danklied', 'schlusslied', 'auszug'
];

function isHeader(line) {
    const l = line.toLowerCase().trim();
    if (!l) return true;
    if (/\bliedplan\b/.test(l) || /\bwoche\b/.test(l) || /\borganist\b/.test(l)) return true;
    if (/^\s*(dienstag|donnerstag|sonntag|samstag|montag|freitag)\b/i.test(l)) return true;
    return false;
}

export function parseLiedplanLocally(text) {
    if (!text) return [];
    const lines = text.split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    const items = [];
    let current = null;

    for (const line of lines) {
        if (isHeader(line)) continue;

        const numMatch = line.match(/\b\d{1,3}\b(?:(?:[.,;]\s*[\d+\-– ]*[\d]))?/);
        if (numMatch) {
            const num = numMatch[0];
            const idx = line.indexOf(num);
            const before = line.slice(0, idx).replace(/[,\s]+$/, '').trim();
            const after = line.slice(idx + num.length).replace(/^[,\s]+/, '').trim();

            current = {
                zeile1: before || '',
                zeile2: num,
                zeile3: after || ''
            };
            items.push(current);
            continue;
        }

        const lower = line.toLowerCase();
        const matchedKw = PP_KEYWORDS.find(kw => lower === kw || lower.startsWith(kw + ' '));

        if (matchedKw) {
            const label = line.slice(0, matchedKw.length);
            const rest = line.slice(matchedKw.length).replace(/^[,\s]+/, '').trim();
            current = {
                zeile1: label,
                zeile2: '',
                zeile3: rest
            };
            items.push(current);
            continue;
        }

        if (current) {
            current.zeile3 = current.zeile3 ? `${current.zeile3} ${line}` : line;
        }
    }

    return items
        .map(it => ({
            zeile1: (it.zeile1 || '').trim(),
            zeile2: (it.zeile2 || '').trim(),
            zeile3: (it.zeile3 || '').trim()
        }))
        .filter(it => it.zeile1 || it.zeile2 || it.zeile3);
}
