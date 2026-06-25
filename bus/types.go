package bus

import "time"

// --- Telemetry structs ---

type Vector3 struct {
	X float64 `json:"x"`
	Y float64 `json:"y"`
	Z float64 `json:"z"`
}

type DynamicsState struct {
	Timestamp     time.Time `json:"timestamp"`
	SimTimeSec    float64   `json:"sim_time_sec"`
	PositionECI   Vector3   `json:"position_eci_m"`
	VelocityECI   Vector3   `json:"velocity_eci_ms"`
	Quaternion    [4]float64 `json:"quaternion"`
	AngularRate   Vector3   `json:"angular_rate_rads"`
	BetaAngle     float64   `json:"beta_angle_deg"`
	Altitude      float64   `json:"altitude_m"`
	Latitude      float64   `json:"latitude_deg"`
	Longitude     float64   `json:"longitude_deg"`
	InSun         bool      `json:"in_sun"`
}

type GNCState struct {
	Timestamp      time.Time `json:"timestamp"`
	AttitudeEst    [4]float64 `json:"attitude_est"`
	RateEst        Vector3   `json:"rate_est"`
	ControlTorque  Vector3   `json:"control_torque_nm"`
	CMGMomentum    Vector3   `json:"cmg_momentum_nms"`
	CMGConfig      [4]float64 `json:"cmg_gimbal_angles"`
	ControlMode    string    `json:"control_mode"`
}

type EPSState struct {
	Timestamp        time.Time `json:"timestamp"`
	SolarArrayCurr   float64   `json:"solar_array_current_a"`
	SolarArrayVolt   float64   `json:"solar_array_voltage_v"`
	TotalPowerGen    float64   `json:"total_power_gen_w"`
	TotalPowerLoad   float64   `json:"total_power_load_w"`
	BatterySOC       float64   `json:"battery_soc_pct"`
	BatteryTemp      float64   `json:"battery_temp_c"`
	BusVoltage       float64   `json:"bus_voltage_v"`
	LoadShedActive   bool      `json:"load_shed_active"`
}

type ECLSSState struct {
	Timestamp    time.Time `json:"timestamp"`
	CabinPress   float64   `json:"cabin_pressure_kpa"`
	CabinTemp    float64   `json:"cabin_temp_c"`
	CabinHumid   float64   `json:"cabin_humidity_pct"`
	O2Partial    float64   `json:"o2_partial_pressure_kpa"`
	CO2Partial   float64   `json:"co2_partial_pressure_kpa"`
	O2Rate       float64   `json:"o2_production_rate_kg_day"`
	WaterProd    float64   `json:"water_production_l_day"`
	WaterStored  float64   `json:"water_stored_l"`
}

type ThermalState struct {
	Timestamp       time.Time `json:"timestamp"`
	LoopATempIn     float64   `json:"loop_a_temp_in_c"`
	LoopATempOut    float64   `json:"loop_a_temp_out_c"`
	LoopBTempIn     float64   `json:"loop_b_temp_in_c"`
	LoopBTempOut    float64   `json:"loop_b_temp_out_c"`
	RadiatorAngle   float64   `json:"radiator_angle_deg"`
	HeaterPower     float64   `json:"heater_power_w"`
	HeatRejection   float64   `json:"heat_rejection_w"`
}

type CommsState struct {
	Timestamp       time.Time `json:"timestamp"`
	TDRSSActive     bool      `json:"tdrss_active"`
	SLinkUplinkRate float64   `json:"s_band_uplink_kbps"`
	SLinkDownlinkRate float64 `json:"s_band_downlink_kbps"`
	KuLinkActive    bool      `json:"ku_band_active"`
	KuDataRate      float64   `json:"ku_band_data_rate_mbps"`
	SignalLatency   float64   `json:"signal_latency_ms"`
	NextPassSec     float64   `json:"next_pass_seconds"`
}

type CDHState struct {
	Timestamp       time.Time `json:"timestamp"`
	CmdsProcessed   int64     `json:"commands_processed"`
	TlmPacketsSent  int64     `json:"telemetry_packets_sent"`
	StoredCmdsQueued int64    `json:"stored_commands_queued"`
	ActiveSequences int       `json:"active_sequences"`
	LastCmdTime     time.Time `json:"last_command_time"`
	AlarmCount      int       `json:"alarm_count"`
}

type CrewState struct {
	Timestamp      time.Time `json:"timestamp"`
	CrewCount      int       `json:"crew_count"`
	CurrentActivity string   `json:"current_activity"`
	NextActivity   string    `json:"next_activity"`
	O2Consumed     float64   `json:"o2_consumed_kg"`
	CO2Produced    float64   `json:"co2_produced_kg"`
	WaterUsed      float64   `json:"water_used_l"`
	FoodConsumed   float64   `json:"food_consumed_kg"`
	SleepPeriod    bool      `json:"sleep_period"`
}

// --- Command structs ---

type Command struct {
	ID          string            `json:"id"`
	Source      string            `json:"source"`
	Target      string            `json:"target"`
	OpCode      string            `json:"opcode"`
	Args        map[string]any    `json:"args"`
	Priority    int               `json:"priority"`
	Timestamp   time.Time         `json:"timestamp"`
}

type CommandResponse struct {
	CmdID    string `json:"cmd_id"`
	Success  bool   `json:"success"`
	Message  string `json:"message"`
}

// --- Event structs ---

type Event struct {
	ID        string    `json:"id"`
	Source    string    `json:"source"`
	Severity  string    `json:"severity"` // INFO, WARN, ERROR, CRITICAL
	Message   string    `json:"message"`
	Timestamp time.Time `json:"timestamp"`
	Data      map[string]any `json:"data,omitempty"`
}

// --- Orchestrator ---

type TickMessage struct {
	SimTimeSec    float64 `json:"sim_time_sec"`
	RealTimeSec   float64 `json:"real_time_sec"`
	Rate          float64 `json:"rate"`
	TickNumber    int64   `json:"tick_number"`
	ScenarioID    string  `json:"scenario_id"`
}

type ScenarioDefinition struct {
	ID          string            `json:"id"`
	Name        string            `json:"name"`
	Description string            `json:"description"`
	InitialOrbit map[string]any   `json:"initial_orbit"`
	Subsystems  []string          `json:"subsystems"`
	DurationSec float64           `json:"duration_sec"`
	TimeScale   float64           `json:"time_scale"`
}

// --- NAS ---

type RecordRequest struct {
	Stream   string `json:"stream"`
	Subject  string `json:"subject"`
	Duration string `json:"duration"`
}

type ReplayRequest struct {
	Stream   string `json:"stream"`
	Start    string `json:"start"`
	End      string `json:"end"`
	Rate     float64 `json:"rate"`
}

type StoreRequest struct {
	Bucket  string `json:"bucket"`
	Key     string `json:"key"`
	Payload []byte `json:"payload"`
}

type RetrieveRequest struct {
	Bucket string `json:"bucket"`
	Key    string `json:"key"`
}

// --- MCP ---

type MCPQuery struct {
	Query  string            `json:"query"`
	Context map[string]any   `json:"context,omitempty"`
}

type MCPResponse struct {
	QueryID string      `json:"query_id"`
	Type    string      `json:"type"` // telemetry, command, scenario, system
	Data    any         `json:"data"`
	Error   string      `json:"error,omitempty"`
}

type MCPToolCall struct {
	Tool     string         `json:"tool"`
	Args     map[string]any `json:"args"`
	CallID   string         `json:"call_id"`
}

type MCPToolResult struct {
	CallID   string `json:"call_id"`
	Success  bool   `json:"success"`
	Output   any    `json:"output"`
	Error    string `json:"error,omitempty"`
}
