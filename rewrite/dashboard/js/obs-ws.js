/**
 * @file obs-ws.js
 * Schlanker, robuster Client für das OBS WebSocket v5 Protokoll (OBS Studio 28+).
 * Bietet Authentifizierung (SHA-256 Salt/Challenge), Request-Response via Promises
 * und Fehlerbehandlung.
 *
 * Gehört zu Version 2 (rewrite/dashboard/).
 * Zusammenspiel:
 *   - Implementiert das OBS WebSocket v5 Protokoll direkt auf Basis des nativen Browser-WebSockets.
 *   - Berechnet Authentifizierungs-Token über Web Crypto API (SHA-256).
 *   - Wird von obs-sync.js gekapselt für High-Level-Operationen (CSS-Injektion, Quellenabfrage).
 */

import { LP_DEBUG } from './box-utils.js';

/** Zeitlimit für den initialen Handshake (Hello/Identify) in Millisekunden. */
const HANDSHAKE_TIMEOUT_MS = 10000;

/** Zeitlimit für einzelne API-Anfragen an OBS Studio in Millisekunden. */
const REQUEST_TIMEOUT_MS = 10000;

/**
 * Erzeugt einen SHA-256 Hash als Base64-String.
 * Nutzt die Web Crypto API (`crypto.subtle`) – im lokalen Chromium-Browser
 * (File- und HTTP-Kontext) immer verfügbar, ein Fallback ist nicht nötig.
 *
 * @param {string} message - Der zu hashende Eingabestring.
 * @returns {Promise<string>} Base64-kodierter SHA-256-Hashwert.
 * @throws {Error} Wenn Web Crypto im Kontext nicht zur Verfügung steht.
 */
async function sha256Base64(message) {
    const encoder = new TextEncoder();
    const data = encoder.encode(message);

    if (!window.crypto || !window.crypto.subtle || !window.crypto.subtle.digest) {
        throw new Error('Web Crypto API ist in diesem Browser-Kontext nicht verfügbar.');
    }

    const digest = await window.crypto.subtle.digest('SHA-256', data);
    const bytes = new Uint8Array(digest);
    let binary = '';
    for (let i = 0; i < bytes.byteLength; i++) {
        binary += String.fromCharCode(bytes[i]);
    }
    return btoa(binary);
}

/**
 * WebSocket-Client zur Ansteuerung von OBS Studio über das Protokoll v5.
 */
export class OBSWebSocketClient {
    constructor() {
        this.ws = null;
        this.messageId = 1;
        this.pendingRequests = new Map();
        this.onConnect = null;
        this.onDisconnect = null;
        this.onError = null;
    }

    /**
     * Gibt an, ob der WebSocket aktuell geöffnet und einsatzbereit ist.
     * @type {boolean}
     */
    get isConnected() {
        return this.ws !== null && this.ws.readyState === WebSocket.OPEN;
    }

    /**
     * Stellt eine Verbindung zum OBS WebSocket Server her und führt den v5 Handshake durch.
     *
     * @param {string} [password=''] - Das in den OBS WebSocket-Einstellungen konfigurierte Server-Passwort.
     * @param {number} [port=4455] - Der WebSocket-Port (Standard in OBS v5: 4455).
     * @param {string} [host='127.0.0.1'] - Die Host-Adresse von OBS Studio.
     * @returns {Promise<boolean>} Löst mit true auf, sobald die Authentifizierung erfolgreich war.
     */
    connect(password = '', port = 4455, host = '127.0.0.1') {
        return new Promise((resolve, reject) => {
            if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
                reject(new Error('OBS-WebSocket-Verbindung ist bereits aktiv oder wird aufgebaut.'));
                return;
            }

            if (LP_DEBUG) console.debug('[liedplan] OBS WebSocket Verbindungsaufbau zu:', `ws://${host}:${port}`);

            let handshakeTimer = null;
            let settled = false;

            const finalize = (fn, arg) => {
                if (settled) return;
                settled = true;
                if (handshakeTimer) {
                    clearTimeout(handshakeTimer);
                    handshakeTimer = null;
                }
                fn(arg);
            };

            try {
                this.ws = new WebSocket(`ws://${host}:${port}`);
            } catch (err) {
                finalize(reject, err instanceof Error ? err : new Error(String(err)));
                return;
            }

            const activeSocket = this.ws;

            activeSocket.onmessage = async (event) => {
                if (activeSocket !== this.ws) return;

                let msg;
                try {
                    msg = JSON.parse(event.data);
                } catch (err) {
                    const parseErr = new Error(`Ungültige OBS WebSocket JSON-Antwort: ${err.message}`);
                    if (this.onError) this.onError(parseErr);
                    return;
                }

                // OpCode 0: Hello vom Server
                if (msg.op === 0) {
                    const auth = msg.d?.authentication;
                    let authResponse;

                    if (LP_DEBUG) console.debug('[liedplan] OBS WebSocket OpCode 0 (Hello) empfangen, Auth verlangt:', !!auth);

                    if (auth) {
                        if (!password) {
                            activeSocket.close();
                            const authErr = new Error('OBS fordert ein Passwort an, aber keines angegeben.');
                            if (this.onError) this.onError(authErr);
                            finalize(reject, authErr);
                            return;
                        }
                        try {
                            const secret = await sha256Base64(password + auth.salt);
                            authResponse = await sha256Base64(secret + auth.challenge);
                        } catch (calcErr) {
                            activeSocket.close();
                            finalize(reject, new Error(`Passwort-Hashing fehlgeschlagen: ${calcErr.message}`));
                            return;
                        }
                    }

                    // OpCode 1: Identify
                    if (LP_DEBUG) console.debug('[liedplan] Sende OBS WebSocket OpCode 1 (Identify)');
                    activeSocket.send(JSON.stringify({
                        op: 1,
                        d: {
                            rpcVersion: 1,
                            authentication: authResponse,
                            eventSubscriptions: 0 // Wir benötigen keine Event-Streams
                        }
                    }));
                }
                // OpCode 2: Identified (Authentifizierung erfolgreich)
                else if (msg.op === 2) {
                    if (LP_DEBUG) console.debug('[liedplan] OBS WebSocket OpCode 2 (Identified) - Verbindung steht!');
                    if (this.onConnect) this.onConnect();
                    finalize(resolve, true);
                }
                // OpCode 7: RequestResponse
                else if (msg.op === 7) {
                    const reqId = msg.d?.requestId;
                    const pending = this.pendingRequests.get(reqId);
                    if (pending) {
                        this.pendingRequests.delete(reqId);
                        if (msg.d?.requestStatus?.result) {
                            pending.resolve(msg.d.responseData || {});
                        } else {
                            const comment = msg.d?.requestStatus?.comment || 'Unbekannter OBS-Fehler';
                            const code = msg.d?.requestStatus?.code || -1;
                            pending.reject(new Error(`OBS Anfrage fehlgeschlagen [${code}]: ${comment}`));
                        }
                    }
                }
            };

            activeSocket.onclose = () => {
                if (activeSocket !== this.ws) return;
                this.ws = null;
                if (handshakeTimer) {
                    clearTimeout(handshakeTimer);
                    handshakeTimer = null;
                }
                for (const pending of this.pendingRequests.values()) {
                    pending.reject(new Error('OBS-Verbindung wurde getrennt.'));
                }
                this.pendingRequests.clear();

                if (this.onDisconnect) this.onDisconnect();
                finalize(reject, new Error('OBS-Verbindung getrennt bevor Handshake beendet war.'));
            };

            activeSocket.onerror = (err) => {
                if (activeSocket !== this.ws) return;
                const errObj = new Error(err?.message || 'OBS WebSocket Verbindungsfehler');
                if (this.onError) this.onError(errObj);
                finalize(reject, errObj);
            };

            handshakeTimer = setTimeout(() => {
                if (activeSocket === this.ws) {
                    activeSocket.close();
                }
                finalize(reject, new Error(`Zeitüberschreitung beim Verbindungsaufbau (${HANDSHAKE_TIMEOUT_MS / 1000}s)`));
            }, HANDSHAKE_TIMEOUT_MS);
        });
    }

    /**
     * Sendet einen RequestType-Befehl (z. B. 'GetInputSettings' oder 'SetInputSettings') an OBS.
     *
     * @param {string} requestType - Name der OBS WebSocket v5 Anfrage.
     * @param {object} [requestData={}] - Payload-Objekt für die Anfrage.
     * @returns {Promise<any>} Antwortdaten des OBS-Servers.
     */
    call(requestType, requestData = {}) {
        return new Promise((resolve, reject) => {
            if (!this.isConnected) {
                reject(new Error('Nicht mit OBS verbunden.'));
                return;
            }

            const reqId = String(this.messageId++);
            if (LP_DEBUG) console.debug('[liedplan] OBS WebSocket Request:', requestType, 'ID:', reqId);
            let timeoutId = null;

            const handle = {
                resolve: (data) => {
                    if (timeoutId) clearTimeout(timeoutId);
                    resolve(data);
                },
                reject: (err) => {
                    if (timeoutId) clearTimeout(timeoutId);
                    reject(err);
                }
            };

            this.pendingRequests.set(reqId, handle);

            timeoutId = setTimeout(() => {
                if (this.pendingRequests.delete(reqId)) {
                    handle.reject(new Error(`Zeitüberschreitung bei OBS-Anfrage "${requestType}"`));
                }
            }, REQUEST_TIMEOUT_MS);

            try {
                this.ws.send(JSON.stringify({
                    op: 6,
                    d: {
                        requestType,
                        requestId: reqId,
                        requestData
                    }
                }));
            } catch (err) {
                this.pendingRequests.delete(reqId);
                if (timeoutId) clearTimeout(timeoutId);
                reject(err);
            }
        });
    }

    /**
     * Trennt die aktive WebSocket-Verbindung und weist alle offenen Anfragen ab.
     */
    disconnect() {
        if (LP_DEBUG) console.debug('[liedplan] OBS WebSocket trennen aufgerufen');
        if (this.ws) {
            const sock = this.ws;
            this.ws = null;
            try {
                sock.close();
            } catch (e) {
                // Fehler beim Schließen ignorieren
            }
        }
        for (const pending of this.pendingRequests.values()) {
            pending.reject(new Error('Verbindung manuell getrennt.'));
        }
        this.pendingRequests.clear();
    }
}
