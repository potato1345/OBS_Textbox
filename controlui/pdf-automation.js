/**
 * @file pdf-automation.js
 * Automatisiert den Import von Gottesdienstprogrammen aus PDF- und Word-Dateien (.docx).
 *
 * Ablauf:
 *  1. Text extrahieren:
 *     - PDF  -> client-seitig via pdf.js (zeilenorientiert; laeuft auch ohne Server)
 *     - DOCX -> server-seitig via /api/parse-document (MarkItDown)
 *  2. OpenRouter Free-Tier: verfuegbare kostenlose Modelle werden LIVE ermittelt,
 *     dann mit Timeouts, Backoff- und Modell-Fallback aufgerufen.
 *  3. Robustes JSON-Parsen + lokale Titel-Vervollstaendigung aus lieder.md.
 *  4. Fallback: Falls kein Free-Tier-Modell erreichbar ist, wird ein deterministischer
 *     lokaler Parser genutzt, damit der Import trotzdem funktioniert (offline).
 */

const DEFAULT_SYSTEM_PROMPT = `Du bist ein Parser für Gottesdienstprogramme (Liedpläne).
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

/**
 * Gibt den aktuell konfigurierten System-Prompt zurück (aus dem Modal oder Default).
 */
function getSystemPrompt() {
    const input = document.getElementById('ai-system-prompt');
    if (input && input.value.trim()) {
        return input.value.trim();
    }
    const saved = localStorage.getItem('ai_system_prompt');
    return saved || DEFAULT_SYSTEM_PROMPT;
}

/**
 * Setzt den System-Prompt auf den Standardwert zurück.
 */
function resetSystemPrompt() {
    const input = document.getElementById('ai-system-prompt');
    if (input) {
        input.value = DEFAULT_SYSTEM_PROMPT;
    }
    localStorage.removeItem('ai_system_prompt');
    if (typeof showToast === 'function') {
        showToast('info', 'System Prompt auf Standard zurückgesetzt');
    }
}

// In-Memory-Cache für die Gotteslob-Referenzen
let cachedSongReference = null;

/**
 * Lädt die Gotteslob-Referenztabelle (lieder.md) und parst sie in eine Map.
 * @returns {Promise<Object>} Eine Map von Songnummer -> { title, type, rubrik }
 */
async function loadSongReference() {
    if (cachedSongReference) {
        return cachedSongReference;
    }

    try {
        console.log('[AI Import] Lade Liedreferenz aus lieder.md...');
        const response = await fetch('lieder.md');
        if (!response.ok) {
            throw new Error(`Konnte lieder.md nicht laden (HTTP ${response.status})`);
        }

        const markdownText = await response.text();
        const lines = markdownText.split('\n');
        const songRef = {};

        // Die ersten beiden Zeilen sind Tabellen-Header und Trennzeile
        for (let i = 2; i < lines.length; i++) {
            const line = lines[i].trim();
            if (!line) continue;

            const parts = line.split('|').map(p => p.trim());
            // | Nr | Titel | altes GL | Rubrik | Unterrubrik | ökumenisch |
            if (parts.length >= 3) {
                const number = parts[1]; // Index 1, da Zeile mit '|' beginnt
                const rawTitle = parts[2] || '';

                // Typ-Marker am Titel-Ende entfernen z. B. (L), (G), (Kv), (Kan), (R), (Res)
                const typeMatch = rawTitle.match(/\s*\(([A-Za-z]+)\)\s*$/);
                const type = typeMatch ? typeMatch[1] : '';
                const title = rawTitle.replace(/\s*\([A-Za-z]+\)\s*$/, '').trim();

                const rubrik = parts[4] || '';

                if (number) {
                    songRef[number] = { title, type, rubrik };
                }
            }
        }

        console.log(`[AI Import] ${Object.keys(songRef).length} Lieder geladen.`);
        cachedSongReference = songRef;
        return songRef;
    } catch (error) {
        console.error('[AI Import] Liedreferenz nicht geladen (fortfahren ohne):', error.message);
        cachedSongReference = {};
        return {};
    }
}

/**
 * Extrahiert den Text aus einer PDF-Datei via pdf.js im Browser, zeilenorientiert.
 * @param {File} file Die PDF-Datei.
 * @returns {Promise<string>} Der extrahierte Text.
 */
async function extractTextFromPDF(file) {
    const arrayBuffer = await file.arrayBuffer();
    const pdf = await pdfjsLib.getDocument(arrayBuffer).promise;
    const numPages = pdf.numPages;
    let fullText = '';

    for (let i = 1; i <= numPages; i++) {
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
 * Parst das Dokument server-seitig via MarkItDown (/api/parse-document).
 * @param {File} file
 * @returns {Promise<string>}
 */
async function parseViaServer(file) {
    const fileName = file.name || 'document.pdf';
    const response = await fetch('/api/parse-document', {
        method: 'POST',
        headers: {
            'Content-Type': file.type || 'application/octet-stream',
            'X-Filename': encodeURIComponent(fileName)
        },
        body: file
    });

    if (response.ok) {
        const data = await response.json();
        if (data.success && data.markdown && data.markdown.trim()) {
            return data.markdown;
        }
        if (data.error) throw new Error(data.error);
    } else {
        const errJson = await response.json().catch(() => null);
        const errMsg = errJson && errJson.error ? errJson.error : `HTTP ${response.status}: ${response.statusText}`;
        throw new Error(`MarkItDown-Server: ${errMsg}`);
    }
    throw new Error('MarkItDown lieferte keine Daten');
}

/**
 * Extrahiert den Inhalt (Text/Markdown) aus einer PDF- oder Word-Datei.
 * PDF bevorzugt client-seitig via pdf.js (sauber zeilenorientiert, ohne Server),
 * Word via /api/parse-document (MarkItDown); bei PDF-Fehler auf den Server zurück.
 * @param {File} file Die PDF- oder DOCX-Datei.
 * @returns {Promise<string>} Der extrahierte Text / Markdown.
 */
async function extractDocumentContent(file) {
    const fileName = file.name || 'document.pdf';
    const isDocx = fileName.toLowerCase().endsWith('.docx') || (file.type && file.type.includes('word'));

    if (isDocx) {
        return await parseViaServer(file);
    }

    // PDF: zuerst lokal via pdf.js (zeilenorientiert), sonst MarkItDown-Server
    try {
        const t = await extractTextFromPDF(file);
        if (t && t.trim().length) {
            console.log(`[AI Import] PDF-Text lokal via pdf.js extrahiert (${t.length} Zeichen)`);
            return t;
        }
        throw new Error('pdf.js lieferte keinen Text');
    } catch (e) {
        console.warn('[AI Import] pdf.js fehlgeschlagen, versuche MarkItDown-Server:', e.message);
        return await parseViaServer(file);
    }
}

/* ================= OpenRouter: Modell-Discovery + robuster Aufruf ================= */

const FREE_MODEL_FILTER = id =>
    id.includes('-vl') || id.includes('content-safety') || id.includes('2.6b');

const GENERAL_FREE_HINTS = [
    'gemma-4-31b', 'gemma-4-26b', 'nemotron-3-super-120b', 'nemotron-3-nano-omni-30b',
    'llama-3.3-70b', 'llama-4', 'deepseek', 'qwen-2.5', 'qwen3', 'mistral', 'grok',
    'gemini', 'phi-4', 'command-r'
];
const SECONDARY_FREE_HINTS = [
    'nemotron-3', 'gemma-4', 'gemma-3', 'llama', 'mistral', 'qwen', 'deepseek', 'phi', 'command-r', 'gpt-oss'
];

/**
 * Ermittelt LIVE die verfügbaren kostenlosen OpenRouter-Modelle (Free Tier)
 * und sortiert die größeren, allgemein tauglichen voran.
 * @param {string} apiKey
 * @returns {Promise<string[]>} Liste von Modell-IDs
 */
async function discoverFreeModels(apiKey) {
    const res = await fetch('https://openrouter.ai/api/v1/models', {
        headers: { 'Authorization': 'Bearer ' + apiKey }
    });
    if (!res.ok) throw new Error('Konnte Modellliste nicht laden (HTTP ' + res.status + ')');
    const data = await res.json();
    let free = (data.data || []).filter(m => typeof m.id === 'string' && m.id.endsWith(':free'));
    free = free.filter(m => !FREE_MODEL_FILTER(m.id));

    const score = m => {
        const id = m.id;
        for (const p of GENERAL_FREE_HINTS) if (id.includes(p)) return 1000;
        for (const p of SECONDARY_FREE_HINTS) if (id.includes(p)) return 500;
        return 100;
    };
    free.sort((a, b) => (score(b) - score(a)) || ((b.context_length || 0) - (a.context_length || 0)));
    return free.map(m => m.id);
}

/**
 * Einzelner Chat-Aufruf an ein Modell mit hartem Timeout (AbortController).
 * @returns {Promise<{content: string, modelUsed: string}>}
 */
async function callModelOnce(apiKey, model, systemPrompt, userText, timeoutMs) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    try {
        const referer = (typeof location !== 'undefined' && location.origin) ? location.origin : 'http://localhost';
        const res = await fetch('https://openrouter.ai/api/v1/chat/completions', {
            method: 'POST',
            signal: controller.signal,
            headers: {
                'Authorization': 'Bearer ' + apiKey,
                'Content-Type': 'application/json',
                'HTTP-Referer': referer,
                'X-Title': 'obs-textbox'
            },
            body: JSON.stringify({
                model,
                messages: [
                    { role: 'system', content: systemPrompt },
                    { role: 'user', content: userText }
                ],
                temperature: 0,
                max_tokens: 4000
            })
        });

        if (!res.ok) {
            const errJson = await res.json().catch(() => ({}));
            const msg = (errJson && errJson.error && errJson.error.message) || ('HTTP ' + res.status);
            const e = new Error(msg);
            e.status = res.status;
            e.retryable = [429, 502, 503, 504, 524, 529].includes(res.status);
            throw e;
        }

        const data = await res.json();
        const content = data && data.choices && data.choices[0] && data.choices[0].message
            ? data.choices[0].message.content : null;
        if (!content || !String(content).trim()) {
            const e = new Error('Leere Antwort vom Modell');
            e.retryable = true;
            e.status = 200;
            throw e;
        }
        return { content: String(content), modelUsed: (data && data.model) || model };
    } finally {
        clearTimeout(timer);
    }
}

/**
 * Robustes JSON-Parsen: entfernt Markdown-Zaun und schneidet vom ersten { bis zum letzten }.
 * @param {string} text
 * @returns {object}
 */
function robustParseJSON(text) {
    let s = String(text).trim();
    s = s.replace(/^```(json)?\s*/i, '').replace(/```\s*$/, '').trim();
    const i = s.indexOf('{');
    const j = s.lastIndexOf('}');
    if (i === -1 || j === -1 || j < i) throw new Error('Kein JSON in der Antwort');
    return JSON.parse(s.slice(i, j + 1));
}

/**
 * Vervollständigt fehlende Liedtitel (zeile3) lokal aus dem lieder.md-Index,
 * basierend auf der Gotteslob-Grundnummer aus zeile2.
 * @param {Array} items
 * @param {Object} songRef
 * @returns {Array}
 */
function completeTitlesLocally(items, songRef) {
    if (!Array.isArray(items)) return items;
    const baseTitle = {};
    for (const key of Object.keys(songRef || {})) {
        const parts = String(key).split(',');
        const num = (parts[0] || '').trim();
        const title = songRef[key] && songRef[key].title;
        if (num && title && !baseTitle[num]) baseTitle[num] = title;
    }
    items.forEach(it => {
        const m = it && it.zeile2 ? String(it.zeile2).match(/\b(\d{1,3})\b/) : null;
        if (m && baseTitle[m[1]] && !(it.zeile3 && String(it.zeile3).trim())) {
            it.zeile3 = baseTitle[m[1]];
        }
    });
    return items;
}

/**
 * Ruft OpenRouter Free-Tier auf: ermittelt die Kandidaten live, probiert sie
 * mit Retries/Backoff und Modell-Fallback aus, bis eine gültige JSON-Antwort steht.
 * @param {string} apiKey
 * @param {string} extractedText
 * @param {Object} songReference
 * @param {function} [onProgress]
 * @returns {Promise<{parsed: object, modelUsed: string}>}
 */
async function callOpenRouter(apiKey, extractedText, songReference, onProgress) {
    const systemPrompt = getSystemPrompt();
    const userText = 'Liedplan-Text:\n' + extractedText;
    const MAX_MODELS = 6;
    const RETRIES_PER_MODEL = 2;
    const TIMEOUT_MS = 75000;
    const BACKOFF_MS = [2500, 5000];

    if (onProgress) onProgress('Lade verfügbare kostenlose Modelle...');
    let models = [];
    try {
        models = await discoverFreeModels(apiKey);
    } catch (e) {
        console.warn('[AI Import] Modell-Discovery fehlgeschlagen:', e.message);
    }
    if (!models || models.length === 0) {
        throw new Error('Keine kostenlosen OpenRouter-Modelle gefunden (Free Tier gerade nicht erreichbar).');
    }
    const candidates = models.slice(0, MAX_MODELS);
    console.log('[AI Import] Kandidaten:', candidates.join(', '));

    const errors = [];
    for (const model of candidates) {
        for (let attempt = 1; attempt <= RETRIES_PER_MODEL; attempt++) {
            if (onProgress) onProgress(`Modell ${model} (Versuch ${attempt}/${RETRIES_PER_MODEL})...`);
            try {
                const { content, modelUsed } = await callModelOnce(apiKey, model, systemPrompt, userText, TIMEOUT_MS);
                const parsed = robustParseJSON(content);
                if (!parsed || !Array.isArray(parsed.items) || parsed.items.length === 0) {
                    const e = new Error('Antwort enthält keine "items"');
                    e.retryable = true;
                    throw e;
                }
                completeTitlesLocally(parsed.items, songReference);
                console.log('[AI Import] Erfolgsmodell:', modelUsed);
                return { parsed, modelUsed };
            } catch (e) {
                console.warn(`[AI Import] ${model} Versuch ${attempt}: ${e.message}`);
                errors.push(`${model} (${attempt}): ${e.message}`);
                const isTimeout = e && e.name === 'AbortError';
                const isNetwork = e instanceof TypeError;
                const retryable = !!(e && e.retryable) || isTimeout || isNetwork;
                if (!retryable) break; // z. B. 403 (agentic) oder 401 (Schlüssel) -> nächstes Modell
                if (attempt < RETRIES_PER_MODEL) {
                    const wait = BACKOFF_MS[attempt - 1] || 5000;
                    if (onProgress) onProgress(`Free-Tier belegt, warte ${wait / 1000}s... (Modell ${model})`);
                    await new Promise(r => setTimeout(r, wait));
                }
            }
        }
    }
    throw new Error(
        'Kein kostenloses Modell hat eine gültige Antwort geliefert (Free-Tier-Limitierung).\nDetails:\n'
        + errors.slice(-8).join('\n')
    );
}

/* ================= Lokaler Fallback-Parser (deterministisch, offline) ================= */

const PP_KEYWORDS = [
    'eröffnung', 'einführung', 'einzug', 'eintritt',
    'kyrie', 'gott danke', 'gott danken', 'gott, danke', 'gloria',
    'zwischengesang', 'halleluja', 'credo', 'glaubensbekenntnis', 'gutenamen', 'glaube',
    'gabenbereitung', 'opfergabe', 'heilig', 'agnus dei', 'lamm gottes',
    'kommunion', 'kommunionlied', 'kommuniongang',
    'lesung', 'bibelwort', 'bibeltexte', 'predigt', 'wortgottes', 'wort gottes',
    'gebet', 'vater unser', 'herrengebet', 'segen', 'segensspruch', 'segensworte', 'sendung',
    'danke', 'danklied', 'dankgesang', 'dankopfer', 'abschied', 'abschluss',
    'schlusslied', 'endlied', 'egressus', 'egress', 'auszug', 'finalsong'
];

/**
 * Erkennt Kopfzeilen (Ort, Datum, Woche, Organist, Tage) und gibt sie frei.
 * @param {string} line
 * @returns {boolean}
 */
function isHeaderLine(line) {
    const l = line.toLowerCase().trim();
    if (!l) return true;
    if (/\bliedplan\b/.test(l)) return true;
    if (/\bwoche\b/.test(l)) return true;
    if (/\b(19|20)\d{2}\b/.test(l)) return true;
    if (/\borganist\w*\b/.test(l)) return true;
    if (/\b(gemeinde|pfarrei|dekanat|pfarr|kantor|kantorin)\b/.test(l)) return true;
    if (/vom:?\s*\d/.test(l)) return true;
    const dayNames = ['montag', 'dienstag', 'mittwoch', 'donnerstag', 'freitag', 'samstag', 'sonntag', 'gottesdienst', 'abendmahl'];
    const words = l.replace(/[()]/g, ' ').split(/\s+/).filter(Boolean);
    if (words.some(w => w.includes('gottesdienst'))) return true;
    if (words.length && words.every(w => dayNames.includes(w) || w === 'st.' || w === 'der' || w === 'die' || w === 'und')) {
        return true;
    }
    return false;
}

/**
 * Findet eine Gotteslob-Nummer in einer Zeile (z. B. "144", "144, 1+4+6", "175.2", "437, 1-4").
 * @param {string} line
 * @returns {string|null}
 */
function matchGLNumber(line) {
    const m = line.match(/\b\d{1,3}\b(?:(?:[.,;]\s*[\d+\-– ]*[\d]))?/);
    return m ? m[0] : null;
}

/**
 * Prüft, ob eine Zeile mit einem bekannten Programmpunkt-Schlüssel beginnt.
 * @param {string} line
 * @returns {string|null}
 */
function matchKeyword(line) {
    const l = line.toLowerCase().trim();
    for (const kw of PP_KEYWORDS) {
        if (l === kw || l.startsWith(kw + ' ')) return kw;
    }
    return null;
}

/**
 * Deterministischer Offline-Parser: zerlegt den zeilenorientierten Liedplan-Text
 * in Programmpunkte (zeile1/zeile2/zeile3). Wirkt als Fallback, wenn kein
 * Free-Tier-Modell erreichbar ist.
 * @param {string} text
 * @returns {Array<{zeile1: string, zeile2: string, zeile3: string}>}
 */
function localParseLiedplan(text) {
    if (!text) return [];
    const lines = String(text).split(/\r?\n/).map(s => s.trim()).filter(Boolean);
    const items = [];
    let current = null;
    let lastWasNumberedPP = false;

    const newPP = (z1, z2) => {
        current = { zeile1: z1, zeile2: z2 || '', zeile3: '' };
        items.push(current);
    };

    for (const rawLine of lines) {
        if (isHeaderLine(rawLine)) { lastWasNumberedPP = false; continue; }

        const num = matchGLNumber(rawLine);
        if (num) {
            const idx = rawLine.indexOf(num);
            const before = rawLine.slice(0, idx).replace(/[,\s]+$/, '').trim();
            if (before) {
                newPP(before, num);
            } else if (current && !current.zeile2) {
                current.zeile2 = num; // Nummer ohne Label: an vorherigen Punkt anhängen
            } else {
                newPP('', num);
            }
            lastWasNumberedPP = true;
            continue;
        }

        const kw = matchKeyword(rawLine);
        if (kw) {
            const label = rawLine.slice(0, kw.length);
            const rest = rawLine.slice(kw.length).replace(/^[,\s]+/, '').trim();
            const labelWords = rawLine.split(/\s+/).filter(Boolean).length;
            if (labelWords <= 3 && !lastWasNumberedPP) {
                newPP(label, rest);
                lastWasNumberedPP = false;
                continue;
            }
        }

        if (current) {
            current.zeile3 = current.zeile3 ? (current.zeile3 + ' ' + rawLine).trim() : rawLine;
        }
        lastWasNumberedPP = false;
    }

    return items
        .map(it => ({
            zeile1: (it.zeile1 || '').replace(/\s+/g, ' ').trim(),
            zeile2: (it.zeile2 || '').replace(/\s+/g, ' ').trim(),
            zeile3: (it.zeile3 || '').replace(/\s+/g, ' ').trim()
        }))
        .filter(it => it.zeile1 || it.zeile2 || it.zeile3);
}

/* ================= UI: Karten füllen, Modal, Dropzone, Fortschritt ================= */

/**
 * Wendet das geparste JSON-Ergebnis auf die Karten im Dashboard an.
 * @param {Object} parsedResult Das Ergebnis von OpenRouter ({ items: [...] })
 * @param {Object} settings Einstellungen { createNewCards: boolean, replaceExisting: boolean }
 */
function applyLLMResultToCards(parsedResult, settings) {
    if (settings.replaceExisting) {
        if (typeof clearAllCards === 'function') {
            clearAllCards();
        }
        const dashboard = document.getElementById('dashboard');
        if (dashboard) {
            const outers = dashboard.querySelectorAll('.card-outer');
            outers.forEach(outer => outer.remove());
        }
    }

    const dashboard = document.getElementById('dashboard');
    const existingCards = dashboard ? Array.from(dashboard.querySelectorAll('.card-outer')) : [];
    let currentExistingIndex = 0;

    if (!parsedResult || !parsedResult.items) return;

    parsedResult.items.forEach((item, index) => {
        let cardOuter;

        if (settings.createNewCards) {
            const boxId = `item_${Date.now()}_${index}`;
            if (typeof addNewCard === 'function') {
                addNewCard(boxId);
            }
            if (dashboard) {
                const outers = dashboard.querySelectorAll('.card-outer');
                cardOuter = outers[outers.length - 1];
            }
        } else {
            if (currentExistingIndex < existingCards.length) {
                cardOuter = existingCards[currentExistingIndex++];
            } else {
                return; // Keine Karten mehr vorhanden
            }
        }

        if (cardOuter) {
            const z1Input = cardOuter.querySelector('.z1');
            const z2Input = cardOuter.querySelector('.z2');
            const z3Input = cardOuter.querySelector('.z3');

            if (z1Input) { z1Input.value = item.zeile1 || ''; z1Input.dispatchEvent(new Event('input', { bubbles: true })); }
            if (z2Input) { z2Input.value = item.zeile2 || ''; z2Input.dispatchEvent(new Event('input', { bubbles: true })); }
            if (z3Input) { z3Input.value = item.zeile3 || ''; z3Input.dispatchEvent(new Event('input', { bubbles: true })); }
        }
    });
}

/**
 * Öffnet das AI Import Modal und lädt Einstellungen.
 */
async function openAIImportModal() {
    const modal = document.getElementById('ai-import-modal');
    if (modal) {
        modal.hidden = false;
    }

    const savedApiKey = localStorage.getItem('openrouter_api_key');
    const keyInput = document.getElementById('ai-api-key');
    if (keyInput) {
        if (savedApiKey) {
            keyInput.value = savedApiKey;
        } else {
            // Versuchen, aus lokaler api.env Datei vorzubefüllen
            try {
                const envResp = await fetch('/api.env');
                if (envResp.ok) {
                    const envText = await envResp.text();
                    const match = envText.match(/sk-or-[a-zA-Z0-9_-]+/);
                    if (match) {
                        keyInput.value = match[0];
                        localStorage.setItem('openrouter_api_key', match[0]);
                    }
                }
            } catch (e) {
                // api.env nicht erreichbar / nicht vorhanden
            }
        }
    }

    const createNewCardsInput = document.getElementById('ai-create-new-cards');
    const replaceExistingInput = document.getElementById('ai-replace-existing');

    if (createNewCardsInput) {
        createNewCardsInput.checked = localStorage.getItem('ai-create-new-cards') !== 'false';
    }
    if (replaceExistingInput) {
        replaceExistingInput.checked = localStorage.getItem('ai-replace-existing') !== 'false';
    }

    // System Prompt laden
    const systemPromptInput = document.getElementById('ai-system-prompt');
    if (systemPromptInput) {
        const savedPrompt = localStorage.getItem('ai_system_prompt');
        systemPromptInput.value = savedPrompt || DEFAULT_SYSTEM_PROMPT;
    }

    resetProgress();
}

/**
 * Schließt das AI Import Modal und speichert Einstellungen.
 */
function closeAIImportModal() {
    const modal = document.getElementById('ai-import-modal');
    if (modal) {
        modal.hidden = true;
    }

    const keyInput = document.getElementById('ai-api-key');
    if (keyInput) {
        localStorage.setItem('openrouter_api_key', keyInput.value.trim());
    }

    const createNewCardsInput = document.getElementById('ai-create-new-cards');
    if (createNewCardsInput) {
        localStorage.setItem('ai-create-new-cards', createNewCardsInput.checked);
    }

    const replaceExistingInput = document.getElementById('ai-replace-existing');
    if (replaceExistingInput) {
        localStorage.setItem('ai-replace-existing', replaceExistingInput.checked);
    }

    // System Prompt speichern
    const systemPromptInput = document.getElementById('ai-system-prompt');
    if (systemPromptInput) {
        localStorage.setItem('ai_system_prompt', systemPromptInput.value);
    }
}

/**
 * Schaltet die Sichtbarkeit des API-Schlüssels um.
 */
function toggleAIKeyVisibility() {
    const keyInput = document.getElementById('ai-api-key');
    const toggleBtn = document.getElementById('btn-toggle-ai-key');

    if (keyInput && toggleBtn) {
        if (keyInput.type === 'password') {
            keyInput.type = 'text';
            toggleBtn.textContent = '🙈';
        } else {
            keyInput.type = 'password';
            toggleBtn.textContent = '👁';
        }
    }
}

/**
 * Aktualisiert den Status eines Schritts in der UI.
 * @param {string} stepId Die ID des Schritt-Elements.
 * @param {string} status 'active', 'done', oder 'error'.
 */
function updateStepStatus(stepId, status) {
    const stepEl = document.getElementById(stepId);
    if (!stepEl) return;

    stepEl.classList.remove('active', 'done', 'error');
    stepEl.classList.add(status);

    const iconSpan = stepEl.querySelector('.step-icon');
    if (iconSpan) {
        if (status === 'active') iconSpan.textContent = '⏳';
        else if (status === 'done') iconSpan.textContent = '✅';
        else if (status === 'error') iconSpan.textContent = '❌';
        else iconSpan.textContent = '⚪';
    }
}

/**
 * Setzt den Live-Progress-Text (LLM/Modell-Versuche) anzeigebar.
 * @param {string} text
 */
function setLLMProgress(text) {
    const el = document.getElementById('ai-llm-progress');
    if (el) { el.textContent = text || ''; el.hidden = !text; }
}

/**
 * Setzt den Fortschritt und die UI zurück.
 */
function resetProgress() {
    ['ai-step-extract', 'ai-step-llm', 'ai-step-apply'].forEach(stepId => {
        const stepEl = document.getElementById(stepId);
        if (stepEl) {
            stepEl.classList.remove('active', 'done', 'error');
            const iconSpan = stepEl.querySelector('.step-icon');
            if (iconSpan) iconSpan.textContent = '⚪';
        }
    });

    const progressSection = document.getElementById('ai-progress-section');
    const resultInfo = document.getElementById('ai-result-info');
    const rawDetails = document.getElementById('ai-raw-details');
    const rawOutput = document.getElementById('ai-raw-output');

    if (progressSection) progressSection.hidden = false;
    if (resultInfo) resultInfo.hidden = true;
    if (rawDetails) rawDetails.hidden = true;
    if (rawOutput) rawOutput.textContent = '';
    setLLMProgress('');
}

/**
 * Verarbeitet die hochgeladene Datei (PDF oder Word), extrahiert den Text/Markdown
 * und ruft das LLM auf (mit lokalem Offline-Fallback).
 * @param {File} file Die ausgewählte Datei (PDF oder DOCX).
 */
async function handleUploadedFile(file) {
    if (!file) return;
    const fileName = file.name || '';
    const isPdf = file.type === 'application/pdf' || fileName.toLowerCase().endsWith('.pdf');
    const isDocx = fileName.toLowerCase().endsWith('.docx') || (file.type && file.type.includes('word'));

    if (!isPdf && !isDocx) {
        if (typeof showToast === 'function') showToast('error', 'Bitte eine PDF- oder Word-Datei (.docx) hochladen');
        return;
    }

    const keyInput = document.getElementById('ai-api-key');
    const apiKey = keyInput ? keyInput.value.trim() : '';

    if (!apiKey) {
        if (typeof showToast === 'function') showToast('error', 'OpenRouter API-Schlüssel fehlt');
        return;
    }

    resetProgress();
    let extractedText = '';

    try {
        // Step 1: Extract text / markdown
        updateStepStatus('ai-step-extract', 'active');
        extractedText = await extractDocumentContent(file);

        if (!extractedText.trim()) {
            throw new Error('Kein Text im Dokument gefunden.');
        }
        updateStepStatus('ai-step-extract', 'done');

        // Step 2: Load song reference + call LLM (mit lokalem Fallback)
        updateStepStatus('ai-step-llm', 'active');
        const songRef = await loadSongReference();

        let parsed, modelUsed, usedLocal = false;
        try {
            ({ parsed, modelUsed } = await callOpenRouter(apiKey, extractedText, songRef, setLLMProgress));
        } catch (llmErr) {
            console.warn('[AI Import] LLM-Ablauf fehlgeschlagen, versuche lokalen Parser:', llmErr.message);
            setLLMProgress('Free-Tier nicht erreichbar – lokal analysiere...');
            const localItems = localParseLiedplan(extractedText);
            if (localItems && localItems.length) {
                parsed = { items: localItems };
                modelUsed = 'lokal (Offline-Fallback)';
                usedLocal = true;
            } else {
                throw llmErr;
            }
        }
        updateStepStatus('ai-step-llm', 'done');

        // Step 3: Apply to cards
        updateStepStatus('ai-step-apply', 'active');
        const createNewCardsInput = document.getElementById('ai-create-new-cards');
        const replaceExistingInput = document.getElementById('ai-replace-existing');

        const settings = {
            createNewCards: createNewCardsInput ? createNewCardsInput.checked : true,
            replaceExisting: replaceExistingInput ? replaceExistingInput.checked : true
        };

        applyLLMResultToCards(parsed, settings);
        updateStepStatus('ai-step-apply', 'done');

        // Show results
        const resultInfo = document.getElementById('ai-result-info');
        const resultMsg = document.getElementById('ai-result-message');
        const rawDetails = document.getElementById('ai-raw-details');
        const rawOutput = document.getElementById('ai-raw-output');
        const count = (parsed && parsed.items) ? parsed.items.length : 0;

        if (resultInfo) resultInfo.hidden = false;
        if (rawDetails) rawDetails.hidden = false;
        if (rawOutput) rawOutput.textContent = JSON.stringify(parsed, null, 2);

        if (usedLocal) {
            if (typeof showToast === 'function') showToast('info', `Free-Tier belegt – lokale Analyse: ${count} Einträge`);
            if (resultMsg) resultMsg.textContent =
                `Lokale Analyse (Offline-Fallback): ${count} Einträge (Modell: ${modelUsed}). ` +
                `Das OpenRouter Free-Tier ist gerade nicht erreichbar – später erneut versuchen für das AI-Ergebnis.`;
        } else {
            if (typeof showToast === 'function') showToast('success', 'Liedplan erfolgreich verarbeitet!');
            if (resultMsg) resultMsg.textContent = `Erfolgreich geparst! ${count} Einträge erstellt. Modell: ${modelUsed}`;
        }

    } catch (error) {
        console.error('[AI Import] Fehler im Hauptablauf:', error);
        if (typeof showToast === 'function') showToast('error', error.message || 'Fehler bei der Dokument-Verarbeitung');

        // Find which step is active and mark as error
        ['ai-step-extract', 'ai-step-llm', 'ai-step-apply'].forEach(stepId => {
            const stepEl = document.getElementById(stepId);
            if (stepEl && stepEl.classList.contains('active')) {
                updateStepStatus(stepId, 'error');
            }
        });

        // Show raw output if we had extracted text (e.g. parse/LLM failure)
        if (extractedText && extractedText.trim()) {
            const resultInfo = document.getElementById('ai-result-info');
            const rawDetails = document.getElementById('ai-raw-details');
            const rawOutput = document.getElementById('ai-raw-output');
            if (resultInfo) resultInfo.hidden = false;
            if (rawDetails) rawDetails.hidden = false;
            if (rawOutput) {
                rawOutput.textContent = `Rohdaten/Fehler:\n${error.message}\n\nExtrahierter Text:\n${extractedText}`;
            }
        }
    }
}

// Rückwärtskompatibler Alias
const handlePDFFile = handleUploadedFile;

/**
 * Initialisiert die Dropzone und File-Input Listener.
 */
function initPDFDropzone() {
    const dropzone = document.getElementById('pdf-dropzone');
    const fileInput = document.getElementById('pdf-file-input');

    if (!dropzone || !fileInput) {
        console.log('[AI Import] Dropzone oder File Input nicht gefunden.');
        return;
    }

    // Check to prevent double initialization
    if (dropzone.dataset.initialized) return;
    dropzone.dataset.initialized = 'true';

    dropzone.addEventListener('click', () => fileInput.click());

    fileInput.addEventListener('change', (e) => {
        if (e.target.files && e.target.files[0]) {
            handleUploadedFile(e.target.files[0]);
        }
        fileInput.value = ''; // Reset so same file can be re-selected
    });

    dropzone.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
    });

    dropzone.addEventListener('dragleave', () => {
        dropzone.classList.remove('dragover');
    });

    dropzone.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');

        if (e.dataTransfer.files && e.dataTransfer.files[0]) {
            const file = e.dataTransfer.files[0];
            const fileName = file.name || '';
            const isPdf = file.type === 'application/pdf' || fileName.toLowerCase().endsWith('.pdf');
            const isDocx = fileName.toLowerCase().endsWith('.docx') || (file.type && file.type.includes('word'));

            if (isPdf || isDocx) {
                handleUploadedFile(file);
            } else {
                if (typeof showToast === 'function') {
                    showToast('error', 'Bitte eine PDF- oder Word-Datei (.docx) hochladen');
                }
            }
        }
    });
    console.log('[AI Import] Dokument-Dropzone initialisiert.');
}

document.addEventListener('DOMContentLoaded', initPDFDropzone);
if (document.readyState !== 'loading') {
    initPDFDropzone();
}
