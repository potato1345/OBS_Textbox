// =====================================================
//  DASHBOARD STATE
// =====================================================
let dashboardState = {
    boxes: [],       // Array of { id, zeile1, zeile2, zeile3, boxColor, cornerColorA, cornerColorB }
    profiles: [],    // Array of { name, boxes: [...] }
    settings: {
        obsPassword: '',
        uiColors: {
            bgPrimary: '#1C1F26',
            bgPrimaryOpacity: 255,
            bgSecondary: '#323a44',
            bgSecondaryOpacity: 255,
            panelBg: '#272A33',
            panelBgOpacity: 255,
            border: '#3C404D',
            borderOpacity: 255,
            cardOuterBg: '#3C404D',
            cardOuterBgOpacity: 255,
            panelBgAlt: '#414852',
            panelBgAltOpacity: 255,
            btnHoverBg: '#414852',
            btnHoverBgOpacity: 255,
            textColor: '#eff0f1',
            textColorOpacity: 255,
            accentBlue: '#3daee9',
            accentBlueOpacity: 255
        }
    }
};

let isAutoSendEnabled = true;

// =====================================================
//  OBS WEBSOCKET SYNC
// =====================================================
const obs = new OBSWebSocket();
let isObsConnected = false;

obs.onConnect = () => {
    isObsConnected = true;
    updateConnectionUI(true);
    showToast('success', 'Mit OBS WebSocket verbunden');
    console.log("[OBS Sync] Erfolgreich mit OBS verbunden.");
    
    // Save password
    const pw = document.getElementById('ws-password').value;
    localStorage.setItem('obs_ws_pw', pw);
    
    // Initial sync
    pushDashboardStateToOBS();
};

obs.onDisconnect = () => {
    isObsConnected = false;
    updateConnectionUI(false);
    console.log("[OBS Sync] Verbindung zu OBS getrennt.");
};

obs.onError = (err) => {
    isObsConnected = false;
    updateConnectionUI(false);
    console.warn("[OBS Sync] Verbindungsfehler:", err);
};

function togglePasswordVisibility() {
    const passwordInput = document.getElementById('ws-password');
    const toggleButton = document.getElementById('btn-toggle-password');

    if (!passwordInput || !toggleButton) return;

    const isPasswordHidden = passwordInput.type === 'password';
    passwordInput.type = isPasswordHidden ? 'text' : 'password';
    toggleButton.textContent = isPasswordHidden ? '🙈' : '👁';
    toggleButton.setAttribute('aria-pressed', String(isPasswordHidden));
    toggleButton.title = isPasswordHidden ? 'Passwort verbergen' : 'Passwort anzeigen';
}

function connectOBS() {
    updateSettingsFromModal();
    connectToOBSWebSocket();
}

function connectToOBSWebSocket() {
    console.log("[OBS Sync] Verbindungsversuch gestartet...");
    const pw = document.getElementById('ws-password').value;
    obs.connect(pw).catch(err => {
        showToast('error', 'Verbindung fehlgeschlagen: ' + err.message);
        console.error("[OBS Sync] Verbindung fehlgeschlagen:", err);
    });
}

/**
 * Pushes the current dashboard state to OBS by modifying the Custom CSS of matching Browser Sources.
 * @param {string|null} specificBoxId - If provided, only updates the box with this ID.
 */
async function pushDashboardStateToOBS(specificBoxId = null) {
    if (!isObsConnected) {
        console.log("[OBS Sync] Abbruch: Nicht mit OBS verbunden.");
        return;
    }
    
    console.log(`[OBS Sync] Pushe Daten an OBS... ${specificBoxId ? '(Nur Box ID: ' + specificBoxId + ')' : '(Alle Boxen)'}`);
    
    try {
        const { inputs } = await obs.call('GetInputList', { inputKind: 'browser_source' });
        console.log(`[OBS Sync] ${inputs.length} Browser-Quellen gefunden. Prüfe auf Textboxen...`);
        
        for (const input of inputs) {
            const { inputSettings } = await obs.call('GetInputSettings', { inputName: input.inputName });
            
            if (inputSettings && inputSettings.css) {
                let css = inputSettings.css;
                const match = css.match(/--box-id:\s*["']([^"']+)["']/);
                
                if (match && match[1]) {
                    const targetId = match[1];
                    
                    // Skip if we only want to update a specific box and this isn't it
                    if (specificBoxId && specificBoxId !== targetId) continue;
                    
                    const boxData = dashboardState.boxes.find(b => b.id === targetId);
                    
                    if (boxData) {
                        console.log(`[OBS Sync] Aktualisiere Textbox-Quelle: "${input.inputName}" (ID: ${targetId})`);
                        
                        // Remove old injected block
                        css = css.replace(/\/\* OBS_TEXTBOX_INJECT_START \*\/[\s\S]*?\/\* OBS_TEXTBOX_INJECT_END \*\//, '');
                        
                        const hasText = !!(boxData.zeile1 || boxData.zeile2 || boxData.zeile3);
                        
                        // Inject new block
                        const injectedCss = `
/* OBS_TEXTBOX_INJECT_START */
:root {
  --zeile-1: "${(boxData.zeile1 || '').replace(/"/g, '\\"')}";
  --zeile-2: "${(boxData.zeile2 || '').replace(/"/g, '\\"')}";
  --zeile-3: "${(boxData.zeile3 || '').replace(/"/g, '\\"')}";
  --box-bg: ${hexToRgba(boxData.boxColor || '#ffffff', boxData.boxColorOpacity ?? 255)};
  --triangle-color-a: ${hexToRgba(boxData.cornerColorA || '#fce647', boxData.cornerColorAOpacity ?? 255)};
  --triangle-color-b: ${hexToRgba(boxData.cornerColorB || '#fce647', boxData.cornerColorBOpacity ?? 255)};
  --text-color: ${hexToRgba(boxData.textColor || '#000000', boxData.textColorOpacity ?? 255)};
  --box-opacity: ${hasText ? 1 : 0};
}
/* OBS_TEXTBOX_INJECT_END */`;
                        
                        css = css.trim() + '\n' + injectedCss;
                        
                        await obs.call('SetInputSettings', {
                            inputName: input.inputName,
                            inputSettings: { css: css },
                            overlay: true
                        });
                    }
                }
            }
        }
        console.log("[OBS Sync] Push an OBS abgeschlossen.");
    } catch (e) {
        console.error("[OBS Sync] Fehler beim Senden an OBS:", e);
    }
}

// =====================================================
//  LOCAL STORAGE & AUTO-SEND LOGIC
// =====================================================

function toggleAutoSend() {
    const toggleEl = document.getElementById('auto-send-toggle');
    if (toggleEl) {
        isAutoSendEnabled = toggleEl.checked;
        console.log(`[Einstellungen] Auto-Send ist nun: ${isAutoSendEnabled ? 'AN' : 'AUS'}`);
        // Optionally save this preference in local storage
        localStorage.setItem('obs_auto_send', isAutoSendEnabled);
    }
}

function saveStateLocally() {
    try {
        localStorage.setItem('obs_textbox_state', JSON.stringify(dashboardState));
        console.log("[Speicher] Zustand im lokalen Browser-Speicher gesichert.");
    } catch (e) {
        console.warn("[Speicher] Fehler beim lokalen Speichern:", e);
    }
}

// Debounced handler for input changes
let inputChangeTimeout = null;
function handleDataChange() {
    saveStateLocally();

    if (isAutoSendEnabled) {
        clearTimeout(inputChangeTimeout);
        inputChangeTimeout = setTimeout(() => {
            pushDashboardStateToOBS();
        }, 150);
    }
}

// Update the connection status UI
function updateConnectionUI(connected) {
    const statusEl = document.getElementById('storage-status');
    const labelEl = document.getElementById('storage-label');
    if (connected) {
        statusEl.classList.add('connected');
        labelEl.textContent = 'Aktiv (Lokal)';
        const settingsStatus = document.getElementById('settings-status-label');
        if (settingsStatus) settingsStatus.textContent = 'Status: Aktiv (Lokal)';
    } else {
        statusEl.classList.remove('connected');
        labelEl.textContent = 'Fehler / Getrennt';
        const settingsStatus = document.getElementById('settings-status-label');
        if (settingsStatus) settingsStatus.textContent = 'Status: Fehler / Getrennt';
    }
}


// =====================================================
//  MANUAL EXPORT / IMPORT
// =====================================================
function exportJSON() {
    syncDOMToState();
    const json = JSON.stringify(dashboardState, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = 'obs_textbox_data.json';
    a.click();
    URL.revokeObjectURL(url);
    showToast('success', 'JSON exportiert');
    console.log("[Export] Daten erfolgreich exportiert.");
}

function importJSON() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;
        try {
            const text = await file.text();
            const data = JSON.parse(text);
            dashboardState = { ...dashboardState, ...data };
            renderCardsFromState();
            applyThemeSettings();
            saveStateLocally();
            if (isAutoSendEnabled) pushDashboardStateToOBS();
            showToast('success', `Importiert: ${file.name}`);
            console.log(`[Import] Datei '${file.name}' erfolgreich importiert.`);
        } catch (err) {
            showToast('error', 'Fehler beim Importieren');
            console.error("[Import] Fehler:", err);
        }
    };
    input.click();
}

function registerSettingsInputListeners() {
    const settingsInputs = [
        'ws-password',
        'auto-send-toggle',
        'ui-color-bg-primary',
        'ui-opacity-bg-primary',
        'ui-color-bg-secondary',
        'ui-opacity-bg-secondary',
        'ui-color-panel-bg',
        'ui-opacity-panel-bg',
        'ui-color-border',
        'ui-opacity-border',
        'ui-color-card-outer-bg',
        'ui-opacity-card-outer-bg',
        'ui-color-panel-bg-alt',
        'ui-opacity-panel-bg-alt',
        'ui-color-btn-hover-bg',
        'ui-opacity-btn-hover-bg',
        'ui-color-text',
        'ui-opacity-text',
        'ui-color-accent-blue',
        'ui-opacity-accent-blue'
    ];

    settingsInputs.forEach(id => {
        const element = document.getElementById(id);
        if (!element) return;

        element.addEventListener('input', () => {
            if (id === 'auto-send-toggle') {
                toggleAutoSend();
            }

            updateSettingsFromModal();
        });
    });
}

// =====================================================
//  CARD MANAGEMENT (UI)
// =====================================================
const dashboard = document.getElementById('dashboard');
const template = document.getElementById('card-template');

function addNewCard(id = 'neue_box', z1 = '', z2 = '', z3 = '', boxColor = '#ffffff', boxColorOpacity = 255, cornerColorA = '#fce647', cornerColorAOpacity = 255, cornerColorB = '#fce647', cornerColorBOpacity = 255, textColor = '#000000', textColorOpacity = 255) {
    console.log(`[UI] Füge neue Karte hinzu (ID: ${id})...`);
    const clone = template.content.cloneNode(true);
    const cardWrapper = clone.querySelector('.card-outer');
    const cardInner = clone.querySelector('.card-inner');
    cardWrapper.querySelector('.id-input').value = id;
    cardInner.querySelector('.z1').value = z1;
    cardInner.querySelector('.z2').value = z2;
    cardInner.querySelector('.z3').value = z3;
    cardInner.querySelector('.box-color').value = boxColor;
    cardInner.querySelector('.box-opacity').value = boxColorOpacity;
    cardInner.querySelector('.corner-a-color').value = cornerColorA;
    cardInner.querySelector('.corner-a-opacity').value = cornerColorAOpacity;
    cardInner.querySelector('.corner-b-color').value = cornerColorB;
    cardInner.querySelector('.corner-b-opacity').value = cornerColorBOpacity;
    cardInner.querySelector('.text-color').value = textColor;
    cardInner.querySelector('.text-color-opacity').value = textColorOpacity;

    // Trigger changes when typing or color picking
    clone.querySelectorAll('input').forEach(input => {
        input.addEventListener('input', () => {
            syncDOMToState();
            handleDataChange();
        });
    });

    dashboard.appendChild(clone);
    syncDOMToState();
    handleDataChange();
}

function deleteCard(btn) {
    const cardWrapper = btn.closest('.card-outer');
    const id = cardWrapper.querySelector('.id-input').value;
    console.log(`[UI] Lösche Karte (ID: ${id})...`);
    
    cardWrapper.style.transition = 'all 0.25s ease';
    cardWrapper.style.opacity = '0';
    cardWrapper.style.transform = 'scale(0.95)';
    
    setTimeout(() => {
        cardWrapper.remove();
        syncDOMToState();
        handleDataChange();
    }, 250);
}

function sendCard(btn) {
    const cardWrapper = btn.closest('.card-outer');
    const targetId = cardWrapper.querySelector('.id-input').value.trim();
    console.log(`[UI] Sende manuell: Karte (ID: ${targetId})`);
    
    syncDOMToState();
    saveStateLocally();
    pushDashboardStateToOBS(targetId);
    
    showToast('success', `Gesendet an "${targetId}"`);
}

function clearCard(btn) {
    const cardWrapper = btn.closest('.card-outer');
    const targetId = cardWrapper.querySelector('.id-input').value.trim();
    console.log(`[UI] Leere Karte (ID: ${targetId})...`);
    
    const cardInner = cardWrapper.querySelector('.card-inner');
    cardInner.querySelector('.z1').value = '';
    cardInner.querySelector('.z2').value = '';
    cardInner.querySelector('.z3').value = '';
    
    syncDOMToState();
    saveStateLocally();
    
    // Always push when cleared explicitly by button
    pushDashboardStateToOBS(targetId);
}

function sendAllCards() {
    console.log("[UI] Sende manuell alle Karten an OBS...");
    syncDOMToState();
    saveStateLocally();
    pushDashboardStateToOBS();
    
    const cards = dashboard.querySelectorAll('.card');
    if (cards.length > 0) {
        showToast('success', `${cards.length} Box(en) gesendet`);
    }
}

function clearAllCards() {
    console.log("[UI] Leere alle Karten...");
    const cards = dashboard.querySelectorAll('.card');
    cards.forEach(card => {
        card.querySelector('.z1').value = '';
        card.querySelector('.z2').value = '';
        card.querySelector('.z3').value = '';
    });
    
    syncDOMToState();
    saveStateLocally();
    pushDashboardStateToOBS(); // Always push when cleared explicitly by button
    
    if (cards.length > 0) {
        showToast('info', 'Alle Boxen geleert');
    }
}


// =====================================================
//  STATE SYNC (DOM <-> dashboardState)
// =====================================================
function syncDOMToState() {
    const cardWrappers = dashboard.querySelectorAll('.card-outer');
    dashboardState.boxes = [];
    cardWrappers.forEach(wrapper => {
        dashboardState.boxes.push({
            id: wrapper.querySelector('.id-input').value.trim(),
            zeile1: wrapper.querySelector('.z1').value,
            zeile2: wrapper.querySelector('.z2').value,
            zeile3: wrapper.querySelector('.z3').value,
            boxColor: wrapper.querySelector('.box-color').value,
            boxColorOpacity: parseOpacityInputFromElement(wrapper.querySelector('.box-opacity')),
            cornerColorA: wrapper.querySelector('.corner-a-color').value,
            cornerColorAOpacity: parseOpacityInputFromElement(wrapper.querySelector('.corner-a-opacity')),
            cornerColorB: wrapper.querySelector('.corner-b-color').value,
            cornerColorBOpacity: parseOpacityInputFromElement(wrapper.querySelector('.corner-b-opacity')),
            textColor: wrapper.querySelector('.text-color').value,
            textColorOpacity: parseOpacityInputFromElement(wrapper.querySelector('.text-color-opacity'))
        });
    });
}

function renderCardsFromState() {
    console.log("[UI] Rendere Karten aus dem State...");
    dashboard.innerHTML = ''; // Clear existing cards

    if (dashboardState.boxes && dashboardState.boxes.length > 0) {
        dashboardState.boxes.forEach(box => {
            addNewCard(
                box.id || 'box',
                box.zeile1 || '',
                box.zeile2 || '',
                box.zeile3 || '',
                box.boxColor || '#ffffff',
                box.boxColorOpacity !== undefined ? box.boxColorOpacity : 255,
                box.cornerColorA || '#fce647',
                box.cornerColorAOpacity !== undefined ? box.cornerColorAOpacity : 255,
                box.cornerColorB || '#fce647',
                box.cornerColorBOpacity !== undefined ? box.cornerColorBOpacity : 255,
                box.textColor || '#000000',
                box.textColorOpacity !== undefined ? box.textColorOpacity : 255
            );
        });
    } else {
        console.log("[UI] Keine Karten im State, erstelle Standard-Karten.");
        addNewCard('lowerthird');
        addNewCard('info');
    }

    renderCustomProfiles();
}


// =====================================================
//  PROFILES
// =====================================================

function applyProfileData(profile) {
    console.log(`[Profile] Lade Profil: "${profile.name}"`);
    dashboard.innerHTML = '';
    
    profile.boxes.forEach(box => {
        addNewCard(
            box.id,
            box.zeile1 || '',
            box.zeile2 || '',
            box.zeile3 || '',
            box.boxColor || '#ffffff',
            box.boxColorOpacity !== undefined ? box.boxColorOpacity : 255,
            box.cornerColorA || '#fce647',
            box.cornerColorAOpacity !== undefined ? box.cornerColorAOpacity : 255,
            box.cornerColorB || '#fce647',
            box.cornerColorBOpacity !== undefined ? box.cornerColorBOpacity : 255,
            box.textColor || '#000000',
            box.textColorOpacity !== undefined ? box.textColorOpacity : 255
        );
    });
    
    syncDOMToState();
    handleDataChange(); // Auto-send will trigger if enabled
    
    showToast('info', `Profil "${profile.name}" geladen`);
}

function openSaveProfileModal() {
    syncDOMToState();
    document.getElementById('save-profile-modal').hidden = false;
    const nameInput = document.getElementById('profile-name-input');
    nameInput.value = '';
    nameInput.focus();
}

function closeSaveProfileModal() {
    document.getElementById('save-profile-modal').hidden = true;
}

function openSettingsModal() {
    document.getElementById('settings-modal').hidden = false;
    loadSettingsToModal();
}

function closeSettingsModal() {
    updateSettingsFromModal();
    document.getElementById('settings-modal').hidden = true;
}

function loadSettingsToModal() {
    const settings = dashboardState.settings || {};
    const colors = settings.uiColors || {};

    document.getElementById('ws-password').value = settings.obsPassword || '';
    document.getElementById('auto-send-toggle').checked = isAutoSendEnabled;
    document.getElementById('ui-color-bg-primary').value = colors.bgPrimary || '#1C1F26';
    document.getElementById('ui-opacity-bg-primary').value = colors.bgPrimaryOpacity !== undefined ? colors.bgPrimaryOpacity : 255;
    document.getElementById('ui-color-bg-secondary').value = colors.bgSecondary || '#323a44';
    document.getElementById('ui-opacity-bg-secondary').value = colors.bgSecondaryOpacity !== undefined ? colors.bgSecondaryOpacity : 255;
    document.getElementById('ui-color-panel-bg').value = colors.panelBg || '#272A33';
    document.getElementById('ui-opacity-panel-bg').value = colors.panelBgOpacity !== undefined ? colors.panelBgOpacity : 255;
    document.getElementById('ui-color-border').value = colors.border || '#3C404D';
    document.getElementById('ui-opacity-border').value = colors.borderOpacity !== undefined ? colors.borderOpacity : 255;
    document.getElementById('ui-color-card-outer-bg').value = colors.cardOuterBg || '#3C404D';
    document.getElementById('ui-opacity-card-outer-bg').value = colors.cardOuterBgOpacity !== undefined ? colors.cardOuterBgOpacity : 255;
    document.getElementById('ui-color-panel-bg-alt').value = colors.panelBgAlt || '#414852';
    document.getElementById('ui-opacity-panel-bg-alt').value = colors.panelBgAltOpacity !== undefined ? colors.panelBgAltOpacity : 255;
    document.getElementById('ui-color-btn-hover-bg').value = colors.btnHoverBg || '#414852';
    document.getElementById('ui-opacity-btn-hover-bg').value = colors.btnHoverBgOpacity !== undefined ? colors.btnHoverBgOpacity : 255;
    document.getElementById('ui-color-text').value = colors.textColor || '#eff0f1';
    document.getElementById('ui-opacity-text').value = colors.textColorOpacity !== undefined ? colors.textColorOpacity : 255;
    document.getElementById('ui-color-accent-blue').value = colors.accentBlue || '#3daee9';
    document.getElementById('ui-opacity-accent-blue').value = colors.accentBlueOpacity !== undefined ? colors.accentBlueOpacity : 255;
    applyThemeSettings();
}

function parseOpacityInput(id) {
    const raw = document.getElementById(id)?.value;
    const value = Number(raw);
    if (Number.isNaN(value)) return 255;
    return Math.max(0, Math.min(255, Math.round(value)));
}

function parseOpacityInputFromElement(element) {
    if (!element) return 255;
    const value = Number(element.value);
    if (Number.isNaN(value)) return 255;
    return Math.max(0, Math.min(255, Math.round(value)));
}

function hexToRgba(hex, opacity255 = 255) {
    const normalized = (hex || '#000000').trim();
    const match = normalized.match(/^#?([a-f\d]{6})$/i);
    if (!match) return normalized;

    const intVal = parseInt(match[1], 16);
    const r = (intVal >> 16) & 255;
    const g = (intVal >> 8) & 255;
    const b = intVal & 255;
    const alpha = Math.max(0, Math.min(255, Number(opacity255))) / 255;

    return alpha === 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${alpha.toFixed(3)})`;
}

function updateSettingsFromModal() {
    dashboardState.settings = dashboardState.settings || {};
    dashboardState.settings.obsPassword = document.getElementById('ws-password').value;
    dashboardState.settings.uiColors = {
        bgPrimary: document.getElementById('ui-color-bg-primary').value,
        bgPrimaryOpacity: parseOpacityInput('ui-opacity-bg-primary'),
        bgSecondary: document.getElementById('ui-color-bg-secondary').value,
        bgSecondaryOpacity: parseOpacityInput('ui-opacity-bg-secondary'),
        panelBg: document.getElementById('ui-color-panel-bg').value,
        panelBgOpacity: parseOpacityInput('ui-opacity-panel-bg'),
        border: document.getElementById('ui-color-border').value,
        borderOpacity: parseOpacityInput('ui-opacity-border'),
        cardOuterBg: document.getElementById('ui-color-card-outer-bg').value,
        cardOuterBgOpacity: parseOpacityInput('ui-opacity-card-outer-bg'),
        panelBgAlt: document.getElementById('ui-color-panel-bg-alt').value,
        panelBgAltOpacity: parseOpacityInput('ui-opacity-panel-bg-alt'),
        btnHoverBg: document.getElementById('ui-color-btn-hover-bg').value,
        btnHoverBgOpacity: parseOpacityInput('ui-opacity-btn-hover-bg'),
        textColor: document.getElementById('ui-color-text').value,
        textColorOpacity: parseOpacityInput('ui-opacity-text'),
        accentBlue: document.getElementById('ui-color-accent-blue').value,
        accentBlueOpacity: parseOpacityInput('ui-opacity-accent-blue')
    };
    applyThemeSettings();
    saveStateLocally();
}

function applyThemeSettings() {
    const colors = dashboardState.settings?.uiColors || {};
    document.documentElement.style.setProperty('--bg-primary', hexToRgba(colors.bgPrimary || '#1C1F26', colors.bgPrimaryOpacity ?? 255));
    document.documentElement.style.setProperty('--bg-secondary', hexToRgba(colors.bgSecondary || '#323a44', colors.bgSecondaryOpacity ?? 255));
    document.documentElement.style.setProperty('--panel-bg', hexToRgba(colors.panelBg || '#272A33', colors.panelBgOpacity ?? 255));
    document.documentElement.style.setProperty('--border', hexToRgba(colors.border || '#3C404D', colors.borderOpacity ?? 255));
    document.documentElement.style.setProperty('--card-outer-bg', hexToRgba(colors.cardOuterBg || '#3C404D', colors.cardOuterBgOpacity ?? 255));
    document.documentElement.style.setProperty('--panel-bg-alt', hexToRgba(colors.panelBgAlt || '#414852', colors.panelBgAltOpacity ?? 255));
    document.documentElement.style.setProperty('--btn-hover-bg', hexToRgba(colors.btnHoverBg || '#414852', colors.btnHoverBgOpacity ?? 255));
    document.documentElement.style.setProperty('--text-primary', hexToRgba(colors.textColor || '#eff0f1', colors.textColorOpacity ?? 255));
    document.documentElement.style.setProperty('--accent-blue', hexToRgba(colors.accentBlue || '#3daee9', colors.accentBlueOpacity ?? 255));
}

function saveCurrentAsProfile() {
    const name = document.getElementById('profile-name-input').value.trim();
    if (!name) {
        showToast('error', 'Bitte einen Profilnamen eingeben');
        return;
    }

    console.log(`[Profile] Speichere aktuelles Setup als Profil: "${name}"`);
    syncDOMToState();
    
    const profile = {
        name: name,
        boxes: JSON.parse(JSON.stringify(dashboardState.boxes))
    };

    if (!dashboardState.profiles) dashboardState.profiles = [];
    
    const existingIdx = dashboardState.profiles.findIndex(p => p.name === name);
    if (existingIdx >= 0) {
        dashboardState.profiles[existingIdx] = profile; // Replace
    } else {
        dashboardState.profiles.push(profile); // Add new
    }

    saveStateLocally();
    renderCustomProfiles();
    closeSaveProfileModal();
    showToast('success', `Profil "${name}" gespeichert`);
}

function deleteCustomProfile(index) {
    const name = dashboardState.profiles[index].name;
    console.log(`[Profile] Lösche Profil: "${name}"`);
    
    dashboardState.profiles.splice(index, 1);
    saveStateLocally();
    renderCustomProfiles();
    
    showToast('info', `Profil "${name}" gelöscht`);
}

function renderCustomProfiles() {
    document.querySelectorAll('.profile-chip.custom').forEach(el => el.remove());

    const anchor = document.getElementById('custom-profiles-anchor');
    if (!dashboardState.profiles) return;

    dashboardState.profiles.forEach((profile, idx) => {
        const chip = document.createElement('button');
        chip.className = 'profile-chip custom';
        chip.innerHTML = `
            <span class="chip-icon">📋</span>
            ${escapeHtml(profile.name)}
            <button class="delete-profile" onclick="event.stopPropagation(); deleteCustomProfile(${idx})" title="Profil löschen">✕</button>
        `;
        chip.onclick = (e) => {
            if (e.target.classList.contains('delete-profile')) return;
            applyProfileData(profile);
        };
        anchor.parentNode.insertBefore(chip, anchor);
    });
}


// =====================================================
//  TOAST NOTIFICATIONS & UTILS
// =====================================================
function showToast(type, message) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${type}`;

    const icons = { success: '✓', error: '✗', info: 'ℹ' };
    toast.innerHTML = `<span class="toast-icon">${icons[type] || ''}</span> ${escapeHtml(message)}`;

    container.appendChild(toast);

    setTimeout(() => {
        toast.classList.add('fade-out');
        setTimeout(() => toast.remove(), 300);
    }, 2500);
}

function escapeHtml(str) {
    const div = document.createElement('div');
    div.textContent = str;
    return div.innerHTML;
}


// =====================================================
//  KEYBOARD SHORTCUTS
// =====================================================
document.addEventListener('keydown', (e) => {
    if (!document.getElementById('save-profile-modal').hidden && e.key === 'Enter') {
        saveCurrentAsProfile();
    }
    if (e.key === 'Escape') {
        closeSaveProfileModal();
    }
});


// =====================================================
//  INITIALIZATION
// =====================================================
async function init() {
    console.log("[Init] Dashboard Initialisierung startet...");
    
    // Load Auto-Send Preference
    const savedAutoSend = localStorage.getItem('obs_auto_send');
    if (savedAutoSend !== null) {
        isAutoSendEnabled = (savedAutoSend === 'true');
        const toggleEl = document.getElementById('auto-send-toggle');
        if (toggleEl) toggleEl.checked = isAutoSendEnabled;
        console.log(`[Init] Auto-Send Einstellung geladen: ${isAutoSendEnabled}`);
    }
    
    // Load State
    try {
        const lsData = localStorage.getItem('obs_textbox_state');
        if (lsData) {
            console.log("[Init] State aus localStorage geladen.");
            const parsed = JSON.parse(lsData);
            dashboardState = { ...dashboardState, ...parsed };
        } else {
            console.log("[Init] Kein existierender State gefunden, starte frisch.");
        }
    } catch (e) {
        console.warn("[Init] Fehler beim Lesen von localStorage:", e);
    }

    renderCardsFromState();
    applyThemeSettings();
    registerSettingsInputListeners();
    updateConnectionUI(false);
    
    // Auto-Connect OBS WebSocket
    const savedPw = localStorage.getItem('obs_ws_pw');
    if (savedPw !== null) {
        document.getElementById('ws-password').value = savedPw;
        console.log("[Init] Gespeichertes OBS Passwort gefunden, verbinde...");
        connectToOBSWebSocket();
    }
    
    console.log("[Init] Initialisierung abgeschlossen.");
}

init();
