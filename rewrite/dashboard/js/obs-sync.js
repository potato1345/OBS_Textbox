/**
 * @file obs-sync.js
 * Synchronisiert den Dashboard-Zustand mit den Browser-Quellen in OBS Studio
 * über die OBS WebSocket v5 Schnittstelle.
 */

import { OBSWebSocketClient } from './obs-ws.js';
import { buildInjectedCss, applyInjectedCss } from './css-builder.js';
import { getState, updateState } from './storage.js';
import { showToast } from './toast.js';

export const obs = new OBSWebSocketClient();

const lastAppliedCss = new Map();
let reconnectTimer = null;
let suppressReconnect = false;
let isSyncing = false;

// Event-Callbacks für UI
const connectionListeners = new Set();

export function onConnectionChange(fn) {
    connectionListeners.add(fn);
    return () => connectionListeners.delete(fn);
}

function notifyConnection(connected) {
    for (const listener of connectionListeners) {
        try {
            listener(connected);
        } catch (e) {
            console.error('[OBS Sync] Listener-Fehler:', e);
        }
    }
}

obs.onConnect = () => {
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    notifyConnection(true);
    showToast('success', 'Mit OBS WebSocket verbunden');
    pushStateToOBS();
};

obs.onDisconnect = () => {
    notifyConnection(false);
    if (!suppressReconnect) {
        scheduleReconnect();
    }
};

obs.onError = (err) => {
    notifyConnection(false);
    console.warn('[OBS Sync] Verbindungsfehler:', err.message);
};

function scheduleReconnect() {
    if (reconnectTimer || suppressReconnect || obs.isConnected) return;
    const state = getState();
    if (!state.settings.obsPassword && !localStorage.getItem('obs_ws_pw')) {
        // Kein Passwort konfiguriert -> kein automatischer Dauer-Reconnect
        return;
    }

    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        if (!obs.isConnected && !suppressReconnect) {
            connectOBS(false);
        }
    }, 3500);
}

export async function connectOBS(showError = true) {
    suppressReconnect = false;
    const state = getState();
    const pw = state.settings.obsPassword || '';

    try {
        await obs.connect(pw);
    } catch (err) {
        if (showError) {
            showToast('error', `Verbindung fehlgeschlagen: ${err.message}`);
        }
        scheduleReconnect();
    }
}

export function disconnectOBS() {
    suppressReconnect = true;
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }
    obs.disconnect();
    notifyConnection(false);
    showToast('info', 'OBS-Verbindung getrennt');
}

export function toggleConnection() {
    if (obs.isConnected) {
        disconnectOBS();
    } else {
        connectOBS(true);
    }
}

/**
 * Pusht die CSS-Variablen der konfigurierten Boxen in alle passenden Browser-Quellen von OBS.
 * @param {string|null} specificBoxId Falls gesetzt, wird nur diese Box aktualisiert.
 */
export async function pushStateToOBS(specificBoxId = null) {
    if (!obs.isConnected) {
        return { updated: 0, checked: 0, connected: false };
    }
    if (isSyncing) return;
    isSyncing = true;

    const state = getState();
    let updated = 0;
    let checked = 0;

    try {
        const { inputs = [] } = await obs.call('GetInputList', { inputKind: 'browser_source' });

        for (const input of inputs) {
            const { inputSettings } = await obs.call('GetInputSettings', { inputName: input.inputName });
            const currentCss = inputSettings?.css || '';

            const match = currentCss.match(/--box-id:\s*["']([^"']+)["']/);
            if (!match) continue;

            const targetId = match[1];
            checked++;

            if (specificBoxId && specificBoxId !== targetId) continue;

            const boxData = state.boxes.find(b => b.id === targetId);
            if (!boxData) {
                // Wenn die Box im Dashboard gelöscht wurde, blenden wir sie in OBS aus (Opacity 0)
                const emptyBlock = buildInjectedCss({ id: targetId, zeile1: '', zeile2: '', zeile3: '' });
                const newCss = applyInjectedCss(currentCss, emptyBlock);

                // Auch gegen currentCss vergleichen: nach einem Reload ist der
                // lastAppliedCss-Cache leer – SetInputSettings nur, wenn sich etwas
                // gegenüber dem bereits in OBS gespeicherten CSS wirklich ändert.
                if (newCss !== lastAppliedCss.get(input.inputName) && newCss !== currentCss) {
                    await obs.call('SetInputSettings', {
                        inputName: input.inputName,
                        inputSettings: { css: newCss },
                        overlay: true
                    });
                    lastAppliedCss.set(input.inputName, newCss);
                    updated++;
                }
                continue;
            }

            const injectedBlock = buildInjectedCss(boxData);
            const newCss = applyInjectedCss(currentCss, injectedBlock);

            // Verhindert unnötiges Neuladen der Quelle, wenn sich das CSS nicht
            // geändert hat – auch im Vergleich zum bereits in OBS gespeicherten
            // CSS (lastAppliedCss-Cache ist nach einem Reload des Dashboards leer).
            if (newCss === lastAppliedCss.get(input.inputName) || newCss === currentCss) {
                continue;
            }

            await obs.call('SetInputSettings', {
                inputName: input.inputName,
                inputSettings: { css: newCss },
                overlay: true
            });
            lastAppliedCss.set(input.inputName, newCss);
            updated++;
        }

        return { updated, checked, connected: true };
    } catch (err) {
        console.error('[OBS Sync] Fehler bei pushStateToOBS:', err);
        showToast('error', `OBS-Sync Fehler: ${err.message}`);
        return { updated, checked, connected: true, error: err };
    } finally {
        isSyncing = false;
    }
}
