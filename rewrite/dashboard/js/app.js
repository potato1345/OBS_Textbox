/**
 * @file app.js
 * Haupt-Einstiegspunkt für das OBS Textbox Dashboard (ES-Module).
 */

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
    showToast('success', 'Backup-JSON exportiert');
}

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
            showToast('success', `Konfiguration aus ${file.name} geladen`);
        } catch (err) {
            showToast('error', `Import-Fehler: ${err.message}`);
        }
    };
    input.click();
}

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

function initKeyboardShortcuts() {
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape') {
            ['settings-dialog', 'save-profile-dialog', 'import-dialog'].forEach(id => {
                closeDialog(id);
            });
        }
    });
}

async function start() {
    console.log('[OBS Textbox v2] Starte Dashboard...');

    const state = loadState();
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
        connectOBS(false);
    }

    console.log('[OBS Textbox v2] Bereit.');
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', start);
} else {
    start();
}
