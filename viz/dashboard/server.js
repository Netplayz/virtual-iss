import express from "express";
import { createServer } from "http";
import { WebSocketServer, WebSocket } from "ws";
import http from "http";
import { fileURLToPath } from "url";
import { dirname, join } from "path";

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

const PORT = process.env.OPENMCT_PORT || 8320;
const NATS_WS_URL = process.env.NATS_WS_URL || "ws://localhost:8222";
const MCP_API_URL = process.env.MCP_API_URL || "http://localhost:8331";

const app = express();
const server = createServer(app);

const wss = new WebSocketServer({ server, path: "/nats" });

wss.on("connection", (ws, req) => {
  console.log("Dashboard WebSocket connected");

  const natsWs = new WebSocket(NATS_WS_URL);
  natsWs.on("open", () => {
    console.log("NATS WebSocket bridge connected");
  });
  natsWs.on("message", (data) => {
    ws.send(data.toString());
  });
  natsWs.on("close", () => {
    ws.close();
  });
  natsWs.on("error", (err) => {
    console.error("NATS WS error:", err);
    ws.close();
  });

  ws.on("message", (data) => {
    if (natsWs.readyState === natsWs.OPEN) {
      natsWs.send(data.toString());
    }
  });
  ws.on("close", () => {
    natsWs.close();
  });
  ws.on("error", (err) => {
    console.error("Dashboard WS error:", err);
    natsWs.close();
  });
});

app.use("/api/v1", (req, res) => {
  const targetUrl = new URL(req.originalUrl, MCP_API_URL);
  const options = {
    hostname: targetUrl.hostname,
    port: targetUrl.port,
    path: targetUrl.pathname + targetUrl.search,
    method: req.method,
    headers: { ...req.headers, host: targetUrl.host },
  };

  const proxyReq = http.request(options, (proxyRes) => {
    res.writeHead(proxyRes.statusCode, proxyRes.headers);
    proxyRes.pipe(res);
  });

  proxyReq.on("error", (err) => {
    console.error("Proxy error:", err);
    res.status(502).json({ error: "Bad Gateway" });
  });

  req.pipe(proxyReq);
});

app.use(express.static(join(__dirname, "dist")));

app.get("*", (req, res) => {
  res.sendFile(join(__dirname, "dist", "index.html"));
});

server.listen(PORT, () => {
  console.log(`Virtual ISS Dashboard running on http://localhost:${PORT}`);
});
