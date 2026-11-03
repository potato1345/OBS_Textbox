/**
 * @file storage.js
 * Verwaltet den State des Dashboards im localStorage inklusive
 * automatischer Daten-Migration aus älteren Versionen.
 */

import { DEFAULT_COLOR_PROFILES } from './color-profiles.js';

const STORAGE_KEY = 'obs_textbox_state_v2';
const LEGACY_STORAGE_KEY = 'obs_textbox_state';

export const DEFAULT_STATE = {
    version: 2,
    autoSend: true,
    settings: {
        obsPassword: '',
        openRouterApiKey: '',
        systemPrompt: '',
        activeColorProfile: 'OBS Dark',
        colorProfiles: [],
        uiColors: { ...DEFAULT_COLOR_PROFILES[0].colors }
    },
    profiles: [],
    boxes: [
        {
            id: 'lowerthird',
            zeile1: '',
            zeile2: '',
            zeile3: '',
            boxColor: '#ffffff',
            boxColorOpacity: 255,
            cornerColorA: '#fce647',
            cornerColorAOpacity: 255,
            cornerColorB: '#fce647',
            cornerColorBOpacity: 255,
            textColor: '#000000',
            textColorOpacity: 255,
            rainbowCorners: false,
            animFile: '',
            showAnim: false,
            animScale: 3,
            animX: 0,
            animY: 0
        },
        {
            id: 'info',
            zeile1: '',
            zeile2: '',
            zeile3: '',
            boxColor: '#ffffff',
            boxColorOpacity: 255,
            cornerColorA: '#fce647',
            cornerColorAOpacity: 255,
            cornerColorB: '#fce647',
            cornerColorBOpacity: 255,
            textColor: '#000000',
            textColorOpacity: 255,
            rainbowCorners: false,
            animFile: '',
            showAnim: false,
            animScale: 3,
            animX: 0,
            animY: 0
        }
    ]
};

let currentState = null;
const eventTarget = new EventTarget();

export function onStateChange(listener) {
    eventTarget.addEventListener('change', listener);
    return () => eventTarget.removeEventListener('change', listener);
}

function notifyChange(action = 'update') {
    eventTarget.dispatchEvent(new CustomEvent('change', { detail: { state: currentState, action } }));
}

/**
 * Überprüft und migriert ggf. vorhandene Daten aus v1 localStorage-Schlüsseln.
 */
function migrateLegacyData(state) {
    let modified = false;

    // Altes Passwort
    const oldPw = localStorage.getItem('obs_ws_pw');
    if (oldPw && !state.settings.obsPassword) {
        state.settings.obsPassword = oldPw;
        modified = true;
    }

    // Alter Auto-Send-Schalter
    const oldAuto = localStorage.getItem('obs_auto_send');
    if (oldAuto !== null) {
        state.autoSend = oldAuto === 'true';
        modified = true;
    }

    // Alter OpenRouter API Key
    const oldKey = localStorage.getItem('openrouter_api_key');
    if (oldKey && !state.settings.openRouterApiKey) {
        state.settings.openRouterApiKey = oldKey;
        modified = true;
    }

    // Alter System Prompt
    const oldPrompt = localStorage.getItem('ai_system_prompt');
    if (oldPrompt && !state.settings.systemPrompt) {
        state.settings.systemPrompt = oldPrompt;
        modified = true;
    }

    // Alter Gesamt-State
    const oldRaw = localStorage.getItem(LEGACY_STORAGE_KEY);
    if (oldRaw) {
        try {
            const oldData = JSON.parse(oldRaw);
            if (oldData.boxes && Array.isArray(oldData.boxes) && (!state.boxes || state.boxes.length === 0)) {
                state.boxes = oldData.boxes;
                modified = true;
            }
            if (oldData.profiles && Array.isArray(oldData.profiles) && state.profiles.length === 0) {
                state.profiles = oldData.profiles;
                modified = true;
            }
            if (oldData.settings) {
                if (oldData.settings.colorProfiles && Array.isArray(oldData.settings.colorProfiles)) {
                    state.settings.colorProfiles = oldData.settings.colorProfiles;
                }
                if (oldData.settings.activeColorProfile) {
                    state.settings.activeColorProfile = oldData.settings.activeColorProfile;
                }
                if (oldData.settings.uiColors) {
                    state.settings.uiColors = { ...state.settings.uiColors, ...oldData.settings.uiColors };
                }
                modified = true;
            }
        } catch (e) {
            console.warn('[Storage] Legacy-Daten konnten nicht geparst werden:', e);
        }
    }

    return modified;
}

/**
 * Bereinigt fehlerhafte Farbwerte (z.B. Opacity 0 aus alten Dashboard-Bugs).
 */
function sanitizeColors(colors) {
    const defaultColors = DEFAULT_COLOR_PROFILES[0].colors;
    if (!colors) return { ...defaultColors };

    const isCorrupted = (
        colors.textColorOpacity === 0 ||
        (colors.bgPrimaryOpacity === 0 && colors.textColorOpacity === 0) ||
        (colors.bgPrimary === '#000000' && colors.bgPrimaryOpacity === 0 && colors.panelBgOpacity === 0)
    );

    if (isCorrupted) {
        return { ...defaultColors };
    }
    return colors;
}

/**
 * Lädt den State aus dem localStorage oder initialisiert ihn.
 */
export function loadState() {
    try {
        const raw = localStorage.getItem(STORAGE_KEY);
        if (raw) {
            const parsed = JSON.parse(raw);
            currentState = {
                ...DEFAULT_STATE,
                ...parsed,
                settings: {
                    ...DEFAULT_STATE.settings,
                    ...(parsed.settings || {})
                }
            };
        } else {
            currentState = JSON.parse(JSON.stringify(DEFAULT_STATE));
            migrateLegacyData(currentState);
            saveState();
        }
    } catch (err) {
        console.warn('[Storage] Fehler beim Laden, setze Default-State:', err);
        currentState = JSON.parse(JSON.stringify(DEFAULT_STATE));
    }

    currentState.settings.uiColors = sanitizeColors(currentState.settings.uiColors);
    return currentState;
}

/**
 * Schreibt den aktuellen State in den localStorage.
 */
export function saveState() {
    if (!currentState) return;
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(currentState));
    } catch (err) {
        console.error('[Storage] Speichern fehlgeschlagen:', err);
    }
}

export function getState() {
    if (!currentState) {
        loadState();
    }
    return currentState;
}

export function updateState(mutator, actionName = 'update') {
    const state = getState();
    mutator(state);
    saveState();
    notifyChange(actionName);
}
