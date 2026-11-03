/**
 * @file obs-ws.js
 * Minimaler OBS WebSocket v5 Client für das Web-Dashboard.
 *
 * Gehört zu Version 1 (main/controlui/).
 * Zusammenspiel:
 *   - Ermöglicht dem Dashboard (control.js), direkt mit der OBS Studio Instanz
 *     über WebSocket v5 (Standard-Port: 4455) zu kommunizieren.
 *   - Steuert Browser-Quellen, CSS-Injektion und Sichtbarkeit der Textbox in OBS.
 *   - Nutzt die Web Crypto API bzw. sha256.min.js für den Challenge-Response-Handshake.
 */

// Timeout für Verbindungsaufbau und RPC-Anfragen in Millisekunden
const OBS_WEBSOCKET_TIMEOUT_MS = 10000;

/**
 * Client-Klasse für die OBS WebSocket v5 RPC-Schnittstelle.
 */
class OBSWebSocket {
    /**
     * Initialisiert einen neuen OBSWebSocket-Client.
     */
    constructor() {
        this.ws = null;
        this.messageId = 1;
        this.resolvers = new Map();
        this.onConnect = null;
        this.onDisconnect = null;
        this.onError = null;
    }

    /**
     * Erzeugt einen Base64-kodierten SHA-256-Hash eines Strings
     * über die Standard-WebCrypto-API des Browsers.
     * @param {string} msg Zu hashender String
     * @returns {Promise<string>} Base64-Hash
     */
    async hashSHA256(msg) {
        const encoder = new TextEncoder();
        const data = encoder.encode(msg);
        const hashBuffer = await crypto.subtle.digest('SHA-256', data);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return btoa(String.fromCharCode.apply(null, hashArray));
    }

    /**
     * Baut eine WebSocket-Verbindung zu OBS auf und durchläuft den v5-Handshake (Hello -> Identify -> Identified).
     * @param {string} password Optionales OBS-WebSocket-Passwort
     * @param {number} port Zielport (Standard: 4455)
     * @returns {Promise<void>}
     */
    connect(password = '', port = 4455) {
        return new Promise((resolve, reject) => {
            let handshakeTimeout = null;
            // Verhindert doppelte Aufrufe, falls onerror und onclose gleichzeitig feuern
            let settled = false;
            const settle = (fn, value) => {
                if (settled) return;
                settled = true;
                clearHandshakeTimeout();
                fn(value);
            };

            const clearHandshakeTimeout = () => {
                if (handshakeTimeout) {
                    clearTimeout(handshakeTimeout);
                    handshakeTimeout = null;
                }
            };

            // Keine zweite Verbindung starten, während eine existiert oder aufgebaut wird
            if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
                reject(new Error("Schon verbunden"));
                return;
            }

            try {
                this.ws = new WebSocket(`ws://127.0.0.1:${port}`);
            } catch (err) {
                reject(err instanceof Error ? err : new Error(String(err)));
                return;
            }

            // Socket-Instanz dieses connect()-Aufrufs sichern (Schutz gegen Race Conditions bei Reconnect)
            const socket = this.ws;

            socket.onopen = () => {
                // Warten auf Hello (op 0) vom Server
            };

            socket.onmessage = async (event) => {
                if (socket !== this.ws) return; // Veralteter Socket -> ignorieren
                let msg;
                try {
                    msg = JSON.parse(event.data);
                } catch (err) {
                    const parseError = new Error(`Invalid OBS WebSocket message: ${err.message || err}`);
                    if (this.onError) this.onError(parseError);
                    this.resolvers.forEach(r => r.reject(parseError));
                    this.resolvers.clear();
                    settle(reject, parseError);
                    return;
                }

                if (msg.op === 0) {
                    // Hello empfangen -> Authentifizierung prüfen
                    const authReq = msg.d.authentication;
                    let authStr = undefined;

                    if (authReq) {
                        if (!password) {
                            socket.close();
                            const authError = new Error("Passwort benötigt, aber keines angegeben.");
                            if (this.onError) this.onError(authError);
                            settle(reject, authError);
                            return;
                        }
                        const passHash = await this.hashSHA256(password + authReq.salt);
                        authStr = await this.hashSHA256(passHash + authReq.challenge);
                    }

                    // Identify senden (op 1)
                    socket.send(JSON.stringify({
                        op: 1,
                        d: {
                            rpcVersion: 1,
                            authentication: authStr,
                            eventSubscriptions: 0 // Keine Events benötigt
                        }
                    }));
                }
                else if (msg.op === 2) {
                    // Identified (Erfolgreich verbunden und autorisiert)
                    if (this.onConnect) this.onConnect();
                    settle(resolve);
                }
                else if (msg.op === 7) {
                    // RequestResponse (Antwort auf vorherigen Request)
                    const reqId = msg.d.requestId;
                    if (this.resolvers.has(reqId)) {
                        const resolver = this.resolvers.get(reqId);
                        if (msg.d.requestStatus.result) {
                            resolver.resolve(msg.d.responseData);
                        } else {
                            const status = msg.d.requestStatus;
                            resolver.reject(new Error(`OBS request failed (${status.code}): ${status.comment || 'Unknown error'}`));
                        }
                        this.resolvers.delete(reqId);
                    }
                }
            };

            socket.onclose = () => {
                if (socket !== this.ws) return; // Veralteter Socket -> ignorieren
                this.ws = null;
                clearHandshakeTimeout();
                if (this.onDisconnect) this.onDisconnect();
                // Ausstehende Requests ablehnen
                this.resolvers.forEach(r => r.reject(new Error("Disconnected")));
                this.resolvers.clear();
                // Falls Handshake unvollständig war, connect-Promise ablehnen
                settle(reject, new Error("Verbindung getrennt vor Identifizierung"));
            };

            socket.onerror = (err) => {
                if (socket !== this.ws) return; // Veralteter Socket -> ignorieren
                const connectionError = new Error(err?.message || 'OBS WebSocket connection error');
                if (this.onError) this.onError(connectionError);
                settle(reject, connectionError);
            };

            handshakeTimeout = setTimeout(() => {
                const timeoutError = new Error('OBS WebSocket handshake timed out');
                if (socket) socket.close();
                settle(reject, timeoutError);
            }, OBS_WEBSOCKET_TIMEOUT_MS);
        });
    }

    /**
     * Sendet einen synchronen RPC-Aufruf (op 6) an OBS und liefert das Ergebnis per Promise.
     * @param {string} requestType OBS WebSocket Request-Typ (z. B. 'SetInputSettings')
     * @param {Object} requestData Request-spezifische Parameter
     * @returns {Promise<any>}
     */
    call(requestType, requestData = {}) {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                reject(new Error("Not connected"));
                return;
            }

            const reqId = (this.messageId++).toString();
            let timeoutId = null;
            const resolver = {
                resolve: value => {
                    if (timeoutId) clearTimeout(timeoutId);
                    resolve(value);
                },
                reject: error => {
                    if (timeoutId) clearTimeout(timeoutId);
                    reject(error);
                }
            };
            this.resolvers.set(reqId, resolver);
            timeoutId = setTimeout(() => {
                if (!this.resolvers.delete(reqId)) return;
                resolver.reject(new Error(`OBS request timed out: ${requestType}`));
            }, OBS_WEBSOCKET_TIMEOUT_MS);

            this.ws.send(JSON.stringify({
                op: 6,
                d: {
                    requestType: requestType,
                    requestId: reqId,
                    requestData: requestData
                }
            }));
        });
    }

    /**
     * Schließt die aktive WebSocket-Verbindung zu OBS Studio.
     */
    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
}
