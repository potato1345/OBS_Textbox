/**
 * @file modals.js
 * Verwaltung der nativen <dialog>-Modale für Einstellungen, Schnell-Profile,
 * AI-Import und Google-Forms-Import.
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Zusammenspiel:
 *   - Steuert Anzeige und Events aller Dialogfenster im Dashboard.
 *   - Verwaltet Einstellungen (OBS WebSocket Passwort, UI-Farbthemen, Auto-Send).
 *   - Behandelt den mehrstufigen Import-Workflow (AI-Extraktion, Dateiupload, Forms-API).
 */

import { getState, updateState } from './storage.js';
import {
    DEFAULT_COLOR_PROFILES,
    COLOR_KEYS,
    applyThemeVariables,
    colorProfileToYAML,
    yamlToColorProfile,
    isDefaultColorProfile
} from './color-profiles.js';
import { connectOBS, disconnectOBS, toggleConnection, obs, pushStateToOBS } from './obs-sync.js';
import { showToast } from './toast.js';
import {
    extractDocumentText,
    processWithLLM,
    parseLiedplanLocally,
    loadSongReference,
    enrichTitlesFromReference,
    DEFAULT_SYSTEM_PROMPT
} from './pdf-ai-importer.js';
import { fetchAllFormsServices, uploadAnswersFile, serviceToItems, applyItemsToDashboard } from './forms-importer.js';
import { LP_DEBUG, escapeHtml } from './box-utils.js';
import { renderDashboardCards } from './cards.js';

/**
 * Aktiviert das Schließen des Dialogs bei Klick auf den Hintergrund (Backdrop-Klick).
 * Bietet Fallback für Browser ohne Unterstützung des nativen closedby="any".
 *
 * @param {HTMLDialogElement|null} dialog - Das Dialog-Element.
 */
export function setupModalDismiss(dialog) {
    if (!dialog) return;

    // Fallback für Browser ohne natives closedby="any"
    if (!('closedBy' in HTMLDialogElement.prototype)) {
        dialog.addEventListener('click', (event) => {
            if (event.target !== dialog) return;
            const rect = dialog.getBoundingClientRect();
            const isInDialog = (
                rect.top <= event.clientY &&
                event.clientY <= rect.top + rect.height &&
                rect.left <= event.clientX &&
                event.clientX <= rect.left + rect.width
            );
            if (!isInDialog) {
                dialog.close();
            }
        });
    }
}

/**
 * Öffnet einen modalen Dialog über dessen ID.
 *
 * @param {string} dialogId - HTML-ID des <dialog>-Elements.
 */
export function openDialog(dialogId) {
    if (LP_DEBUG) console.debug('[liedplan] Öffne Dialog:', dialogId);
    const dialog = document.getElementById(dialogId);
    if (!dialog) return;
    if (typeof dialog.showModal === 'function') {
        dialog.showModal();
    } else {
        dialog.setAttribute('open', '');
    }
}

/**
 * Schließt einen modalen Dialog über dessen ID.
 *
 * @param {string} dialogId - HTML-ID des <dialog>-Elements.
 */
export function closeDialog(dialogId) {
    if (LP_DEBUG) console.debug('[liedplan] Schließe Dialog:', dialogId);
    const dialog = document.getElementById(dialogId);
    if (!dialog) return;
    if (typeof dialog.close === 'function') {
        dialog.close();
    } else {
        dialog.removeAttribute('open');
    }
}

// -------------------------------------------------------------
// Einstellungen & Farbprofile
// -------------------------------------------------------------

/**
 * Initialisiert das Einstellungs-Modal, inkl. Passwort-Umschaltung, Auto-Send-Option,
 * OBS-Verbindungsbutton und Verwaltung von Farbprofilen (Speichern, Löschen, Export, Import).
 */
export function initSettingsModal() {
    const dialog = document.getElementById('settings-dialog');
    setupModalDismiss(dialog);

    const pwInput = document.getElementById('ws-password');
    const autoSendCheckbox = document.getElementById('setting-auto-send');
    const btnTogglePw = document.getElementById('btn-toggle-ws-pw');
    const btnConnect = document.getElementById('btn-settings-connect');

    // Passwort Sichtbarkeit
    btnTogglePw?.addEventListener('click', () => {
        const isSecret = pwInput.type === 'password';
        pwInput.type = isSecret ? 'text' : 'password';
        btnTogglePw.textContent = isSecret ? '🙈' : '👁';
    });

    // Auto-Send Checkbox
    autoSendCheckbox?.addEventListener('change', () => {
        updateState(s => {
            s.autoSend = autoSendCheckbox.checked;
        }, 'setting_autosend');
    });

    // Verbindungsbutton
    btnConnect?.addEventListener('click', () => {
        const pw = pwInput.value.trim();
        updateState(s => {
            s.settings.obsPassword = pw;
        }, 'setting_pw');
        toggleConnection();
    });

    // Farbprofil Select und Aktionen
    const profileSelect = document.getElementById('color-profile-select');
    const btnSaveForm = document.getElementById('btn-show-save-color-profile');
    const saveInlineBox = document.getElementById('color-profile-save-box');
    const inputProfileName = document.getElementById('input-color-profile-name');
    const btnConfirmSave = document.getElementById('btn-confirm-save-profile');
    const btnCancelSave = document.getElementById('btn-cancel-save-profile');
    const btnExport = document.getElementById('btn-export-color-profile');
    const btnImport = document.getElementById('btn-import-color-profile');
    const btnDelete = document.getElementById('btn-delete-color-profile');

    profileSelect?.addEventListener('change', () => {
        onProfileSelectChange(profileSelect.value);
    });

    btnSaveForm?.addEventListener('click', () => {
        saveInlineBox.hidden = !saveInlineBox.hidden;
        if (!saveInlineBox.hidden) {
            inputProfileName.value = '';
            inputProfileName.focus();
        }
    });

    btnCancelSave?.addEventListener('click', () => {
        saveInlineBox.hidden = true;
    });

    btnConfirmSave?.addEventListener('click', () => {
        const name = inputProfileName.value.trim();
        if (!name) {
            showToast('error', 'Bitte einen Profilnamen eingeben.');
            return;
        }
        if (isDefaultColorProfile(name)) {
            showToast('error', `"${name}" ist ein geschütztes Standard-Profil.`);
            return;
        }

        const state = getState();
        const colors = { ...state.settings.uiColors };

        updateState(s => {
            if (!s.settings.colorProfiles) s.settings.colorProfiles = [];
            const idx = s.settings.colorProfiles.findIndex(p => p.name === name);
            if (idx >= 0) {
                s.settings.colorProfiles[idx] = { name, colors };
            } else {
                s.settings.colorProfiles.push({ name, colors });
            }
            s.settings.activeColorProfile = name;
        }, 'save_color_profile');

        saveInlineBox.hidden = true;
        renderColorProfileSelect();
        showToast('success', `Farbprofil "${name}" gespeichert`);
    });

    btnDelete?.addEventListener('click', () => {
        const selected = profileSelect.value;
        if (isDefaultColorProfile(selected) || selected === '__custom__') return;

        updateState(s => {
            s.settings.colorProfiles = (s.settings.colorProfiles || []).filter(p => p.name !== selected);
            s.settings.activeColorProfile = null;
        }, 'delete_color_profile');

        renderColorProfileSelect();
        showToast('info', `Farbprofil "${selected}" gelöscht`);
    });

    btnExport?.addEventListener('click', () => {
        const state = getState();
        const activeName = profileSelect.value;
        let profile;

        if (activeName === '__custom__') {
            profile = { name: 'Benutzerdefiniert', colors: state.settings.uiColors };
        } else {
            const all = [...DEFAULT_COLOR_PROFILES, ...(state.settings.colorProfiles || [])];
            profile = all.find(p => p.name === activeName);
        }

        if (!profile) return;
        const yaml = colorProfileToYAML(profile);
        const blob = new Blob([yaml], { type: 'text/yaml;charset=utf-8' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = `farbprofil_${profile.name.replace(/[^a-zA-Z0-9_-]/g, '_')}.yaml`;
        a.click();
        URL.revokeObjectURL(url);
        showToast('success', `Farbprofil "${profile.name}" exportiert`);
    });

    btnImport?.addEventListener('click', () => {
        const fileInput = document.createElement('input');
        fileInput.type = 'file';
        fileInput.accept = '.yaml,.yml';
        fileInput.onchange = async () => {
            const file = fileInput.files?.[0];
            if (!file) return;
            try {
                const text = await file.text();
                const profile = yamlToColorProfile(text);
                if (!profile.name || !profile.colors || Object.keys(profile.colors).length === 0) {
                    throw new Error('Datei enthält kein gültiges Farbprofil.');
                }
                if (isDefaultColorProfile(profile.name)) {
                    profile.name += ' (Import)';
                }

                updateState(s => {
                    if (!s.settings.colorProfiles) s.settings.colorProfiles = [];
                    const idx = s.settings.colorProfiles.findIndex(p => p.name === profile.name);
                    if (idx >= 0) s.settings.colorProfiles[idx] = profile;
                    else s.settings.colorProfiles.push(profile);
                    s.settings.activeColorProfile = profile.name;
                    s.settings.uiColors = { ...profile.colors };
                }, 'import_color_profile');

                applyThemeVariables(profile.colors);
                loadSettingsFields();
                renderColorProfileSelect();
                showToast('success', `Profil "${profile.name}" importiert`);
            } catch (err) {
                showToast('error', `Import-Fehler: ${err.message}`);
            }
        };
        fileInput.click();
    });

    // Color Pickers im Grid
    bindThemeColorPickers();
}

/**
 * Lädt die aktuellen Konfigurationswerte aus dem State in die Eingabefelder des Einstellungs-Modals.
 */
export function loadSettingsFields() {
    const state = getState();
    const pwInput = document.getElementById('ws-password');
    const autoSendCheckbox = document.getElementById('setting-auto-send');
    if (pwInput) pwInput.value = state.settings.obsPassword || '';
    if (autoSendCheckbox) autoSendCheckbox.checked = !!state.autoSend;

    const colors = state.settings.uiColors || {};
    for (const key of COLOR_KEYS) {
        const el = document.getElementById(`theme-${key}`);
        if (el) {
            el.value = colors[key] !== undefined ? colors[key] : (key.endsWith('Opacity') ? 255 : '#000000');
        }
    }

    renderColorProfileSelect();
    updateConnectionStatusLabel(obs.isConnected);
}

/**
 * Aktualisiert die Statusanzeige und Beschriftung des Verbindungsbuttons im Modal.
 *
 * @param {boolean} connected - Verbindungsstatus zu OBS WebSocket.
 */
export function updateConnectionStatusLabel(connected) {
    const lbl = document.getElementById('settings-connection-status');
    const btn = document.getElementById('btn-settings-connect');
    if (lbl) {
        lbl.textContent = connected ? 'Status: Mit OBS verbunden' : 'Status: Nicht verbunden';
        lbl.className = connected ? 'status-text status-online' : 'status-text status-offline';
    }
    if (btn) {
        btn.textContent = connected ? 'Trennen' : 'Mit OBS verbinden';
    }
}

/**
 * Registriert Event-Listener auf allen Theme-Farbwählern zur Live-Vorschau und Zustandsspeicherung.
 */
function bindThemeColorPickers() {
    for (const key of COLOR_KEYS) {
        const el = document.getElementById(`theme-${key}`);
        if (!el) continue;

        el.addEventListener('input', () => {
            const state = getState();
            const colors = state.settings.uiColors || {};
            const val = key.endsWith('Opacity') ? Number(el.value) : el.value;
            colors[key] = val;

            applyThemeVariables(colors);
            updateState(s => {
                s.settings.uiColors = colors;
                s.settings.activeColorProfile = null; // Auf Custom umschalten
            }, 'color_adjust');

            const select = document.getElementById('color-profile-select');
            if (select) select.value = '__custom__';
            updateDeleteButtonState();
        });
    }
}

/**
 * Baut das Dropdown-Menü für Farbprofile (Standard- und benutzerdefinierte Themes) neu auf.
 */
export function renderColorProfileSelect() {
    const select = document.getElementById('color-profile-select');
    if (!select) return;

    const state = getState();
    const active = state.settings.activeColorProfile;

    select.replaceChildren();

    const customOpt = document.createElement('option');
    customOpt.value = '__custom__';
    customOpt.textContent = '✎ Benutzerdefiniert';
    select.appendChild(customOpt);

    const defaultGroup = document.createElement('optgroup');
    defaultGroup.label = '📦 Standard-Profile';
    DEFAULT_COLOR_PROFILES.forEach(p => {
        const opt = document.createElement('option');
        opt.value = p.name;
        opt.textContent = p.name;
        if (active === p.name) opt.selected = true;
        defaultGroup.appendChild(opt);
    });
    select.appendChild(defaultGroup);

    const customProfiles = state.settings.colorProfiles || [];
    if (customProfiles.length > 0) {
        const customGroup = document.createElement('optgroup');
        customGroup.label = '🎨 Eigene Profile';
        customProfiles.forEach(p => {
            const opt = document.createElement('option');
            opt.value = p.name;
            opt.textContent = p.name;
            if (active === p.name) opt.selected = true;
            customGroup.appendChild(opt);
        });
        select.appendChild(customGroup);
    }

    if (!active) {
        customOpt.selected = true;
    }

    updateDeleteButtonState();
}

/**
 * Behandelt die Auswahl eines Farbprofils im Einstellungsmenü.
 *
 * @param {string} name - Name des gewählten Profils oder '__custom__'.
 */
function onProfileSelectChange(name) {
    if (name === '__custom__') {
        updateState(s => { s.settings.activeColorProfile = null; }, 'set_custom_profile');
        updateDeleteButtonState();
        return;
    }

    const state = getState();
    const all = [...DEFAULT_COLOR_PROFILES, ...(state.settings.colorProfiles || [])];
    const profile = all.find(p => p.name === name);
    if (!profile) return;

    updateState(s => {
        s.settings.activeColorProfile = name;
        s.settings.uiColors = { ...profile.colors };
    }, 'apply_profile');

    applyThemeVariables(profile.colors);
    loadSettingsFields();
    showToast('info', `Profil "${name}" angewendet`);
}

/**
 * Aktiviert oder deaktiviert den Profil-Löschen-Button abhängig vom Schutzstatus des gewählten Profils.
 */
function updateDeleteButtonState() {
    const select = document.getElementById('color-profile-select');
    const deleteBtn = document.getElementById('btn-delete-color-profile');
    if (!select || !deleteBtn) return;

    const val = select.value;
    const isProtected = isDefaultColorProfile(val) || val === '__custom__';
    deleteBtn.disabled = isProtected;
}

// -------------------------------------------------------------
// Schnell-Profile (Box-Zustand)
// -------------------------------------------------------------

/**
 * Initialisiert das Dialogfenster zum Abspeichern des aktuellen Box-Zustands als Schnell-Profil.
 */
export function initSaveProfileModal() {
    const dialog = document.getElementById('save-profile-dialog');
    setupModalDismiss(dialog);

    const input = document.getElementById('input-quick-profile-name');
    const btnSave = document.getElementById('btn-confirm-save-quick-profile');
    const btnCancel = document.getElementById('btn-cancel-save-quick-profile');

    btnCancel?.addEventListener('click', () => dialog.close());

    btnSave?.addEventListener('click', () => {
        const name = input.value.trim();
        if (!name) {
            showToast('error', 'Bitte einen Namen für das Profil angeben.');
            return;
        }

        const state = getState();
        const boxesCopy = JSON.parse(JSON.stringify(state.boxes));

        updateState(s => {
            if (!s.profiles) s.profiles = [];
            const idx = s.profiles.findIndex(p => p.name === name);
            if (idx >= 0) {
                s.profiles[idx] = { name, boxes: boxesCopy };
            } else {
                s.profiles.push({ name, boxes: boxesCopy });
            }
        }, 'save_quick_profile');

        renderQuickProfilesBar();
        dialog.close();
        showToast('success', `Profil "${name}" gespeichert`);
    });
}

/**
 * Rendert die Leiste mit Schnellprofil-Chips in der oberen Navigationsleiste des Dashboards.
 */
export function renderQuickProfilesBar() {
    const container = document.getElementById('quick-profiles-list');
    if (!container) return;

    container.replaceChildren();
    const state = getState();

    (state.profiles || []).forEach((profile, idx) => {
        const chip = document.createElement('div');
        chip.className = 'profile-chip';

        const btnLoad = document.createElement('button');
        btnLoad.type = 'button';
        btnLoad.className = 'chip-load-btn';
        btnLoad.innerHTML = `<span class="chip-icon">📋</span> ${escapeHtml(profile.name)}`;
        btnLoad.addEventListener('click', () => {
            updateState(s => {
                s.boxes = JSON.parse(JSON.stringify(profile.boxes));
            }, 'load_quick_profile');
            const dashboard = document.getElementById('dashboard');
            renderDashboardCards(dashboard);
            if (getState().autoSend) pushStateToOBS();
            showToast('info', `Profil "${profile.name}" geladen`);
        });

        const btnDel = document.createElement('button');
        btnDel.type = 'button';
        btnDel.className = 'chip-delete-btn';
        btnDel.title = 'Profil löschen';
        btnDel.textContent = '✕';
        btnDel.addEventListener('click', (e) => {
            e.stopPropagation();
            updateState(s => {
                s.profiles.splice(idx, 1);
            }, 'delete_quick_profile');
            renderQuickProfilesBar();
            showToast('info', `Profil "${profile.name}" gelöscht`);
        });

        chip.appendChild(btnLoad);
        chip.appendChild(btnDel);
        container.appendChild(chip);
    });
}

// -------------------------------------------------------------
// Einheitlicher Import-Dialog (AI / Datei / Google Forms)
// -------------------------------------------------------------

const IMPORT_DIALOG_ID = 'import-dialog';
const IMPORT_TABS = ['ai', 'file', 'google'];

let importResponseList = []; // alle geladenen Einreichungen (für den Auswahl-Dropdown)
let importPending = null;     // { items, sourceLabel, count }

/**
 * Initialisiert den mehrteiligen Import-Dialog (AI-Analyse, Dateiupload, Google Forms API).
 * Bindet Tab-Umschaltung, Drag-and-Drop-Zonen, API-Schlüssel-Vorbelegung und Übernahme ins Dashboard.
 */
export function initImportModal() {
    const dialog = document.getElementById(IMPORT_DIALOG_ID);
    setupModalDismiss(dialog);

    const btnApply = document.getElementById('btn-apply-forms-data');
    const previewContainer = document.getElementById('forms-preview-container');
    const responsePick = document.getElementById('import-response-pick');
    const responseSelect = document.getElementById('import-response-select');

    // Bei jedem Öffnen: Zustand zurücksetzen
    dialog?.addEventListener('open', () => {
        importPending = null;
        importResponseList = [];
        if (previewContainer) { previewContainer.hidden = true; previewContainer.replaceChildren(); }
        if (responsePick) responsePick.hidden = true;
        if (btnApply) btnApply.disabled = true;
        switchImportTab('ai');
    });

    // Tab-Umschaltung
    IMPORT_TABS.forEach(tabId => {
        document.getElementById(`import-tab-${tabId}`)?.addEventListener('click', () => switchImportTab(tabId));
    });

    // AI-Panel: Key-Toggle, Prompt-Reset, Dropzone
    const apiKeyInput = document.getElementById('input-ai-api-key');
    const promptInput = document.getElementById('input-ai-system-prompt');
    const btnResetPrompt = document.getElementById('btn-reset-system-prompt');
    const btnToggleKey = document.getElementById('btn-toggle-ai-key');
    const aiDropzone = document.getElementById('ai-file-dropzone');
    const aiFileInput = document.getElementById('ai-file-input');

    // Auto-Prefill Key from Backend
    fetch('/api/openrouter-key')
        .then(r => r.json())
        .then(data => {
            if (data.success && data.key && apiKeyInput && !apiKeyInput.value) {
                apiKeyInput.value = data.key;
            }
        })
        .catch(() => {});

    if (promptInput && !promptInput.value) promptInput.value = DEFAULT_SYSTEM_PROMPT;

    btnToggleKey?.addEventListener('click', () => {
        const isSecret = apiKeyInput.type === 'password';
        apiKeyInput.type = isSecret ? 'text' : 'password';
        btnToggleKey.textContent = isSecret ? '🙈' : '👁';
    });

    btnResetPrompt?.addEventListener('click', () => {
        if (promptInput) promptInput.value = DEFAULT_SYSTEM_PROMPT;
        showToast('info', 'System-Prompt zurückgesetzt');
    });

    bindDropzone(aiDropzone, aiFileInput, file => handleAiUpload(file));

    // Datei-Panel: Dropzone
    const fileDropzone = document.getElementById('forms-file-dropzone');
    const fileInput = document.getElementById('forms-file-input');
    bindDropzone(fileDropzone, fileInput, file => handleFileImport(file));

    // Google-Panel: alle Einreichungen laden
    const btnFetchApi = document.getElementById('btn-fetch-forms-api');
    btnFetchApi?.addEventListener('click', async () => {
        btnFetchApi.disabled = true;
        const oldLabel = btnFetchApi.textContent;
        btnFetchApi.textContent = '⏳ Lade von Google...';
        try {
            const { services } = await fetchAllFormsServices();
            await loadServicesIntoImport(services, 'Google Forms');
            showToast('success', 'Einreichungen von Google geladen');
        } catch (err) {
            showToast('error', err.message);
        } finally {
            btnFetchApi.disabled = false;
            btnFetchApi.textContent = oldLabel;
        }
    });

    // Auswahl-Dropdown: andere Einreichung wählen
    responseSelect?.addEventListener('change', () => {
        const service = importResponseList[Number(responseSelect.value)];
        if (service) {
            showImportPreview(serviceToItems(service), formatResponseLabel(service), service);
        }
    });

    // Übernahme in das Dashboard
    btnApply?.addEventListener('click', () => {
        if (!importPending) return;
        try {
            const chkReplace = document.getElementById('forms-replace-existing');
            applyItemsToDashboard(importPending.items, {
                replaceExisting: chkReplace ? chkReplace.checked : true
            });
            const dashboard = document.getElementById('dashboard');
            renderDashboardCards(dashboard);
            dialog.close();
            showToast('success', `Import erfolgreich (${importPending.count} Programmpunkte)`);
        } catch (err) {
            showToast('error', err.message);
        }
    });
}

/**
 * Schaltet zwischen den Import-Reitern (AI, Datei, Google Forms) um.
 *
 * @param {string} tabId - ID des Reiters ('ai', 'file' oder 'google').
 */
function switchImportTab(tabId) {
    IMPORT_TABS.forEach(id => {
        document.getElementById(`import-tab-${id}`)?.classList.toggle('active', id === tabId);
        const panel = document.getElementById(`import-panel-${id}`);
        if (panel) panel.hidden = id !== tabId;
    });
}

/**
 * Bindet Klick- und Drag-and-Drop-Events an einen Upload-Bereich.
 *
 * @param {HTMLElement|null} dropzone - Der sichtbare Drop-Bereich.
 * @param {HTMLInputElement|null} fileInput - Das versteckte <input type="file">-Element.
 * @param {function(File): void} onFile - Callback bei Dateiauswahl.
 */
function bindDropzone(dropzone, fileInput, onFile) {
    dropzone?.addEventListener('click', () => fileInput?.click());
    fileInput?.addEventListener('change', () => {
        if (fileInput.files?.[0]) {
            onFile(fileInput.files[0]);
            fileInput.value = '';
        }
    });
    dropzone?.addEventListener('dragover', (e) => {
        e.preventDefault();
        dropzone.classList.add('dragover');
    });
    dropzone?.addEventListener('dragleave', () => dropzone.classList.remove('dragover'));
    dropzone?.addEventListener('drop', (e) => {
        e.preventDefault();
        dropzone.classList.remove('dragover');
        if (e.dataTransfer?.files?.[0]) onFile(e.dataTransfer.files[0]);
    });
}

/**
 * Verarbeitet eine per AI/OCR zu analysierende Liedplan-Datei (PDF, Bild, DOCX, etc.).
 *
 * @param {File} file - Die hochgeladene Liedplan-Datei.
 */
async function handleAiUpload(file) {
    const apiKey = document.getElementById('input-ai-api-key')?.value.trim();
    const systemPrompt = document.getElementById('input-ai-system-prompt')?.value.trim() || DEFAULT_SYSTEM_PROMPT;

    const progressBox = document.getElementById('ai-progress-box');
    const stepExtract = document.getElementById('ai-step-extract');
    const stepLlm = document.getElementById('ai-step-llm');
    const stepApply = document.getElementById('ai-step-apply');
    const liveLog = document.getElementById('ai-live-log');
    const resultBox = document.getElementById('ai-result-box');
    const rawPre = document.getElementById('ai-raw-output');

    if (progressBox) progressBox.hidden = false;
    if (resultBox) resultBox.hidden = true;
    if (liveLog) liveLog.textContent = 'Starte Extraktion...';

    const setStep = (el, status, text) => {
        if (!el) return;
        el.className = `progress-step ${status}`;
        const icon = status === 'done' ? '✅' : status === 'active' ? '⏳' : status === 'error' ? '❌' : '⚪';
        el.querySelector('.step-icon').textContent = icon;
        if (text) el.querySelector('.step-text').textContent = text;
    };

    setStep(stepExtract, 'active', 'Text aus Dokument extrahieren...');
    setStep(stepLlm, 'pending', 'AI / LLM Verarbeitung...');
    setStep(stepApply, 'pending', 'Karten im Dashboard befüllen...');

    try {
        const text = await extractDocumentText(file);
        if (!text || !text.trim()) {
            throw new Error('Kein Text im Dokument gefunden.');
        }
        setStep(stepExtract, 'done', `Text extrahiert (${text.length} Zeichen)`);

        setStep(stepLlm, 'active', 'Analysiere Liedplan...');

        let parsedResult = null;
        let modelLabel = '';

        if (apiKey) {
            try {
                const { parsed, modelUsed } = await processWithLLM(
                    apiKey,
                    text,
                    systemPrompt,
                    msg => { if (liveLog) liveLog.textContent = msg; }
                );
                parsedResult = parsed;
                modelLabel = modelUsed;
            } catch (llmErr) {
                console.warn('[AI Import] OpenRouter fehlgeschlagen, wechsle auf lokalen Parser:', llmErr);
                if (liveLog) liveLog.textContent = 'Free-Tier nicht erreichbar – nutze lokalen Offline-Parser...';
                const localItems = parseLiedplanLocally(text);
                if (!localItems.length) throw llmErr;
                const songRef = await loadSongReference();
                enrichTitlesFromReference(localItems, songRef);
                parsedResult = { items: localItems };
                modelLabel = 'Offline-Fallback';
            }
        } else {
            if (liveLog) liveLog.textContent = 'Kein API-Key angegeben – führe lokale Offline-Analyse durch...';
            const localItems = parseLiedplanLocally(text);
            if (!localItems.length) {
                throw new Error('Weder API-Key vorhanden noch konnte der lokale Parser Programmpunkte erkennen.');
            }
            const songRef = await loadSongReference();
            enrichTitlesFromReference(localItems, songRef);
            parsedResult = { items: localItems };
            modelLabel = 'Lokal (ohne API-Key)';
        }

        setStep(stepLlm, 'done', `Erfolgreich strukturiert (${modelLabel})`);
        setStep(stepApply, 'active', 'Vorschau vorbereiten...');

        const items = (parsedResult.items || []).map(it => ({
            zeile1: it.zeile1 || '',
            zeile2: it.zeile2 || '',
            zeile3: it.zeile3 || ''
        }));
        if (items.length === 0) {
            throw new Error('Keine Programmpunkte erkannt.');
        }

        setStep(stepApply, 'done', `Vorschau: ${items.length} Punkte`);
        if (resultBox) resultBox.hidden = false;
        if (rawPre) rawPre.textContent = JSON.stringify(parsedResult, null, 2);
        showImportPreview(items, `AI-Import (${modelLabel})`, null);
        showToast('success', `${items.length} Programmpunkte erkannt – Vorschau prüfen und übernehmen`);
    } catch (err) {
        console.error('[AI Import] Fehler:', err);
        setStep(stepExtract, stepExtract.className.includes('done') ? 'done' : 'error');
        setStep(stepLlm, stepLlm.className.includes('done') ? 'done' : 'error');
        setStep(stepApply, 'error', `Fehler: ${err.message}`);
        if (liveLog) liveLog.textContent = `Fehler: ${err.message}`;
        showToast('error', err.message);
    }
}

// -------------------------------------------------------------
// Datei-Import (CSV/XLSX/ZIP) & gemeinsame Vorschau
// -------------------------------------------------------------

/**
 * Verarbeitet eine hochgeladene Antwort-Exportdatei (CSV, XLSX oder ZIP).
 *
 * @param {File} file - Die Antwortdatei.
 */
async function handleFileImport(file) {
    try {
        const { services, count, filename } = await uploadAnswersFile(file);
        await loadServicesIntoImport(services, filename);
        showToast('success', `${filename} eingelesen (${count} Einreichungen)`);
    } catch (err) {
        showToast('error', err.message);
    }
}

/**
 * Lädt alle Einreichungen (neueste zuerst) in den Auswahl-Dropdown und
 * zeigt die erste (neueste) als Vorschau an.
 *
 * @param {Array<Object>} services - Liste geparster Gottesdienst-Objekte.
 * @param {string} sourceLabel - Anzeigename der Quelle (Dateiname oder 'Google Forms').
 */
async function loadServicesIntoImport(services, sourceLabel) {
    if (!Array.isArray(services) || services.length === 0) {
        throw new Error('Keine Einreichungen in dieser Quelle gefunden.');
    }

    importResponseList = services;
    const pick = document.getElementById('import-response-pick');
    const select = document.getElementById('import-response-select');

    if (services.length > 1) {
        if (select) {
            select.replaceChildren();
            services.forEach((service, idx) => {
                const opt = document.createElement('option');
                opt.value = String(idx);
                opt.textContent = formatResponseLabel(service);
                select.appendChild(opt);
            });
        }
        if (pick) pick.hidden = false;
    } else if (pick) {
        pick.hidden = true;
    }

    showImportPreview(serviceToItems(services[0]), sourceLabel, services[0]);
}

/**
 * Erzeugt eine lesbare Beschriftung für eine Formulareinreichung im Dropdown.
 *
 * @param {Object} service - Gottesdienst-Objekt.
 * @returns {string} Formatierte Beschriftung.
 */
function formatResponseLabel(service) {
    const parts = [service.zeitstempel || 'Ohne Zeitstempel'];
    if (service.woche) parts.push(`Woche ${service.woche}`);
    if (service.tag) parts.push(service.tag);
    if (service.organist) parts.push(`Org: ${service.organist}`);
    return parts.join(' · ');
}

/**
 * Gemeinsame Vorschau für alle Import-Quellen (F1: alle dynamischen Werte
 * werden maskiert, bevor sie per innerHTML eingefügt werden).
 *
 * @param {Array<{zeile1: string, zeile2: string, zeile3: string}>} items - Extrahierte Programmpunkte.
 * @param {string} sourceLabel - Quellen-Kennzeichnung für den Header.
 * @param {Object|null} [service=null] - Optionale Gottesdienst-Metadaten.
 */
function showImportPreview(items, sourceLabel, service = null) {
    const container = document.getElementById('forms-preview-container');
    const btnApply = document.getElementById('btn-apply-forms-data');
    if (!container) return;

    importPending = { items, sourceLabel, count: items.length };
    container.hidden = false;

    const meta = [];
    if (service) {
        if (service.woche) meta.push(`<span><strong>Woche:</strong> ${escapeHtml(service.woche)}</span>`);
        if (service.tag) meta.push(`<span><strong>Tag:</strong> ${escapeHtml(service.tag)}</span>`);
        if (service.organist) meta.push(`<span><strong>Organist:</strong> ${escapeHtml(service.organist)}</span>`);
        if (service.zeitstempel) meta.push(`<span><strong>Eingereicht:</strong> ${escapeHtml(service.zeitstempel)}</span>`);
    }

    container.innerHTML = `
        <div class="forms-summary-card">
            <div class="summary-header">
                <strong>Programmvorschau</strong>
                <span class="source-tag">${escapeHtml(sourceLabel)}</span>
            </div>
            ${meta.length ? `<div class="summary-meta">${meta.join('')}</div>` : ''}
            <div class="table-wrap">
                <table class="preview-table">
                    <thead>
                        <tr>
                            <th>Zeile 1</th>
                            <th>Zeile 2</th>
                            <th>Zeile 3</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${items.map(it => `
                            <tr>
                                <td><strong>${escapeHtml(it.zeile1) || '–'}</strong></td>
                                <td>${escapeHtml(it.zeile2) || '–'}</td>
                                <td>${escapeHtml(it.zeile3) || '–'}</td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            </div>
        </div>
    `;
    if (btnApply) btnApply.disabled = false;
}
