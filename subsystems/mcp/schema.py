"""MCP schema definitions — tools, resources, and data models."""

from dataclasses import dataclass, field
from typing import Any


@dataclass
class ToolDefinition:
    name: str
    description: str
    input_schema: dict[str, Any]
    output_schema: dict[str, Any] = field(default_factory=lambda: {
        "type": "object",
        "properties": {"result": {"type": "any"}},
    })


@dataclass
class ResourceDefinition:
    uri: str
    name: str
    description: str
    mime_type: str = "application/json"
    schema_: dict[str, Any] | None = None


# ── Tool Definitions ──────────────────────────────────────────────────

ALL_TOOLS: list[ToolDefinition] = [
    ToolDefinition(
        name="get_telemetry",
        description="Get the latest telemetry state for a subsystem",
        input_schema={
            "type": "object",
            "properties": {
                "subsystem": {
                    "type": "string",
                    "description": "Subsystem name (dynamics, gnc, eps, eclss, thermal, comms, cdh, crew)",
                    "enum": ["dynamics", "gnc", "eps", "eclss", "thermal", "comms", "cdh", "crew"],
                }
            },
            "required": ["subsystem"],
        },
    ),
    ToolDefinition(
        name="send_command",
        description="Send a command to a subsystem",
        input_schema={
            "type": "object",
            "properties": {
                "target": {"type": "string", "description": "Target subsystem"},
                "opcode": {"type": "string", "description": "Command opcode"},
                "args": {"type": "object", "description": "Command arguments"},
            },
            "required": ["target", "opcode"],
        },
    ),
    ToolDefinition(
        name="list_subsystems",
        description="List all available subsystems in the simulation",
        input_schema={"type": "object", "properties": {}},
    ),
    ToolDefinition(
        name="get_scenario",
        description="Get the current scenario information",
        input_schema={"type": "object", "properties": {}},
    ),
    ToolDefinition(
        name="set_time_scale",
        description="Change the simulation time scale/rate",
        input_schema={
            "type": "object",
            "properties": {
                "rate": {
                    "type": "number",
                    "description": "Simulation speed multiplier (0.1 = slow motion, 1.0 = real-time, 10.0 = fast-forward)",
                    "minimum": 0.1,
                    "maximum": 100.0,
                }
            },
            "required": ["rate"],
        },
    ),
    ToolDefinition(
        name="inject_fault",
        description="Inject a fault/anomaly into a subsystem",
        input_schema={
            "type": "object",
            "properties": {
                "subsystem": {"type": "string", "description": "Target subsystem"},
                "fault_type": {"type": "string", "description": "Type of fault to inject"},
                "params": {
                    "type": "object",
                    "description": "Fault parameters",
                    "additionalProperties": True,
                },
            },
            "required": ["subsystem", "fault_type"],
        },
    ),
    ToolDefinition(
        name="get_orbit_state",
        description="Get current orbital elements and state vector",
        input_schema={"type": "object", "properties": {}},
    ),
    ToolDefinition(
        name="get_crew_status",
        description="Get current crew status and activity information",
        input_schema={"type": "object", "properties": {}},
    ),
]


# ── Resource Definitions ──────────────────────────────────────────────

ALL_RESOURCES: list[ResourceDefinition] = [
    ResourceDefinition(
        uri="mcp://telemetry/{subsystem}/latest",
        name="Latest Telemetry",
        description="Latest telemetry snapshot for a subsystem",
    ),
    ResourceDefinition(
        uri="mcp://telemetry/{subsystem}/history",
        name="Telemetry History",
        description="Time-series telemetry history for a subsystem",
    ),
    ResourceDefinition(
        uri="mcp://scenario/current",
        name="Current Scenario",
        description="Current scenario definition and state",
    ),
    ResourceDefinition(
        uri="mcp://system/status",
        name="System Status",
        description="Overall system health and status",
    ),
    ResourceDefinition(
        uri="mcp://orbit/state",
        name="Orbit State",
        description="Current orbit state vector and elements",
    ),
    ResourceDefinition(
        uri="mcp://crew/status",
        name="Crew Status",
        description="Current crew status information",
    ),
]


# ── Helper ────────────────────────────────────────────────────────────

def tool_def_to_dict(td: ToolDefinition) -> dict[str, Any]:
    return {
        "name": td.name,
        "description": td.description,
        "input_schema": td.input_schema,
        "output_schema": td.output_schema,
    }


def get_tool(name: str) -> ToolDefinition | None:
    for t in ALL_TOOLS:
        if t.name == name:
            return t
    return None
