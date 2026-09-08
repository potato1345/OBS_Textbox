// =====================================================
//  DEFAULT COLOR PROFILES (from color-profiles.yaml)
// =====================================================
const DEFAULT_COLOR_PROFILES = [
    {
        name: 'OBS Dark',
        isDefault: true,
        colors: {
            bgPrimary: '#1C1F26', bgPrimaryOpacity: 255,
            bgSecondary: '#323a44', bgSecondaryOpacity: 255,
            panelBg: '#272A33', panelBgOpacity: 255,
            border: '#3C404D', borderOpacity: 255,
            cardOuterBg: '#3C404D', cardOuterBgOpacity: 255,
            panelBgAlt: '#414852', panelBgAltOpacity: 255,
            btnHoverBg: '#414852', btnHoverBgOpacity: 255,
            textColor: '#eff0f1', textColorOpacity: 255,
            accentBlue: '#3daee9', accentBlueOpacity: 255
        }
    },
    {
        name: 'OBS Light',
        isDefault: true,
        colors: {
            bgPrimary: '#f0f0f0', bgPrimaryOpacity: 255,
            bgSecondary: '#ffffff', bgSecondaryOpacity: 255,
            panelBg: '#e8e8e8', panelBgOpacity: 255,
            border: '#c0c0c0', borderOpacity: 255,
            cardOuterBg: '#d5d5d5', cardOuterBgOpacity: 255,
            panelBgAlt: '#dcdcdc', panelBgAltOpacity: 255,
            btnHoverBg: '#d0d0d0', btnHoverBgOpacity: 255,
            textColor: '#1a1a1a', textColorOpacity: 255,
            accentBlue: '#2980b9', accentBlueOpacity: 255
        }
    },
    {
        name: 'Midnight Blue',
        isDefault: true,
        colors: {
            bgPrimary: '#0d1117', bgPrimaryOpacity: 255,
            bgSecondary: '#161b22', bgSecondaryOpacity: 255,
            panelBg: '#1c2333', panelBgOpacity: 255,
            border: '#30363d', borderOpacity: 255,
            cardOuterBg: '#252d3a', cardOuterBgOpacity: 255,
            panelBgAlt: '#2a3342', panelBgAltOpacity: 255,
            btnHoverBg: '#2d3748', btnHoverBgOpacity: 255,
            textColor: '#c9d1d9', textColorOpacity: 255,
            accentBlue: '#58a6ff', accentBlueOpacity: 255
        }
    },
    {
        name: 'Forest Green',
        isDefault: true,
        colors: {
            bgPrimary: '#1a2318', bgPrimaryOpacity: 255,
            bgSecondary: '#2a3528', bgSecondaryOpacity: 255,
            panelBg: '#222d20', panelBgOpacity: 255,
            border: '#3a4a36', borderOpacity: 255,
            cardOuterBg: '#344530', cardOuterBgOpacity: 255,
            panelBgAlt: '#3d4f38', panelBgAltOpacity: 255,
            btnHoverBg: '#455a40', btnHoverBgOpacity: 255,
            textColor: '#d4e0d0', textColorOpacity: 255,
            accentBlue: '#6bcf7f', accentBlueOpacity: 255
        }
    },
    {
        name: 'Sunset Warm',
        isDefault: true,
        colors: {
            bgPrimary: '#1f1510', bgPrimaryOpacity: 255,
            bgSecondary: '#2d1f18', bgSecondaryOpacity: 255,
            panelBg: '#261a14', panelBgOpacity: 255,
            border: '#4a3228', borderOpacity: 255,
            cardOuterBg: '#3d2a20', cardOuterBgOpacity: 255,
            panelBgAlt: '#4a3428', panelBgAltOpacity: 255,
            btnHoverBg: '#553d30', btnHoverBgOpacity: 255,
            textColor: '#f0ddd0', textColorOpacity: 255,
            accentBlue: '#f59e42', accentBlueOpacity: 255
        }
    },
    {
        name: 'High Contrast',
        isDefault: true,
        colors: {
            bgPrimary: '#000000', bgPrimaryOpacity: 255,
            bgSecondary: '#1a1a1a', bgSecondaryOpacity: 255,
            panelBg: '#0d0d0d', panelBgOpacity: 255,
            border: '#ffffff', borderOpacity: 255,
            cardOuterBg: '#222222', cardOuterBgOpacity: 255,
            panelBgAlt: '#2a2a2a', panelBgAltOpacity: 255,
            btnHoverBg: '#333333', btnHoverBgOpacity: 255,
            textColor: '#ffffff', textColorOpacity: 255,
            accentBlue: '#00e5ff', accentBlueOpacity: 255
        }
    }
];

// =====================================================
//  DASHBOARD STATE
// =====================================================
let dashboardState = {
    boxes: [],       // Array of { id, zeile1, zeile2, zeile3, boxColor, cornerColorA, cornerColorB, rainbowCorners }
    profiles: [],    // Array of { name, boxes: [...] }
    settings: {
        obsPassword: '',
        activeColorProfile: 'OBS Dark',
        colorProfiles: [],   // User-defined color profiles
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
// Merkt die letzte an OBS geschickte CSS-Zeichenkette pro Quelle,
// damit unveränderte Pushes keine OBS-Reloads auslösen
const lastAppliedCss = {};
let reconnectTimer = null;
let suppressReconnect = false;

obs.onConnect = () => {
    isObsConnected = true;
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    updateConnectionUI(true);
    showToast('success', 'Mit OBS WebSocket verbunden');
    console.log("[OBS Sync] Erfolgreich mit OBS verbunden.");
    
    // Save password
    const pw = document.getElementById('ws-password').value;
    if (pw) localStorage.setItem('obs_ws_pw', pw);
    
    // Initial sync
    pushDashboardStateToOBS();
};

obs.onDisconnect = () => {
    isObsConnected = false;
    updateConnectionUI(false);
    console.log("[OBS Sync] Verbindung zu OBS getrennt.");
    scheduleReconnect();
};

obs.onError = (err) => {
    isObsConnected = false;
    updateConnectionUI(false);
    console.warn("[OBS Sync] Verbindungsfehler:", err);
};

/**
 * Reconnectet automatisch, wenn die Verbindung abbricht (z.B. OBS-Neustart).
 */
function scheduleReconnect() {
    if (suppressReconnect || reconnectTimer || isObsConnected) return;
    if (!localStorage.getItem('obs_ws_pw') && !document.getElementById('ws-password')?.value) return;
    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        if (!isObsConnected) {
            console.log("[OBS Sync] Automatische Reconnect-Versuch...");
            connectToOBSWebSocket(false);
        }
    }, 3000);
}

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
    const pwInput = document.getElementById('ws-password');
    if (pwInput) {
        dashboardState.settings = dashboardState.settings || {};
        dashboardState.settings.obsPassword = pwInput.value;
        saveStateLocally();
    }
    connectToOBSWebSocket();
}

function toggleOBSConnection() {
    if (isObsConnected) {
        disconnectOBS();
        return;
    }
    connectOBS();
}

function connectToOBSWebSocket(showError = true) {
    suppressReconnect = false;
    console.log("[OBS Sync] Verbindungsversuch gestartet...");
    const pw = document.getElementById('ws-password').value;
    obs.connect(pw).catch(err => {
        if (showError) {
            showToast('error', 'Verbindung fehlgeschlagen: ' + (err.message || err));
        }
        console.error("[OBS Sync] Verbindung fehlgeschlagen:", err);
        // Will re-try on next error/disconnect event via scheduleReconnect()
    });
}

function disconnectOBS() {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    suppressReconnect = true; // No auto-reconnect after a manual disconnect
    obs.disconnect();
    isObsConnected = false;
    updateConnectionUI(false);
    showToast('info', 'OBS-Verbindung getrennt');
    console.log("[OBS Sync] Manuell getrennt.");
}

/**
 * Pushes the current dashboard state to OBS by modifying the Custom CSS of matching Browser Sources.
 * @param {string|null} specificBoxId - If provided, only updates the box with this ID.
 */
function escapeCssString(str) {
    // CSS String-Werte: Steuerzeichen entfernen und Backslash sowie Anführungszeichen maskieren,
    // sonst zerbricht das injizierte CSS bei z.B. "C:\\Temp" oder Umlaut-Escapes
    return String(str)
        .replace(/[\u0000-\u001F\u007F-\u009F]/g, '')
        .replace(/\\/g, '\\\\')
        .replace(/"/g, '\\"');
}

async function pushDashboardStateToOBS(specificBoxId = null) {
    if (!isObsConnected) {
        console.log("[OBS Sync] Abbruch: Nicht mit OBS verbunden.");
        return;
    }
    
    console.log(`[OBS Sync] Pushe Daten an OBS... ${specificBoxId ? '(Nur Box ID: ' + specificBoxId + ')' : '(Alle Boxen)'}`);
    
    let updated = 0;
    let checked = 0;
    
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
                    checked++;
                    
                    // Skip if we only want to update a specific box and this isn't it
                    if (specificBoxId && specificBoxId !== targetId) continue;
                    
                    const boxData = dashboardState.boxes.find(b => b.id === targetId);
                    
                    if (boxData) {
                        // Remove old injected block
                        css = css.replace(/\/\* OBS_TEXTBOX_INJECT_START \*\/[\s\S]*?\/\* OBS_TEXTBOX_INJECT_END \*\//, '');
                        
                        const hasText = !!(boxData.zeile1 || boxData.zeile2 || boxData.zeile3);
                        
                        // Inject new block
                        let injectedCss;
                        if (boxData.rainbowCorners) {
                            // Smooth rainbow corners: GPU-accelerated hue-rotate over 12s
                            injectedCss = `
/* OBS_TEXTBOX_INJECT_START */
:root {
  --zeile-1: "${escapeCssString(boxData.zeile1 || '')}";
  --zeile-2: "${escapeCssString(boxData.zeile2 || '')}";
  --zeile-3: "${escapeCssString(boxData.zeile3 || '')}";
  --box-bg: ${hexToRgba(boxData.boxColor || '#ffffff', boxData.boxColorOpacity ?? 255)};
  --triangle-color-a: ${hexToRgba(boxData.cornerColorA || '#fce647', boxData.cornerColorAOpacity ?? 255)};
    --triangle-color-b: ${hexToRgba(boxData.cornerColorB || '#fce647', boxData.cornerColorBOpacity ?? 255)};
  --text-color: ${hexToRgba(boxData.textColor || '#000000', boxData.textColorOpacity ?? 255)};
  --box-opacity: ${hasText ? 1 : 0};
}
.textbox-container {
  background-image: none !important;
}
.textbox-container::before {
  content: "" !important;
  display: block !important;
  position: absolute;
  top: 0;
  left: 0;
  width: 50px;
  height: 50px;
  border-top-left-radius: 4px;
  background: linear-gradient(135deg, var(--triangle-color-a) 50%, transparent 50%);
  pointer-events: none;
  z-index: 0;
  animation: rainbow-rotate 12s linear infinite;
}
.textbox-container::after {
  content: "" !important;
  display: block !important;
  position: absolute;
  bottom: 0;
  right: 0;
  width: 50px;
  height: 50px;
  border-bottom-right-radius: 4px;
  background: linear-gradient(315deg, var(--triangle-color-b) 50%, transparent 50%);
  pointer-events: none;
  z-index: 0;
  animation: rainbow-rotate 12s linear infinite;
}
.text-line {
  position: relative;
  z-index: 1;
}
@keyframes rainbow-rotate {
  from { filter: hue-rotate(0deg); }
  to   { filter: hue-rotate(360deg); }
}
/* OBS_TEXTBOX_INJECT_END */`;
                        } else {
                            injectedCss = `
/* OBS_TEXTBOX_INJECT_START */
:root {
  --zeile-1: "${escapeCssString(boxData.zeile1 || '')}";
  --zeile-2: "${escapeCssString(boxData.zeile2 || '')}";
  --zeile-3: "${escapeCssString(boxData.zeile3 || '')}";
  --box-bg: ${hexToRgba(boxData.boxColor || '#ffffff', boxData.boxColorOpacity ?? 255)};
  --triangle-color-a: ${hexToRgba(boxData.cornerColorA || '#fce647', boxData.cornerColorAOpacity ?? 255)};
  --triangle-color-b: ${hexToRgba(boxData.cornerColorB || '#fce647', boxData.cornerColorBOpacity ?? 255)};
  --text-color: ${hexToRgba(boxData.textColor || '#000000', boxData.textColorOpacity ?? 255)};
  --box-opacity: ${hasText ? 1 : 0};
}
/* OBS_TEXTBOX_INJECT_END */`;
                        }
                        
                        const newCss = css.trim() + '\n' + injectedCss;
                        
                        // Skip the OBS round-trip when nothing changed
                        if (newCss === lastAppliedCss[input.inputName]) {
                            console.log(`[OBS Sync] Quelle "${input.inputName}" unverändert, übersprungen.`);
                            continue;
                        }
                        
                        console.log(`[OBS Sync] Aktualisiere Textbox-Quelle: "${input.inputName}" (ID: ${targetId})`);
                        
                        await obs.call('SetInputSettings', {
                            inputName: input.inputName,
                            inputSettings: { css: newCss },
                            overlay: true
                        });
                        lastAppliedCss[input.inputName] = newCss;
                        updated++;
                    }
                }
            }
        }
        
        if (specificBoxId && updated === 0 && checked === 0) {
            showToast('error', `Keine Browser-Quelle mit --box-id "${specificBoxId}" gefunden`);
        } else if (specificBoxId && updated === 0) {
            showToast('error', `Keine Karte mit der ID "${specificBoxId}" im Dashboard gefunden`);
        } else if (!specificBoxId && checked === 0) {
            showToast('info', 'Keine Textbox-Quellen in OBS gefunden (brauchen --box-id im Custom CSS)');
        }
        console.log(`[OBS Sync] Push an OBS abgeschlossen. (${updated}/${checked} Quellen aktualisiert)`);
    } catch (e) {
        console.error("[OBS Sync] Fehler beim Senden an OBS:", e);
        showToast('error', 'OBS-Sync-Fehler: ' + (e.message || e));
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
    const connectionControlEl = document.getElementById('obs-connection-control');
    const statusEl = document.getElementById('storage-status');
    const labelEl = document.getElementById('storage-label');
    const btnEl = document.getElementById('btn-connect-obs');
    if (btnEl) btnEl.textContent = connected ? 'Trennen' : 'Verbinden';
    if (connectionControlEl) connectionControlEl.classList.toggle('connected', connected);
    if (connected) {
        statusEl.classList.add('connected');
        labelEl.textContent = 'Mit OBS verbunden';
        const settingsStatus = document.getElementById('settings-status-label');
        if (settingsStatus) settingsStatus.textContent = 'Status: Mit OBS verbunden';
    } else {
        statusEl.classList.remove('connected');
        labelEl.textContent = 'Nicht verbunden';
        const settingsStatus = document.getElementById('settings-status-label');
        if (settingsStatus) settingsStatus.textContent = 'Status: Nicht verbunden';
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
    const pwInput = document.getElementById('ws-password');
    if (pwInput) {
        pwInput.addEventListener('input', () => {
            dashboardState.settings = dashboardState.settings || {};
            dashboardState.settings.obsPassword = pwInput.value;
            saveStateLocally();
        });
    }

    const autoSendToggle = document.getElementById('auto-send-toggle');
    if (autoSendToggle) {
        autoSendToggle.addEventListener('change', () => {
            toggleAutoSend();
        });
    }

    const colorInputs = [
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

    colorInputs.forEach(id => {
        const element = document.getElementById(id);
        if (!element) return;

        element.addEventListener('input', () => {
            updateSettingsFromModal();
        });
    });
}

// =====================================================
//  CARD MANAGEMENT (UI)
// =====================================================
const dashboard = document.getElementById('dashboard');
const template = document.getElementById('card-template');

function addNewCard(id = 'neue_box', z1 = '', z2 = '', z3 = '', boxColor = '#ffffff', boxColorOpacity = 255, cornerColorA = '#fce647', cornerColorAOpacity = 255, cornerColorB = '#fce647', cornerColorBOpacity = 255, textColor = '#000000', textColorOpacity = 255, rainbowCorners = false) {
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

    // Rainbow corners toggle
    const rainbowToggle = cardInner.querySelector('.rainbow-toggle');
    if (rainbowToggle) {
        rainbowToggle.checked = !!rainbowCorners;
        const cornerFields = cardInner.querySelector('.corner-color-fields');
        if (cornerFields && rainbowCorners) {
            cornerFields.classList.add('rainbow-active');
        }
    }

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
        // Karten-Löschen soll die zugehörige OBS-Box ausblenden
        pushDashboardStateToOBS(id);
    }, 250);
}

/**
 * Toggles rainbow corners mode for a card.
 * Hides the static corner color pickers when active.
 */
function onRainbowToggle(checkbox) {
    const cardOuter = checkbox.closest('.card-outer');
    if (!cardOuter) return;
    const cornerFields = cardOuter.querySelector('.corner-color-fields');
    if (cornerFields) {
        cornerFields.classList.toggle('rainbow-active', checkbox.checked);
    }
    syncDOMToState();
    handleDataChange();
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
        const rainbowToggle = wrapper.querySelector('.rainbow-toggle');
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
            textColorOpacity: parseOpacityInputFromElement(wrapper.querySelector('.text-color-opacity')),
            rainbowCorners: rainbowToggle ? rainbowToggle.checked : false
        });
    });
}

/**
 * Sucht im Dashboard nach einer Karte mit der angegebenen Box-ID.
 * @param {string} boxId - Die gesuchte Box-ID
 * @returns {HTMLElement|null} - Das .card-outer Element oder null
 */
function findCardByBoxId(boxId) {
    const cards = dashboard.querySelectorAll('.card-outer');
    for (const card of cards) {
        if (card.querySelector('.id-input').value.trim() === boxId) return card;
    }
    return null;
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
                box.textColorOpacity !== undefined ? box.textColorOpacity : 255,
                box.rainbowCorners || false
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
            box.textColorOpacity !== undefined ? box.textColorOpacity : 255,
            box.rainbowCorners || false
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
    const autoSendToggle = document.getElementById('auto-send-toggle');
    if (autoSendToggle) autoSendToggle.checked = isAutoSendEnabled;
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
    renderColorProfileDropdown();
}

function parseOpacityInput(id) {
    const raw = document.getElementById(id)?.value;
    if (raw === undefined || raw === null || String(raw).trim() === '') return 255;
    const value = Number(raw);
    if (Number.isNaN(value)) return 255;
    return Math.max(0, Math.min(255, Math.round(value)));
}

function parseOpacityInputFromElement(element) {
    if (!element) return 255;
    const raw = element.value;
    if (raw === undefined || raw === null || String(raw).trim() === '') return 255;
    const value = Number(raw);
    if (Number.isNaN(value)) return 255;
    return Math.max(0, Math.min(255, Math.round(value)));
}

function hexToRgba(hex, opacity255 = 255) {
    const normalized = String(hex || '#000000').trim();
    // 3- und 6-stellige HEX-Werte (#abc, #aabbcc) — ohne #, mit oder ohne Präfix
    const match = normalized.match(/^#?([a-f\d]{3}|[a-f\d]{6})$/i);
    if (!match) return normalized;

    let hexVal = match[1];
    if (hexVal.length === 3) {
        hexVal = hexVal.split('').map(c => c + c).join('');
    }
    const intVal = parseInt(hexVal, 16);
    const r = (intVal >> 16) & 255;
    const g = (intVal >> 8) & 255;
    const b = intVal & 255;
    const numOpacity = Number(opacity255);
    const validOpacity = (opacity255 !== undefined && opacity255 !== null && !isNaN(numOpacity)) ? numOpacity : 255;
    const alpha = Math.max(0, Math.min(255, validOpacity)) / 255;

    // parseFloat räumt überflüssige Nachkommastellen ein (0.5 statt 0.500)
    const a = parseFloat(alpha.toFixed(3));
    return a >= 1 ? `rgb(${r}, ${g}, ${b})` : `rgba(${r}, ${g}, ${b}, ${a})`;
}

function sanitizeThemeSettings() {
    const defaultColors = DEFAULT_COLOR_PROFILES[0].colors;
    dashboardState.settings = dashboardState.settings || {};

    if (!dashboardState.settings.uiColors) {
        dashboardState.settings.uiColors = { ...defaultColors };
        return;
    }

    const c = dashboardState.settings.uiColors;
    const isCorrupted = (c.textColorOpacity === 0) ||
                        (c.bgPrimaryOpacity === 0 && c.textColorOpacity === 0) ||
                        (c.bgPrimary === '#000000' && c.bgPrimaryOpacity === 0 && c.panelBgOpacity === 0);

    if (isCorrupted) {
        console.warn("[Theme] Korrupte UI-Farben in Einstellungen erkannt (Deckkraft 0). Setze auf Standardfarben zurück.");
        const activeProf = findColorProfile(dashboardState.settings.activeColorProfile);
        dashboardState.settings.uiColors = activeProf ? { ...activeProf.colors } : { ...defaultColors };
        if (!activeProf) {
            dashboardState.settings.activeColorProfile = 'OBS Dark';
        }
        saveStateLocally();
    }
}

function updateSettingsFromModal() {
    dashboardState.settings = dashboardState.settings || {};
    const pwInput = document.getElementById('ws-password');
    if (pwInput) {
        dashboardState.settings.obsPassword = pwInput.value;
    }

    const currentColors = dashboardState.settings.uiColors || DEFAULT_COLOR_PROFILES[0].colors;
    const bgPrimaryEl = document.getElementById('ui-color-bg-primary');
    if (!bgPrimaryEl || !bgPrimaryEl.value) {
        return;
    }

    dashboardState.settings.uiColors = {
        bgPrimary: document.getElementById('ui-color-bg-primary')?.value || currentColors.bgPrimary || '#1C1F26',
        bgPrimaryOpacity: parseOpacityInput('ui-opacity-bg-primary'),
        bgSecondary: document.getElementById('ui-color-bg-secondary')?.value || currentColors.bgSecondary || '#323a44',
        bgSecondaryOpacity: parseOpacityInput('ui-opacity-bg-secondary'),
        panelBg: document.getElementById('ui-color-panel-bg')?.value || currentColors.panelBg || '#272A33',
        panelBgOpacity: parseOpacityInput('ui-opacity-panel-bg'),
        border: document.getElementById('ui-color-border')?.value || currentColors.border || '#3C404D',
        borderOpacity: parseOpacityInput('ui-opacity-border'),
        cardOuterBg: document.getElementById('ui-color-card-outer-bg')?.value || currentColors.cardOuterBg || '#3C404D',
        cardOuterBgOpacity: parseOpacityInput('ui-opacity-card-outer-bg'),
        panelBgAlt: document.getElementById('ui-color-panel-bg-alt')?.value || currentColors.panelBgAlt || '#414852',
        panelBgAltOpacity: parseOpacityInput('ui-opacity-panel-bg-alt'),
        btnHoverBg: document.getElementById('ui-color-btn-hover-bg')?.value || currentColors.btnHoverBg || '#414852',
        btnHoverBgOpacity: parseOpacityInput('ui-opacity-btn-hover-bg'),
        textColor: document.getElementById('ui-color-text')?.value || currentColors.textColor || '#eff0f1',
        textColorOpacity: parseOpacityInput('ui-opacity-text'),
        accentBlue: document.getElementById('ui-color-accent-blue')?.value || currentColors.accentBlue || '#3daee9',
        accentBlueOpacity: parseOpacityInput('ui-opacity-accent-blue')
    };
    applyThemeSettings();
    saveStateLocally();

    // Detect if current colors still match the active profile
    updateColorProfileDropdownState();
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

// =====================================================
//  COLOR PROFILES
// =====================================================

/**
 * Returns all profiles: defaults first, then user-created ones.
 */
function getAllColorProfiles() {
    const custom = (dashboardState.settings.colorProfiles || []).map(p => ({
        ...p,
        isDefault: false
    }));
    return [...DEFAULT_COLOR_PROFILES, ...custom];
}

/**
 * Finds a profile by name across defaults and custom profiles.
 */
function findColorProfile(name) {
    return getAllColorProfiles().find(p => p.name === name) || null;
}

/**
 * Checks if a profile name belongs to a default (non-deletable) profile.
 */
function isDefaultColorProfile(name) {
    return DEFAULT_COLOR_PROFILES.some(p => p.name === name);
}

/**
 * Renders the <select> dropdown with all available color profiles.
 */
function renderColorProfileDropdown() {
    const select = document.getElementById('color-profile-select');
    if (!select) return;

    const active = dashboardState.settings.activeColorProfile || null;
    select.innerHTML = '';

    // "Benutzerdefiniert" option (shown when colors don't match any profile)
    const customOpt = document.createElement('option');
    customOpt.value = '__custom__';
    customOpt.textContent = '✎ Benutzerdefiniert';
    select.appendChild(customOpt);

    // Default profiles group
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

    // Custom profiles group (only if there are any)
    const customProfiles = dashboardState.settings.colorProfiles || [];
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

    // If no profile is active (custom colors), select the custom option
    if (!active) {
        customOpt.selected = true;
    }

    updateDeleteButtonState();
}

/**
 * Called when the user selects a profile from the dropdown.
 */
function onColorProfileSelected() {
    const select = document.getElementById('color-profile-select');
    const selectedName = select.value;

    if (selectedName === '__custom__') {
        dashboardState.settings.activeColorProfile = null;
        updateDeleteButtonState();
        saveStateLocally();
        return;
    }

    applyColorProfile(selectedName);
}

/**
 * Applies a color profile's values to the UI color pickers and live theme.
 */
function applyColorProfile(profileName) {
    const profile = findColorProfile(profileName);
    if (!profile) {
        showToast('error', `Profil "${profileName}" nicht gefunden`);
        return;
    }

    console.log(`[Farbprofile] Wende Profil an: "${profileName}"`);

    const c = profile.colors;

    // Update the color picker inputs
    document.getElementById('ui-color-bg-primary').value = c.bgPrimary || '#1C1F26';
    document.getElementById('ui-opacity-bg-primary').value = c.bgPrimaryOpacity ?? 255;
    document.getElementById('ui-color-bg-secondary').value = c.bgSecondary || '#323a44';
    document.getElementById('ui-opacity-bg-secondary').value = c.bgSecondaryOpacity ?? 255;
    document.getElementById('ui-color-panel-bg').value = c.panelBg || '#272A33';
    document.getElementById('ui-opacity-panel-bg').value = c.panelBgOpacity ?? 255;
    document.getElementById('ui-color-border').value = c.border || '#3C404D';
    document.getElementById('ui-opacity-border').value = c.borderOpacity ?? 255;
    document.getElementById('ui-color-card-outer-bg').value = c.cardOuterBg || '#3C404D';
    document.getElementById('ui-opacity-card-outer-bg').value = c.cardOuterBgOpacity ?? 255;
    document.getElementById('ui-color-panel-bg-alt').value = c.panelBgAlt || '#414852';
    document.getElementById('ui-opacity-panel-bg-alt').value = c.panelBgAltOpacity ?? 255;
    document.getElementById('ui-color-btn-hover-bg').value = c.btnHoverBg || '#414852';
    document.getElementById('ui-opacity-btn-hover-bg').value = c.btnHoverBgOpacity ?? 255;
    document.getElementById('ui-color-text').value = c.textColor || '#eff0f1';
    document.getElementById('ui-opacity-text').value = c.textColorOpacity ?? 255;
    document.getElementById('ui-color-accent-blue').value = c.accentBlue || '#3daee9';
    document.getElementById('ui-opacity-accent-blue').value = c.accentBlueOpacity ?? 255;

    // Update state
    dashboardState.settings.uiColors = { ...c };
    dashboardState.settings.activeColorProfile = profileName;

    applyThemeSettings();
    saveStateLocally();
    updateDeleteButtonState();

    showToast('info', `Farbprofil "${profileName}" angewendet`);
}

/**
 * Checks if the current colors match the active profile.
 * If not, switches the dropdown to "Benutzerdefiniert".
 */
function updateColorProfileDropdownState() {
    const active = dashboardState.settings.activeColorProfile;
    if (!active) return; // Already in custom mode

    const profile = findColorProfile(active);
    if (!profile) {
        dashboardState.settings.activeColorProfile = null;
        renderColorProfileDropdown();
        return;
    }

    const current = dashboardState.settings.uiColors || {};
    const profileColors = profile.colors;
    const colorKeys = Object.keys(profileColors);

    const matches = colorKeys.every(key => {
        const currentVal = current[key];
        const profileVal = profileColors[key];
        // Compare normalized: lowercase hex strings and numeric opacity
        if (typeof profileVal === 'number') {
            return Number(currentVal) === profileVal;
        }
        return (currentVal || '').toLowerCase() === (profileVal || '').toLowerCase();
    });

    if (!matches) {
        dashboardState.settings.activeColorProfile = null;
        const select = document.getElementById('color-profile-select');
        if (select) select.value = '__custom__';
        updateDeleteButtonState();
    }
}

/**
 * Enables/disables the delete button based on whether the selected profile is a default.
 */
function updateDeleteButtonState() {
    const select = document.getElementById('color-profile-select');
    const deleteBtn = document.getElementById('btn-delete-color-profile');
    if (!select || !deleteBtn) return;

    const selected = select.value;
    const isDefault = isDefaultColorProfile(selected);
    const isCustomMode = selected === '__custom__';

    deleteBtn.disabled = isDefault || isCustomMode;
    deleteBtn.title = isDefault
        ? 'Standard-Profile können nicht gelöscht werden'
        : isCustomMode
            ? 'Kein Profil ausgewählt'
            : 'Gewähltes Farbprofil löschen';
}

/**
 * Toggles the inline save form visibility.
 */
function toggleColorProfileSaveForm() {
    const form = document.getElementById('color-profile-save-form');
    if (!form) return;
    const isHidden = form.hidden;
    form.hidden = !isHidden;
    if (!isHidden) return;
    const nameInput = document.getElementById('color-profile-name-input');
    if (nameInput) {
        nameInput.value = '';
        nameInput.focus();
    }
}

/**
 * Saves the current UI colors as a new custom color profile.
 */
function saveAsColorProfile() {
    const nameInput = document.getElementById('color-profile-name-input');
    const name = (nameInput?.value || '').trim();

    if (!name) {
        showToast('error', 'Bitte einen Profilnamen eingeben');
        return;
    }

    if (isDefaultColorProfile(name)) {
        showToast('error', `"${name}" ist ein Standard-Profil und kann nicht überschrieben werden`);
        return;
    }

    console.log(`[Farbprofile] Speichere neues Profil: "${name}"`);

    // Read current colors from state
    updateSettingsFromModal();
    const colors = JSON.parse(JSON.stringify(dashboardState.settings.uiColors));

    if (!dashboardState.settings.colorProfiles) {
        dashboardState.settings.colorProfiles = [];
    }

    // Check if a custom profile with this name already exists
    const existingIdx = dashboardState.settings.colorProfiles.findIndex(p => p.name === name);
    const profileObj = { name, colors };

    if (existingIdx >= 0) {
        dashboardState.settings.colorProfiles[existingIdx] = profileObj;
    } else {
        dashboardState.settings.colorProfiles.push(profileObj);
    }

    dashboardState.settings.activeColorProfile = name;
    saveStateLocally();
    renderColorProfileDropdown();

    // Select the new profile in the dropdown
    const select = document.getElementById('color-profile-select');
    if (select) select.value = name;

    // Hide the save form
    const form = document.getElementById('color-profile-save-form');
    if (form) form.hidden = true;

    showToast('success', `Farbprofil "${name}" gespeichert`);
}

/**
 * Deletes the currently selected custom color profile.
 */
function deleteSelectedColorProfile() {
    const select = document.getElementById('color-profile-select');
    if (!select) return;

    const selectedName = select.value;

    if (selectedName === '__custom__') {
        showToast('error', 'Kein Profil zum Löschen ausgewählt');
        return;
    }

    if (isDefaultColorProfile(selectedName)) {
        showToast('error', 'Standard-Profile können nicht gelöscht werden');
        return;
    }

    console.log(`[Farbprofile] Lösche Profil: "${selectedName}"`);

    dashboardState.settings.colorProfiles = (dashboardState.settings.colorProfiles || [])
        .filter(p => p.name !== selectedName);

    // If the deleted profile was active, switch to custom
    if (dashboardState.settings.activeColorProfile === selectedName) {
        dashboardState.settings.activeColorProfile = null;
    }

    saveStateLocally();
    renderColorProfileDropdown();
    showToast('info', `Farbprofil "${selectedName}" gelöscht`);
}

// =====================================================
//  YAML EXPORT / IMPORT
// =====================================================

/**
 * Converts a color profile object to a simple YAML string.
 */
function colorProfileToYAML(profile) {
    let yaml = `name: "${profile.name}"\n`;
    yaml += `colors:\n`;
    const c = profile.colors;
    const keys = [
        'bgPrimary', 'bgPrimaryOpacity',
        'bgSecondary', 'bgSecondaryOpacity',
        'panelBg', 'panelBgOpacity',
        'border', 'borderOpacity',
        'cardOuterBg', 'cardOuterBgOpacity',
        'panelBgAlt', 'panelBgAltOpacity',
        'btnHoverBg', 'btnHoverBgOpacity',
        'textColor', 'textColorOpacity',
        'accentBlue', 'accentBlueOpacity'
    ];
    for (const key of keys) {
        const val = c[key];
        if (typeof val === 'number') {
            yaml += `  ${key}: ${val}\n`;
        } else {
            yaml += `  ${key}: "${val}"\n`;
        }
    }
    return yaml;
}

/**
 * Parses a simple YAML string back into a color profile object.
 */
function yamlToColorProfile(yamlString) {
    const lines = yamlString.split('\n');
    const profile = { name: '', colors: {} };
    let inColors = false;

    for (const rawLine of lines) {
        const line = rawLine.trim();
        if (!line || line.startsWith('#')) continue;

        if (line.startsWith('name:')) {
            const val = line.substring(5).trim();
            profile.name = val.replace(/^["']|["']$/g, '');
            continue;
        }

        if (line === 'colors:') {
            inColors = true;
            continue;
        }

        if (inColors && line.includes(':')) {
            const colonIdx = line.indexOf(':');
            const key = line.substring(0, colonIdx).trim();
            let val = line.substring(colonIdx + 1).trim();
            val = val.replace(/^["']|["']$/g, '');

            // Detect numeric values (opacity)
            if (/^\d+$/.test(val)) {
                profile.colors[key] = parseInt(val, 10);
            } else {
                profile.colors[key] = val;
            }
        }
    }

    return profile;
}

/**
 * Exports the currently selected color profile as a YAML file download.
 */
function exportColorProfileYAML() {
    const select = document.getElementById('color-profile-select');
    if (!select) return;

    const selectedName = select.value;
    let profile;

    if (selectedName === '__custom__') {
        // Export current custom colors
        updateSettingsFromModal();
        profile = {
            name: 'Benutzerdefiniert',
            colors: JSON.parse(JSON.stringify(dashboardState.settings.uiColors))
        };
    } else {
        profile = findColorProfile(selectedName);
        if (!profile) {
            showToast('error', `Profil "${selectedName}" nicht gefunden`);
            return;
        }
    }

    const yaml = colorProfileToYAML(profile);
    const blob = new Blob([yaml], { type: 'text/yaml' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `farbprofil_${profile.name.replace(/[^a-zA-Z0-9äöüÄÖÜß_-]/g, '_')}.yaml`;
    a.click();
    URL.revokeObjectURL(url);

    console.log(`[Farbprofile] Profil "${profile.name}" als YAML exportiert.`);
    showToast('success', `Profil "${profile.name}" exportiert`);
}

/**
 * Imports a color profile from a YAML file.
 */
function importColorProfileYAML() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.yaml,.yml';
    input.onchange = async (e) => {
        const file = e.target.files[0];
        if (!file) return;

        try {
            const text = await file.text();
            const profile = yamlToColorProfile(text);

            if (!profile.name) {
                showToast('error', 'YAML enthält keinen Profilnamen');
                return;
            }

            if (!profile.colors || Object.keys(profile.colors).length === 0) {
                showToast('error', 'YAML enthält keine Farbwerte');
                return;
            }

            // Prevent overwriting default profiles
            if (isDefaultColorProfile(profile.name)) {
                profile.name = profile.name + ' (Import)';
            }

            console.log(`[Farbprofile] Importiere Profil: "${profile.name}" aus ${file.name}`);

            if (!dashboardState.settings.colorProfiles) {
                dashboardState.settings.colorProfiles = [];
            }

            // Replace existing or add new
            const existingIdx = dashboardState.settings.colorProfiles.findIndex(p => p.name === profile.name);
            if (existingIdx >= 0) {
                dashboardState.settings.colorProfiles[existingIdx] = profile;
            } else {
                dashboardState.settings.colorProfiles.push(profile);
            }

            // Apply the imported profile immediately
            dashboardState.settings.activeColorProfile = profile.name;
            dashboardState.settings.uiColors = { ...profile.colors };
            loadSettingsToModal();
            saveStateLocally();
            renderColorProfileDropdown();

            // Select the imported profile
            const select = document.getElementById('color-profile-select');
            if (select) select.value = profile.name;

            showToast('success', `Farbprofil "${profile.name}" importiert`);
        } catch (err) {
            showToast('error', 'Fehler beim Importieren der YAML-Datei');
            console.error('[Farbprofile] Import-Fehler:', err);
        }
    };
    input.click();
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
    const profilesBar = document.getElementById('profiles-bar');
    if (profilesBar) {
        profilesBar.scrollTo({ left: profilesBar.scrollWidth, behavior: 'smooth' });
    }
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
    document.querySelectorAll('.profile-chip.custom, .delete-profile.custom').forEach(el => el.remove());

    const anchor = document.getElementById('custom-profiles-anchor');
    if (!dashboardState.profiles) return;

    dashboardState.profiles.forEach((profile, idx) => {
        const chip = document.createElement('button');
        chip.type = 'button';
        chip.className = 'profile-chip custom';
        chip.innerHTML = `
            <span class="chip-icon">📋</span>
            ${escapeHtml(profile.name)}
        `;
        chip.onclick = () => applyProfileData(profile);

        const deleteButton = document.createElement('button');
        deleteButton.type = 'button';
        deleteButton.className = 'delete-profile custom';
        deleteButton.title = 'Profil löschen';
        deleteButton.textContent = '✕';
        deleteButton.onclick = (event) => {
            event.stopPropagation();
            deleteCustomProfile(idx);
        };

        anchor.parentNode.insertBefore(chip, anchor);
        anchor.parentNode.insertBefore(deleteButton, anchor);
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
    // Color profile save form: Enter to save, Escape to close (works even if the
    // element is currently hidden, so no focus is required)
    const cpForm = document.getElementById('color-profile-save-form');
    if (cpForm && !cpForm.hidden) {
        if (e.key === 'Enter') {
            saveAsColorProfile();
            e.preventDefault();
        }
        if (e.key === 'Escape') {
            toggleColorProfileSaveForm();
            e.preventDefault();
            return;
        }
    }
    if (e.key === 'Escape') {
        const aiModal = document.getElementById('ai-import-modal');
        if (aiModal && !aiModal.hidden) {
            closeAIImportModal();
            return;
        }
        const settingsModal = document.getElementById('settings-modal');
        if (settingsModal && !settingsModal.hidden) {
            closeSettingsModal();
            return;
        }
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
        console.log(`[Init] Auto-Send Einstellung geladen: ${isAutoSendEnabled}`);
    }

    const autoSendToggle = document.getElementById('auto-send-toggle');
    if (autoSendToggle) autoSendToggle.checked = isAutoSendEnabled;
    
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

    // Auto-heal corrupted UI colors (e.g. from previous empty-modal bug)
    sanitizeThemeSettings();

    renderCardsFromState();
    applyThemeSettings();
    renderColorProfileDropdown();
    loadSettingsToModal();
    registerSettingsInputListeners();
    updateConnectionUI(false);
    
    // Horizontal wheel scroll on Schnell-Profile bar
    const profilesRow = document.querySelector('.header-profiles-row');
    const profilesBar = document.getElementById('profiles-bar');
    if (profilesBar && profilesRow) {
        profilesRow.addEventListener('wheel', (e) => {
            if (e.deltaY !== 0 && e.deltaX === 0 && profilesBar.scrollWidth > profilesBar.clientWidth) {
                e.preventDefault();
                profilesBar.scrollLeft += e.deltaY;
            }
        }, { passive: false });
    }
    
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
