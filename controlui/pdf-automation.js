/**
 * @file pdf-automation.js
 * @description PDF-Automatisierungsmodul für das OBS Studio Custom Dock.
 */

let cachedSongReference = null;

const DEFAULT_SYSTEM_PROMPT = `Du bist ein Parser für Gottesdienstprogramme.
Du erhältst extrahierten Text aus einem PDF und eine Gotteslob-Liedreferenz (Nummer: Titel).
Identifiziere alle Programmpunkte (Lieder, Lesungen, Predigt, Gebete, etc.).
Für Lieder: Schlage den vollen Titel aus der Referenz anhand der Nummer nach, falls vorhanden.
Gib AUSSCHLIESSLICH gültiges JSON zurück im folgenden Format:
{ "items": [{ "zeile1": "...", "zeile2": "...", "zeile3": "..." }] }
- zeile1 = Titel/Name des Programmpunkts
- zeile2 = GL-Nummer/Details
- zeile3 = optionale Zusatzinformationen`.trim();

/**
 * Gibt den aktuellen System Prompt zurück (aus Textarea oder Default).
 */
function getSystemPrompt() {
    const textarea = document.getElementById('ai-system-prompt');
    const value = textarea ? textarea.value.trim() : '';
    return value || DEFAULT_SYSTEM_PROMPT;
}

/**
 * Setzt den System Prompt im Textarea auf den Standardwert zurück.
 */
function resetSystemPrompt() {
    const textarea = document.getElementById('ai-system-prompt');
    if (textarea) {
        textarea.value = DEFAULT_SYSTEM_PROMPT;
        localStorage.setItem('ai_system_prompt', DEFAULT_SYSTEM_PROMPT);
    }
    if (typeof showToast === 'function') showToast('info', 'System Prompt auf Standard zurückgesetzt');
}

/**
 * Lädt die Gotteslob-Referenz aus lieder.md.
 * @returns {Promise<Object>} Ein Objekt mit Liednummern als Schlüssel und Titeln als Wert.
 */
async function loadSongReference() {
    if (cachedSongReference) return cachedSongReference;
    
    try {
        const response = await fetch('lieder.md');
        if (!response.ok) throw new Error('lieder.md konnte nicht geladen werden.');
        const text = await response.text();
        
        const lines = text.split('\n');
        const songRef = {};
        
        // Skip first two lines (header + separator)
        for (let i = 2; i < lines.length; i++) {
            const line = lines[i].trim();
            if (!line) continue;
            
            const parts = line.split('|').map(p => p.trim());
            // | Nr | Titel | altes GL | Rubrik | Unterrubrik | ökumenisch |
            if (parts.length >= 3) {
                const number = parts[1]; // Index 1 because string starts with '|'
                const rawTitle = parts[2] || '';
                
                // Extract type marker from title end e.g. (L), (G), (Kv), (Kan), (R), (Res)
                const typeMatch = rawTitle.match(/\s*\(([A-Za-z]+)\)\s*$/);
                const type = typeMatch ? typeMatch[1] : '';
                const title = rawTitle.replace(/\s*\([A-Za-z]+\)\s*$/, '').trim();
                
                // parts[3] = altes GL, parts[4] = Rubrik, parts[5] = Unterrubrik
                const rubrik = parts[4] || '';
                
                if (number) {
                    songRef[number] = { title, type, rubrik };
                }
            }
        }
        
        cachedSongReference = songRef;
        return songRef;
    } catch (error) {
        console.error('[AI Import] Fehler beim Laden der Liedreferenz:', error);
        throw error;
    }
}

/**
 * Extrahiert den Text aus einer hochgeladenen PDF-Datei.
 * @param {File} file Die PDF-Datei.
 * @returns {Promise<string>} Der extrahierte Text.
 */
async function extractTextFromPDF(file) {
    try {
        const arrayBuffer = await file.arrayBuffer();
        const pdf = await pdfjsLib.getDocument(arrayBuffer).promise;
        const numPages = pdf.numPages;
        let fullText = '';
        
        for (let i = 1; i <= numPages; i++) {
            const page = await pdf.getPage(i);
            const textContent = await page.getTextContent();
            const pageText = textContent.items.map(item => item.str).join(' ');
            fullText += pageText + '\n';
        }
        
        return fullText;
    } catch (error) {
        console.error('[AI Import] Fehler bei der PDF-Textextraktion:', error);
        throw error;
    }
}

const FREE_MODELS = [
    'meta-llama/llama-3.3-70b-instruct:free',
    'openrouter/free'
];

/**
 * Ruft die OpenRouter API auf, um den extrahierten Text zu parsen.
 * @param {string} apiKey Der OpenRouter API-Schlüssel.
 * @param {string} extractedText Der aus der PDF extrahierte Text.
 * @param {Object} songReference Das Objekt mit den Liedreferenzen.
 * @returns {Promise<{parsed: Object, modelUsed: string}>} Das geparste Ergebnis und das verwendete Modell.
 */
async function callOpenRouter(apiKey, extractedText, songReference) {
    // Compact representation of song references to save tokens
    const compactSongRef = {};
    for (const [num, data] of Object.entries(songReference)) {
        compactSongRef[num] = data.title;
    }
    
    const systemPrompt = getSystemPrompt();

    const userPrompt = `
Liedreferenz: ${JSON.stringify(compactSongRef)}
PDF Text: ${extractedText}
`.trim();

    console.log('[AI Import] Sende Anfrage an OpenRouter (Free-Tier Modelle)...');
    
    const response = await fetch('https://openrouter.ai/api/v1/chat/completions', {
        method: 'POST',
        headers: {
            'Authorization': `Bearer ${apiKey}`,
            'Content-Type': 'application/json'
        },
        body: JSON.stringify({
            models: FREE_MODELS,
            messages: [
                { role: 'system', content: systemPrompt },
                { role: 'user', content: userPrompt }
            ],
            temperature: 0.2,
            max_tokens: 4000,
            response_format: { type: 'json_object' }
        })
    });
    
    if (!response.ok) {
        const errData = await response.json().catch(() => ({}));
        const errMsg = errData.error?.message || `HTTP ${response.status}`;
        throw new Error(`OpenRouter API Fehler: ${errMsg}`);
    }
    
    const data = await response.json();
    const content = data.choices[0].message.content;
    const modelUsed = data.model || 'unbekannt';
    
    console.log(`[AI Import] Erfolgreich. Verwendetes Modell: ${modelUsed}`);
    
    let parsed = {}; try { parsed = JSON.parse(content.replace(/^```json\s*|\s*```$/g, "")); } catch (e) { console.error("JSON Parse Error:", e, content); }
    return { parsed, modelUsed };
}

/**
 * Überträgt das LLM-Ergebnis in die UI-Karten.
 * @param {Object} parsedResult Das von der LLM geparste JSON-Ergebnis.
 * @param {Object} settings Die Einstellungen ({ createNewCards: boolean, replaceExisting: boolean }).
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
function openAIImportModal() {
    const modal = document.getElementById('ai-import-modal');
    if (modal) {
        modal.hidden = false;
    }
    
    const savedApiKey = localStorage.getItem('openrouter_api_key');
    const keyInput = document.getElementById('ai-api-key');
    if (keyInput && savedApiKey) {
        keyInput.value = savedApiKey;
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
}

/**
 * Verarbeitet die hochgeladene PDF-Datei, extrahiert den Text und ruft die LLM auf.
 * @param {File} file Die ausgewählte PDF-Datei.
 */
async function handlePDFFile(file) {
    if (!file || file.type !== 'application/pdf') {
        if (typeof showToast === 'function') showToast('error', 'Bitte eine PDF-Datei hochladen');
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
        // Step 1: Extract text
        updateStepStatus('ai-step-extract', 'active');
        extractedText = await extractTextFromPDF(file);
        
        if (!extractedText.trim()) {
            throw new Error('Kein Text in der PDF gefunden.');
        }
        updateStepStatus('ai-step-extract', 'done');
        
        // Step 2: Load song reference + call LLM
        updateStepStatus('ai-step-llm', 'active');
        const songRef = await loadSongReference();
        const { parsed, modelUsed } = await callOpenRouter(apiKey, extractedText, songRef);
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
        
        // Show success results
        if (typeof showToast === 'function') showToast('success', 'PDF erfolgreich verarbeitet!');
        
        const resultInfo = document.getElementById('ai-result-info');
        const resultMsg = document.getElementById('ai-result-message');
        const rawDetails = document.getElementById('ai-raw-details');
        const rawOutput = document.getElementById('ai-raw-output');
        
        if (resultInfo) resultInfo.hidden = false;
        if (resultMsg) {
            const count = (parsed && parsed.items) ? parsed.items.length : 0;
            resultMsg.textContent = `Erfolgreich geparst! ${count} Einträge erstellt. Modell: ${modelUsed}`;
        }
        if (rawDetails) rawDetails.hidden = false;
        if (rawOutput) {
            rawOutput.textContent = JSON.stringify(parsed, null, 2);
        }
        
    } catch (error) {
        console.error('[AI Import] Fehler im Hauptablauf:', error);
        if (typeof showToast === 'function') showToast('error', error.message || 'Fehler bei der PDF-Verarbeitung');
        
        // Find which step is active and mark as error
        ['ai-step-extract', 'ai-step-llm', 'ai-step-apply'].forEach(stepId => {
            const stepEl = document.getElementById(stepId);
            if (stepEl && stepEl.classList.contains('active')) {
                updateStepStatus(stepId, 'error');
            }
        });
        
        // Show raw output if it was a parse error or similar after extraction
        if (error.name === 'SyntaxError' || (extractedText && extractedText.trim())) {
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
            handlePDFFile(e.target.files[0]);
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
            if (file.type === 'application/pdf') {
                handlePDFFile(file);
            } else {
                if (typeof showToast === 'function') {
                    showToast('error', 'Bitte eine PDF-Datei hochladen');
                }
            }
        }
    });
    console.log('[AI Import] PDF Dropzone initialisiert.');
}

document.addEventListener('DOMContentLoaded', initPDFDropzone);
if (document.readyState !== 'loading') {
    initPDFDropzone();
}
