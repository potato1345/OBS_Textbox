// Minimal OBS WebSocket v5 Client
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
            // Don't spawn a second socket while one is already open/connecting
            if (this.ws && (this.ws.readyState === WebSocket.OPEN || this.ws.readyState === WebSocket.CONNECTING)) {
                reject(new Error("Schon verbunden"));
                return;
            }

            try {
                this.ws = new WebSocket(`ws://127.0.0.1:${port}`);
            } catch (err) {
                reject(err);
                return;
            }

            this.ws.onopen = () => {
                // Wait for Hello (op 0)
            };

            this.ws.onmessage = async (event) => {
                const msg = JSON.parse(event.data);
                
                if (msg.op === 0) {
                    // Hello received
                    const authReq = msg.d.authentication;
                    let authStr = undefined;
                    
                    if (authReq) {
                        if (!password) {
                            this.ws.close();
                            if (this.onError) this.onError("Passwort benötigt, aber keines angegeben.");
                            reject(new Error("Auth required"));
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
                            resolver.reject(msg.d.requestStatus.code);
                        }
                        this.resolvers.delete(reqId);
                    }
                }
            };

            this.ws.onclose = () => {
                if (this.onDisconnect) this.onDisconnect();
                // Reject pending requests
                this.resolvers.forEach(r => r.reject(new Error("Disconnected")));
                this.resolvers.clear();
            };

            this.ws.onerror = (err) => {
                if (this.onError) this.onError(err);
                reject(err);
            };
        });
    }

    call(requestType, requestData = {}) {
        return new Promise((resolve, reject) => {
            if (!this.ws || this.ws.readyState !== WebSocket.OPEN) {
                reject(new Error("Not connected"));
                return;
            }

            const reqId = (this.messageId++).toString();
            this.resolvers.set(reqId, { resolve, reject });

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
