// Minimal OBS WebSocket v5 Client
const OBS_WEBSOCKET_TIMEOUT_MS = 10000;

class OBSWebSocket {
    constructor() {
        this.ws = null;
        this.messageId = 1;
        this.resolvers = new Map();
        this.onConnect = null;
        this.onDisconnect = null;
        this.onError = null;
    }

    async hashSHA256(msg) {
        const encoder = new TextEncoder();
        const data = encoder.encode(msg);
        const hashBuffer = await crypto.subtle.digest('SHA-256', data);
        const hashArray = Array.from(new Uint8Array(hashBuffer));
        return btoa(String.fromCharCode.apply(null, hashArray));
    }

    connect(password = '', port = 4455) {
        return new Promise((resolve, reject) => {
            let handshakeTimeout = null;
            const clearHandshakeTimeout = () => {
                if (handshakeTimeout) {
                    clearTimeout(handshakeTimeout);
                    handshakeTimeout = null;
                }
            };

            // Don't spawn a second socket while one is already open/connecting
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

            this.ws.onopen = () => {
                // Wait for Hello (op 0)
            };

            this.ws.onmessage = async (event) => {
                let msg;
                try {
                    msg = JSON.parse(event.data);
                } catch (err) {
                    clearHandshakeTimeout();
                    const parseError = new Error(`Invalid OBS WebSocket message: ${err.message || err}`);
                    if (this.onError) this.onError(parseError);
                    this.resolvers.forEach(r => r.reject(parseError));
                    this.resolvers.clear();
                    reject(parseError);
                    return;
                }
                
                if (msg.op === 0) {
                    // Hello received
                    const authReq = msg.d.authentication;
                    let authStr = undefined;
                    
                    if (authReq) {
                        if (!password) {
                            this.ws.close();
                            const authError = new Error("Passwort benötigt, aber keines angegeben.");
                            clearHandshakeTimeout();
                            if (this.onError) this.onError(authError);
                            reject(authError);
                            return;
                        }
                        const passHash = await this.hashSHA256(password + authReq.salt);
                        authStr = await this.hashSHA256(passHash + authReq.challenge);
                    }

                    // Send Identify
                    this.ws.send(JSON.stringify({
                        op: 1,
                        d: {
                            rpcVersion: 1,
                            authentication: authStr,
                            eventSubscriptions: 0 // We don't need events
                        }
                    }));
                } 
                else if (msg.op === 2) {
                    // Identified (Success)
                    clearHandshakeTimeout();
                    if (this.onConnect) this.onConnect();
                    resolve();
                }
                else if (msg.op === 7) {
                    // RequestResponse
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

            this.ws.onclose = () => {
                clearHandshakeTimeout();
                if (this.onDisconnect) this.onDisconnect();
                // Reject pending requests
                this.resolvers.forEach(r => r.reject(new Error("Disconnected")));
                this.resolvers.clear();
            };

            this.ws.onerror = (err) => {
                clearHandshakeTimeout();
                const connectionError = new Error(err?.message || 'OBS WebSocket connection error');
                if (this.onError) this.onError(connectionError);
                reject(connectionError);
            };

            handshakeTimeout = setTimeout(() => {
                const timeoutError = new Error('OBS WebSocket handshake timed out');
                clearHandshakeTimeout();
                if (this.ws) this.ws.close();
                reject(timeoutError);
            }, OBS_WEBSOCKET_TIMEOUT_MS);
        });
    }

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

    disconnect() {
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
}
