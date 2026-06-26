"""MCPServer — NATS + HTTP gateway for the Model Context Protocol."""

import asyncio
import json
import logging
import os
from datetime import datetime, timezone
from typing import Any

import nats
from aiohttp import web

from handlers import CommandHandler, ScenarioHandler, SystemHandler, TelemetryHandler
from schema import ALL_TOOLS, tool_def_to_dict

logger = logging.getLogger("mcp.server")


class MCPServer:
    def __init__(self, nc: nats.NATSConnection):
        self.nc = nc
        self.telemetry = TelemetryHandler(nc)
        self.command = CommandHandler(nc)
        self.system = SystemHandler(nc)
        self.scenario = ScenarioHandler(nc)
        self._sse_clients: set[asyncio.Queue] = set()

    async def run(self):
        logger.info("starting MCP server")

        # NATS subscriptions
        await self.nc.subscribe("mcp.query", cb=self.handle_query)
        await self.nc.subscribe("mcp.tool.call", cb=self.handle_tool_call)

        # Subscribe to all telemetry for SSE streaming
        for sub in ["dynamics", "gnc", "eps", "eclss", "thermal", "comms", "cdh", "crew"]:
            subject = f"telemetry.{sub}.state"
            await self.nc.subscribe(subject, cb=self._broadcast_telemetry)

        logger.info("NATS subscriptions established")

        # HTTP server
        port = int(os.environ.get("MCP_PORT", "8331"))
        app = web.Application()
        app.router.add_get("/api/v1/telemetry/{subsystem}", self.http_get_telemetry)
        app.router.add_get("/api/v1/telemetry/{subsystem}/history", self.http_get_history)
        app.router.add_post("/api/v1/command", self.http_send_command)
        app.router.add_get("/api/v1/subsystems", self.http_list_subsystems)
        app.router.add_get("/api/v1/scenario", self.http_get_scenario)
        app.router.add_get("/api/v1/status", self.http_status)
        app.router.add_get("/api/v1/stream", self.http_sse_stream)
        app.router.add_get("/api/v1/tools", self.http_list_tools)
        app.router.add_post("/api/v1/tools/{tool_name}", self.http_run_tool)
        app.router.add_get("/openapi.yaml", self.http_openapi_spec)

        runner = web.AppRunner(app)
        await runner.setup()
        site = web.TCPSite(runner, "0.0.0.0", port)
        await site.start()

        logger.info("MCP server running on port %d", port)

        # Keep running
        while True:
            await asyncio.sleep(3600)

    # ── NATS handlers ─────────────────────────────────────────────

    async def handle_query(self, msg):
        try:
            query = json.loads(msg.data)
            qtype = query.get("type", "")
            context = query.get("context", {})

            if qtype == "telemetry":
                subsystem = context.get("subsystem", "dynamics")
                data = await self.telemetry.get_latest(subsystem)
            elif qtype == "command":
                data = await self.command.send_command(
                    context.get("target", ""),
                    context.get("opcode", ""),
                    context.get("args"),
                )
            elif qtype == "scenario":
                data = await self.scenario.get_current_scenario()
            elif qtype == "system":
                data = await self.system.get_status()
            else:
                data = {"error": f"unknown query type: {qtype}"}

            response = {
                "query_id": query.get("query_id", ""),
                "type": qtype,
                "data": data,
            }
            await msg.respond(json.dumps(response).encode())
        except Exception as e:
            logger.exception("handle_query error")
            await msg.respond(json.dumps({"error": str(e)}).encode())

    async def handle_tool_call(self, msg):
        try:
            call = json.loads(msg.data)
            tool_name = call.get("tool", "")
            args = call.get("args", {})
            call_id = call.get("call_id", "")

            result = await self._execute_tool(tool_name, args)
            response = {
                "call_id": call_id,
                "success": "error" not in result,
                "output": result,
                "error": result.get("error", ""),
            }
            await msg.respond(json.dumps(response).encode())
        except Exception as e:
            logger.exception("handle_tool_call error")
            await msg.respond(json.dumps({"call_id": "", "success": False, "error": str(e)}).encode())

    async def _execute_tool(self, tool_name: str, args: dict) -> dict:
        if tool_name == "get_telemetry":
            subsystem = args.get("subsystem", "dynamics")
            return await self.telemetry.get_latest(subsystem)

        elif tool_name == "send_command":
            return await self.command.send_command(
                args.get("target", ""),
                args.get("opcode", ""),
                args.get("args"),
            )

        elif tool_name == "list_subsystems":
            subs = await self.system.list_subsystems()
            return {"subsystems": subs}

        elif tool_name == "get_scenario":
            return await self.scenario.get_current_scenario()

        elif tool_name == "set_time_scale":
            return await self.scenario.set_time_scale(float(args.get("rate", 1.0)))

        elif tool_name == "inject_fault":
            return await self.scenario.inject_fault(
                args.get("subsystem", ""),
                args.get("fault_type", ""),
                args.get("params"),
            )

        elif tool_name == "get_orbit_state":
            return await self.telemetry.get_latest("dynamics")

        elif tool_name == "get_crew_status":
            return await self.telemetry.get_latest("crew")

        else:
            return {"error": f"unknown tool: {tool_name}"}

    # ── SSE streaming ─────────────────────────────────────────────

    async def _broadcast_telemetry(self, msg):
        data = json.loads(msg.data)
        payload = json.dumps({
            "subject": msg.subject,
            "data": data,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })
        for q in list(self._sse_clients):
            try:
                await q.put(payload)
            except Exception:
                self._sse_clients.discard(q)

    # ── HTTP handlers ─────────────────────────────────────────────

    async def http_get_telemetry(self, request):
        subsystem = request.match_info["subsystem"]
        data = await self.telemetry.get_latest(subsystem)
        return web.json_response(data)

    async def http_get_history(self, request):
        subsystem = request.match_info["subsystem"]
        limit = int(request.query.get("limit", "100"))
        history = await self.telemetry.get_history(subsystem, limit)
        return web.json_response(history)

    async def http_send_command(self, request):
        body = await request.json()
        data = await self.command.send_command(
            body.get("target", ""),
            body.get("opcode", ""),
            body.get("args"),
        )
        return web.json_response(data)

    async def http_list_subsystems(self, request):
        subs = await self.system.list_subsystems()
        return web.json_response({"subsystems": subs})

    async def http_get_scenario(self, request):
        data = await self.scenario.get_current_scenario()
        return web.json_response(data)

    async def http_status(self, request):
        status = await self.system.get_status()
        return web.json_response({
            "service": "mcp",
            "status": "ok",
            "subsystems": status,
            "timestamp": datetime.now(timezone.utc).isoformat(),
        })

    async def http_sse_stream(self, request):
        q: asyncio.Queue = asyncio.Queue()
        self._sse_clients.add(q)

        response = web.StreamResponse(
            status=200,
            reason="OK",
            headers={
                "Content-Type": "text/event-stream",
                "Cache-Control": "no-cache",
                "Connection": "keep-alive",
                "Access-Control-Allow-Origin": "*",
            },
        )
        await response.prepare(request)

        try:
            while True:
                try:
                    data = await asyncio.wait_for(q.get(), timeout=30.0)
                    await response.write(f"data: {data}\n\n".encode())
                except asyncio.TimeoutError:
                    await response.write(b": keepalive\n\n")
        except (asyncio.CancelledError, ConnectionResetError):
            pass
        finally:
            self._sse_clients.discard(q)

        return response

    async def http_list_tools(self, request):
        return web.json_response({
            "tools": [tool_def_to_dict(t) for t in ALL_TOOLS],
        })

    async def http_run_tool(self, request):
        tool_name = request.match_info["tool_name"]
        body = await request.json() if request.body_exists else {}
        args = body.get("args", {})
        result = await self._execute_tool(tool_name, args)
        return web.json_response(result)

    async def http_openapi_spec(self, request):
        return web.json_response(self._generate_openapi_spec())

    def _generate_openapi_spec(self) -> dict:
        paths = {}
        for sub in ["dynamics", "gnc", "eps", "eclss", "thermal", "comms", "cdh", "crew"]:
            paths[f"/api/v1/telemetry/{sub}"] = {
                "get": {
                    "summary": f"Get latest {sub} telemetry",
                    "operationId": f"get{sub.capitalize()}Telemetry",
                    "responses": {"200": {"description": "Telemetry data"}},
                }
            }
            paths[f"/api/v1/telemetry/{sub}/history"] = {
                "get": {
                    "summary": f"Get {sub} telemetry history",
                    "operationId": f"get{sub.capitalize()}History",
                    "parameters": [
                        {
                            "name": "limit",
                            "in": "query",
                            "schema": {"type": "integer", "default": 100},
                        }
                    ],
                    "responses": {"200": {"description": "Telemetry history"}},
                }
            }

        paths["/api/v1/command"] = {
            "post": {
                "summary": "Send a command",
                "operationId": "sendCommand",
                "requestBody": {
                    "required": True,
                    "content": {
                        "application/json": {
                            "schema": {
                                "type": "object",
                                "properties": {
                                    "target": {"type": "string"},
                                    "opcode": {"type": "string"},
                                    "args": {"type": "object"},
                                },
                                "required": ["target", "opcode"],
                            }
                        }
                    },
                },
                "responses": {"200": {"description": "Command response"}},
            }
        }

        paths["/api/v1/subsystems"] = {
            "get": {
                "summary": "List all subsystems",
                "operationId": "listSubsystems",
                "responses": {"200": {"description": "Subsystem list"}},
            }
        }

        paths["/api/v1/scenario"] = {
            "get": {
                "summary": "Get current scenario",
                "operationId": "getScenario",
                "responses": {"200": {"description": "Scenario info"}},
            }
        }

        paths["/api/v1/status"] = {
            "get": {
                "summary": "Health check",
                "operationId": "getStatus",
                "responses": {"200": {"description": "Status info"}},
            }
        }

        paths["/api/v1/stream"] = {
            "get": {
                "summary": "SSE telemetry stream",
                "operationId": "streamTelemetry",
                "responses": {"200": {"description": "SSE event stream"}},
            }
        }

        paths["/api/v1/tools"] = {
            "get": {
                "summary": "List available tools",
                "operationId": "listTools",
                "responses": {"200": {"description": "Tool list"}},
            }
        }

        for tool in ALL_TOOLS:
            paths[f"/api/v1/tools/{tool.name}"] = {
                "post": {
                    "summary": f"Execute tool: {tool.name}",
                    "operationId": f"execute{tool.name}",
                    "requestBody": {
                        "content": {
                            "application/json": {
                                "schema": {
                                    "type": "object",
                                    "properties": {
                                        "args": tool.input_schema,
                                    },
                                }
                            }
                        }
                    },
                    "responses": {"200": {"description": "Tool result"}},
                }
            }

        spec = {
            "openapi": "3.1.0",
            "info": {
                "title": "Virtual ISS MCP API",
                "version": "0.1.0",
                "description": "Model Context Protocol interface for Virtual ISS simulation",
            },
            "servers": [{"url": "http://localhost:8331"}],
            "paths": paths,
        }

        return spec
