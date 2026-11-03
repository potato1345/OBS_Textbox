/**
 * @file storage.js
 * Verwaltet den Zustand des Dashboards im localStorage inklusive
 * automatischer Daten-Migration aus älteren Versionen.
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Zusammenspiel:
 *   - Verwaltet den globalen Single Source of Truth (Boxen, Profile, Einstellungen).
 *   - Benachrichtigt Subscriber über EventTarget bei Zustandsänderungen.
 *   - Migriert Einstellungen und Boxen aus der v1-Installation automatisch nach v2.
 */

import { DEFAULT_COLOR_PROFILES } from './color-profiles.js';
import { LP_DEBUG } from './box-utils.js';

/** Schlüssel für den v2-Zustand im localStorage des Browsers. */
const STORAGE_KEY = 'obs_textbox_state_v2';

/** Veralteter Schlüssel für den v1-Zustand zur Migration. */
const LEGACY_STORAGE_KEY = 'obs_textbox_state';

/**
 * Standardzustand für neue Dashboard-Installationen.
 */
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

/**
 * Registriert einen Listener für Zustandsänderungen (CustomEvent mit detail: { state, action }).
 *
 * @param {EventListener} listener - Callback-Funktion.
 * @returns {function(): void} Funktion zum Abbestellen des Listeners.
 */
export function onStateChange(listener) {
    eventTarget.addEventListener('change', listener);
    return () => eventTarget.removeEventListener('change', listener);
}

/**
 * Löst das change-Event auf dem internen EventTarget aus.
 *
 * @param {string} [action='update'] - Bezeichner der ausgeführten Mutation.
 */
function notifyChange(action = 'update') {
    eventTarget.dispatchEvent(new CustomEvent('change', { detail: { state: currentState, action } }));
}

/**
 * Überprüft und migriert ggf. vorhandene Daten aus v1 localStorage-Schlüsseln.
 *
 * @param {object} state - Zu aktualisierendes Zustandsobjekt.
 * @returns {boolean} True, wenn mindestens ein Legacy-Wert übernommen wurde.
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
 *
 * @param {object} colors - Zu prüfende Farbkonfiguration.
 * @returns {object} Bereinigte Farben oder Standard-Theme-Farben bei Korruption.
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
 * Lädt den Zustand aus dem localStorage oder initialisiert ihn mit Standardwerten.
 * Führt bei Bedarf eine Datenmigration aus Version 1 durch.
 *
 * @returns {object} Der geladene Gesamtzustand.
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
    if (LP_DEBUG) console.debug('[liedplan] Zustand geladen, Boxen:', currentState.boxes?.length);
    return currentState;
}

/**
 * Schreibt den aktuellen Zustand persistent in den localStorage.
 */
export function saveState() {
    if (!currentState) return;
    try {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(currentState));
        if (LP_DEBUG) console.debug('[liedplan] Zustand persistent gespeichert');
    } catch (err) {
        console.error('[Storage] Speichern fehlgeschlagen:', err);
    }
}

/**
 * Gibt den aktuellen Zustand zurück; lädt ihn bei Bedarf, falls noch nicht initialisiert.
 *
 * @returns {object} Der aktuelle Gesamtzustand.
 */
export function getState() {
    if (!currentState) {
        loadState();
    }
    return currentState;
}

/**
 * Mutiert den Zustand atomar über eine Mutator-Funktion, speichert ihn und benachrichtigt Listener.
 *
 * @param {function(object): void} mutator - Funktion, die Änderungen am Zustandsobjekt vornimmt.
 * @param {string} [actionName='update'] - Name der Aktion für Debugging und Event-Details.
 */
export function updateState(mutator, actionName = 'update') {
    if (LP_DEBUG) console.debug('[liedplan] updateState Aktion:', actionName);
    const state = getState();
    mutator(state);
    saveState();
    notifyChange(actionName);
}
