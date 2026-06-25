package bus

// NATS subjects for Virtual ISS message bus.
// Convention: <domain>.<subsystem>.<message_type>[.<qualifier>]

const (
	// Telemetry: subsystem state published at tick rate
	TlmDynamics     = "telemetry.dynamics.state"
	TlmGNC          = "telemetry.gnc.state"
	TlmEPS          = "telemetry.eps.state"
	TlmECLSS        = "telemetry.eclss.state"
	TlmThermal      = "telemetry.thermal.state"
	TlmComms        = "telemetry.comms.state"
	TlmCDH          = "telemetry.cdh.state"
	TlmCrew         = "telemetry.crew.state"
	TlmNAS          = "telemetry.nas.state"

	// Commands: request/reply pattern
	CmdUplink       = "command.uplink"
	CmdDownlink     = "command.downlink"
	CmdOrchestrator = "command.orchestrator"

	// State snapshots (persisted, JetStream)
	StateSnapshot   = "state.snapshot.>"
	StateRequest    = "state.request"

	// Events: alarms, anomalies, log entries
	EventLog        = "event.log"
	EventAlarm      = "event.alarm"
	EventAnomaly    = "event.anomaly"

	// NAS operations
	NASRecord       = "nas.record"
	NASReplay       = "nas.replay"
	NASStore        = "nas.store"
	NASRetrieve     = "nas.retrieve"

	// MCP interface
	MCPQuery        = "mcp.query"
	MCPResponse     = "mcp.response"
	MCPToolCall     = "mcp.tool.call"
	MCPToolResult   = "mcp.tool.result"

	// Orchestrator
	OrchTick        = "orchestrator.tick"
	OrchStatus      = "orchestrator.status"
	OrchScenario    = "orchestrator.scenario"
	OrchTime         = "orchestrator.time"
)
