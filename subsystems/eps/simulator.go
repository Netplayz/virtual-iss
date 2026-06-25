package eps

import (
	"context"
	"encoding/json"
	"sync"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
)

type EPSState struct {
	Timestamp      int64              `json:"timestamp"`
	SolarPower     float64            `json:"solar_power"`
	SolarCurrent   float64            `json:"solar_current"`
	SolarVoltage   float64            `json:"solar_voltage"`
	BatterySOC     float64            `json:"battery_soc"`
	BatteryVoltage float64            `json:"battery_voltage"`
	BatteryTemp    float64            `json:"battery_temp"`
	TotalLoad      float64            `json:"total_load"`
	MaxPower       float64            `json:"max_power"`
	Shedding       bool               `json:"shedding"`
	BusVoltage     float64            `json:"bus_voltage"`
	InSun          bool               `json:"in_sun"`
	LoadStates     map[string]bool    `json:"load_states"`
	BetaAngle      float64            `json:"beta_angle"`
}

type DynamicsState struct {
	Timestamp int64   `json:"timestamp"`
	Lat       float64 `json:"lat"`
	Lon       float64 `json:"lon"`
	Alt       float64 `json:"alt"`
	BetaAngle float64 `json:"beta_angle"`
	InSun     bool    `json:"in_sun"`
}

type Command struct {
	Target  string `json:"target"`
	Action  string `json:"action"`
	Payload string `json:"payload,omitempty"`
}

type EPSSimulator struct {
	nc          *nats.Conn
	Solar       *SolarArray
	Batteries   []*Battery
	Bus         *DistributionBus
	State       EPSState
	mu          sync.RWMutex
	log         *logrus.Entry
	tickSub     *nats.Subscription
	cmdSub      *nats.Subscription
}

func NewEPSSimulator(nc *nats.Conn) *EPSSimulator {
	solar := NewSolarArray(8, 30.0, 0.30, 0.02)
	battery := NewBattery(0.85, 4000.0, 120.0, 48, 4)
	bus := NewDistributionBus(160.0, 30000.0)

	bus.AddLoad("command_handling", 500, 1)
	bus.AddLoad("guidance_nav", 1500, 1)
	bus.AddLoad("thermal_ctrl", 2000, 2)
	bus.AddLoad("life_support", 5000, 1)
	bus.AddLoad("payload_experiment", 3000, 3)
	bus.AddLoad("lighting", 1000, 4)
	bus.AddLoad("crew_computing", 800, 3)

	return &EPSSimulator{
		nc:        nc,
		Solar:     solar,
		Batteries: []*Battery{battery},
		Bus:       bus,
		State: EPSState{
			BatterySOC:     0.85,
			BusVoltage:     160.0,
			MaxPower:       bus.MaxPower,
			LoadStates:     make(map[string]bool),
		},
		log: logrus.WithField("subsystem", "eps"),
	}
}

func (e *EPSSimulator) Run(ctx context.Context) error {
	var err error
	e.tickSub, err = e.nc.Subscribe("orchestrator.tick", func(msg *nats.Msg) {
		e.handleTick()
	})
	if err != nil {
		return err
	}

	e.cmdSub, err = e.nc.Subscribe("command.uplink", func(msg *nats.Msg) {
		var cmd Command
		if err := json.Unmarshal(msg.Data, &cmd); err != nil {
			e.log.WithError(err).Error("failed to parse command")
			return
		}
		if cmd.Target == "eps" {
			e.handleCommand(cmd)
		}
	})
	if err != nil {
		return err
	}

	<-ctx.Done()
	return nil
}

func (e *EPSSimulator) handleTick() {
	e.mu.Lock()
	defer e.mu.Unlock()

	var dyn DynamicsState
	msg, err := e.nc.Request("telemetry.dynamics.state", nil, 100*time.Millisecond)
	if err == nil {
		json.Unmarshal(msg.Data, &dyn)
	}

	e.State.Timestamp = time.Now().UnixMilli()
	e.State.InSun = dyn.InSun
	e.State.BetaAngle = dyn.BetaAngle

	current, voltage, solarPower := e.Solar.ComputePower(dyn.BetaAngle, dyn.InSun, 1361.0)
	e.State.SolarCurrent = current
	e.State.SolarVoltage = voltage
	e.State.SolarPower = solarPower

	totalLoad := e.Bus.ComputeTotalLoad()
	e.State.TotalLoad = totalLoad

	for _, bat := range e.Batteries {
		if solarPower > totalLoad {
			excess := solarPower - totalLoad
			bat.Charge(excess, 1.0)
		} else if totalLoad > solarPower {
			deficit := totalLoad - solarPower
			bat.Discharge(deficit, 1.0)
		}
		bat.ThermalModel(20.0)
	}

	if len(e.Batteries) > 0 {
		bat := e.Batteries[0]
		e.State.BatterySOC = bat.GetSOC()
		e.State.BatteryVoltage = bat.GetVoltage()
		e.State.BatteryTemp = bat.Temp
	}

	if totalLoad > e.Bus.MaxPower {
		e.State.Shedding = true
		e.Bus.ShedLoad()
	} else if e.State.Shedding && len(e.Batteries) > 0 && e.Batteries[0].GetSOC() > 0.5 {
		e.Bus.RestoreLoad()
		if e.Bus.ComputeTotalLoad() < e.Bus.MaxPower {
			e.State.Shedding = false
		}
	}

	e.State.BusVoltage = e.Bus.BusVoltage
	e.State.TotalLoad = e.Bus.ComputeTotalLoad()

	e.State.LoadStates = make(map[string]bool)
	for _, l := range e.Bus.Loads {
		e.State.LoadStates[l.Name] = l.Active
	}

	data, err := json.Marshal(e.State)
	if err != nil {
		e.log.WithError(err).Error("failed to marshal EPS state")
		return
	}

	if err := e.nc.Publish("telemetry.eps.state", data); err != nil {
		e.log.WithError(err).Error("failed to publish EPS state")
	}
}

func (e *EPSSimulator) handleCommand(cmd Command) {
	e.mu.Lock()
	defer e.mu.Unlock()

	switch cmd.Action {
	case "POWER_ON":
		if cmd.Payload != "" {
			e.Bus.AddLoad(cmd.Payload, 500, 5)
			e.log.WithField("load", cmd.Payload).Info("power on")
		}
	case "POWER_OFF":
		if cmd.Payload != "" {
			e.Bus.RemoveLoad(cmd.Payload)
			e.log.WithField("load", cmd.Payload).Info("power off")
		}
	case "SHED_LOAD":
		e.Bus.ShedLoad()
		e.State.Shedding = true
		e.log.Info("load shed triggered")
	}
}
