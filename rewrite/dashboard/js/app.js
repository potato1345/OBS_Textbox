/**
 * @file app.js
 * Haupt-Einstiegspunkt für das OBS Textbox Dashboard (Version 2, rewrite/).
 *
 * Zweck:
 *   - Initialisiert den Anwendungszustand (State), Themes und DOM-Komponenten.
 *   - Verdrahtet globale Toolbar-Aktionen, Tastaturkürzel und Dialoge.
 *   - Startet die OBS WebSocket Verbindung (obs-sync.js) bei hinterlegtem Passwort.
 *
 * Zusammenspiel:
 *   - Dient als Einstiegsmodul für index.html (<script type="module" src="js/app.js">).
 *   - Koordiniert storage.js, cards.js, modals.js, color-profiles.js und obs-sync.js.
 */

import { LP_DEBUG } from './box-utils.js';
import { getState, loadState, updateState } from './storage.js';
import { applyThemeVariables } from './color-profiles.js';
import { renderDashboardCards, addNewBox, clearAllCards } from './cards.js';
import {
    initSettingsModal,
    initSaveProfileModal,
    initImportModal,
    openDialog,
    closeDialog,
    loadSettingsFields,
    renderQuickProfilesBar,
    updateConnectionStatusLabel
} from './modals.js';
import { connectOBS, toggleConnection, onConnectionChange, obs } from './obs-sync.js';
import { showToast } from './toast.js';

/**
 * Exportiert die gesamte Dashboard-Konfiguration (Karten, Profile, Einstellungen) als JSON-Datei.
 */
function exportFullJson() {
    const state = getState();
    const json = JSON.stringify(state, null, 2);
    const blob = new Blob([json], { type: 'application/json;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `obs_textbox_backup_${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
    if (LP_DEBUG) console.debug('[App] Konfigurations-Backup exportiert');
    showToast('success', 'Backup-JSON exportiert');
}

/**
 * Importiert eine vollständige Konfigurations-JSON-Datei über einen Datei-Öffnen-Dialog.
 */
function importFullJson() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json';
    input.onchange = async () => {
        const file = input.files?.[0];
        if (!file) return;
        try {
            const text = await file.text();
            const data = JSON.parse(text);
            if (!data || (!data.boxes && !data.settings)) {
                throw new Error('Datei enthält keine gültige OBS-Textbox-Konfiguration.');
            }
            updateState(state => {
                if (Array.isArray(data.boxes)) state.boxes = data.boxes;
                if (Array.isArray(data.profiles)) state.profiles = data.profiles;
                if (data.settings) state.settings = { ...state.settings, ...data.settings };
            }, 'import_full');

            applyThemeVariables(getState().settings.uiColors);
            renderDashboardCards(document.getElementById('dashboard'));
            renderQuickProfilesBar();
            loadSettingsFields();
            if (LP_DEBUG) console.debug('[App] Konfiguration erfolgreich importiert aus:', file.name);
            showToast('success', `Konfiguration aus ${file.name} geladen`);
        } catch (err) {
            showToast('error', `Import-Fehler: ${err.message}`);
        }
    };
    input.click();
}

/**
 * Aktualisiert die visuelle Verbindungsanzeige in Header und Statusleiste.
 * @param {boolean} connected Ob die WebSocket-Verbindung aktiv ist
 */
function updateHeaderConnectionUi(connected) {
    const statusWrap = document.getElementById('header-obs-status');
    const statusLabel = document.getElementById('header-obs-label');
    const connectBtn = document.getElementById('btn-header-connect');

    if (statusWrap) {
        statusWrap.classList.toggle('is-connected', connected);
    }
    if (statusLabel) {
        statusLabel.textContent = connected ? 'Mit OBS verbunden' : 'Nicht verbunden';
    }
    if (connectBtn) {
        connectBtn.textContent = connected ? 'Trennen' : 'Verbinden';
    }
    updateConnectionStatusLabel(connected);
}

/**
 * Registriert Event-Listener für Header- und Toolbar-Schaltflächen.
 */
function initToolbar() {
    // Buttons im Header & Toolbar
    document.getElementById('btn-header-connect')?.addEventListener('click', () => toggleConnection());
    document.getElementById('btn-open-settings')?.addEventListener('click', () => {
        loadSettingsFields();
        openDialog('settings-dialog');
    });
    document.getElementById('btn-export-json')?.addEventListener('click', exportFullJson);
    document.getElementById('btn-import-json')?.addEventListener('click', importFullJson);

    document.getElementById('btn-add-box')?.addEventListener('click', () => addNewBox());
    document.getElementById('btn-clear-all')?.addEventListener('click', () => clearAllCards());
    document.getElementById('btn-open-import')?.addEventListener('click', () => openDialog('import-dialog'));

    document.getElementById('btn-open-save-profile')?.addEventListener('click', () => {
        const inp = document.getElementById('input-quick-profile-name');
        if (inp) inp.value = '';
        openDialog('save-profile-dialog');
    });

    // Horizontales Scrollen per Mausrad in der Profil-Leiste
    const profileScroll = document.getElementById('quick-profiles-wrapper');
    profileScroll?.addEventListener('wheel', (e) => {
        if (e.deltaY !== 0 && profileScroll.scrollWidth > profileScroll.clientWidth) {
            e.preventDefault();
            profileScroll.scrollLeft += e.deltaY;
        }
    }, { passive: false });
}

/**
 * Registriert globale Tastaturkürzel (z. B. Escape zum Schließen offener Modale).
 */
function initKeyboardShortcuts() {
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            ['settings-dialog', 'save-profile-dialog', 'import-dialog'].forEach(id => {
                closeDialog(id);
            });
        }
    });
}

/**
 * Hauptinitialisierung beim Start der Anwendung.
 */
async function start() {
    if (LP_DEBUG) console.debug('[App] Starte OBS Textbox Dashboard v2 (Debug aktiv)...');
    console.log('[OBS Textbox v2] Starte Dashboard...');

    const state = loadState();
    if (LP_DEBUG) console.debug('[App] Geladener State:', state.boxes.length, 'Boxen,', (state.profiles || []).length, 'Profile');
    applyThemeVariables(state.settings.uiColors);

    const dashboard = document.getElementById('dashboard');
    renderDashboardCards(dashboard);
    renderQuickProfilesBar();

    initSettingsModal();
    initSaveProfileModal();
    initImportModal();
    initToolbar();
    initKeyboardShortcuts();

    onConnectionChange(connected => {
        updateHeaderConnectionUi(connected);
    });

    // Automatische Verbindung wenn Passwort vorhanden
    if (state.settings.obsPassword) {
        if (LP_DEBUG) console.debug('[App] Gespeichertes OBS-Passwort vorhanden, starte Autoconnect...');
        connectOBS(false);
    }

    console.log('[OBS Textbox v2] Bereit.');
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
} else {
    start();
}
