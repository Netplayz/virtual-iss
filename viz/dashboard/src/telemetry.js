const NATS_WS_URL = `ws://${location.hostname}:8320/nats`;
const MCP_API_URL = `/api/v1`;

class TelemetryBridge {
  constructor() {
    this._ws = null;
    this._subscribers = new Map();
    this._buffer = new Map();
    this._history = new Map();
    this._connected = false;
    this._pending = [];
  }

  connect() {
    return new Promise((resolve, reject) => {
      this._ws = new WebSocket(NATS_WS_URL);

      this._ws.onopen = () => {
        this._connected = true;
        this._subscribe("telemetry.>");
        for (const sub of this._pending) {
          this._subscribe(sub);
        }
        this._pending = [];
        resolve();
      };

      this._ws.onmessage = (event) => {
        try {
          const msg = JSON.parse(event.data);
          const subject = msg.subject || "";
          const payload = typeof msg.payload === "string"
            ? JSON.parse(msg.payload)
            : msg.payload;

          if (subject.startsWith("telemetry.")) {
            const parts = subject.split(".");
            const subsystem = parts[1];
            const data = { ...payload, subsystem, timestamp: Date.now() };
            this._buffer.set(subsystem, data);
            if (!this._history.has(subsystem)) {
              this._history.set(subsystem, []);
            }
            this._history.get(subsystem).push(data);
            const maxHistory = 10000;
            if (this._history.get(subsystem).length > maxHistory) {
              this._history.get(subsystem).shift();
            }
            const callbacks = this._subscribers.get(subsystem) || [];
            for (const cb of callbacks) {
              try {
                cb(data);
              } catch (e) {
                console.warn("Subscriber callback error:", e);
              }
            }
          }
        } catch (e) {
          console.warn("Failed to parse WS message:", e);
        }
      };

      this._ws.onerror = (err) => {
        console.error("NATS WS error:", err);
        reject(err);
      };

      this._ws.onclose = () => {
        this._connected = false;
        setTimeout(() => this.connect(), 1000);
      };
    });
  }

  _subscribe(subject) {
    if (this._ws && this._ws.readyState === WebSocket.OPEN) {
      this._ws.send(JSON.stringify({ op: "sub", subject }));
    }
  }

  subscribe(subsystem, callback) {
    const key = subsystem;
    if (!this._subscribers.has(key)) {
      this._subscribers.set(key, []);
    }
    this._subscribers.get(key).push(callback);

    if (!this._connected) {
      this._pending.push(`telemetry.${subsystem}.state`);
    } else {
      this._subscribe(`telemetry.${subsystem}.state`);
    }

    const latest = this._buffer.get(key);
    if (latest) {
      callback(latest);
    }

    return () => {
      const cbs = this._subscribers.get(key);
      if (cbs) {
        const idx = cbs.indexOf(callback);
        if (idx >= 0) cbs.splice(idx, 1);
      }
    };
  }

  request(subsystem, options) {
    const history = this._history.get(subsystem) || [];
    const start = options.start ? options.start : 0;
    const end = options.end ? options.end : Date.now();
    return Promise.resolve(
      history.filter((d) => d.timestamp >= start && d.timestamp <= end)
    );
  }

  getLatest(subsystem) {
    return this._buffer.get(subsystem) || null;
  }
}

export { TelemetryBridge };
