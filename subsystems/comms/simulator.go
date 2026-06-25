package comms

import (
	"context"
	"encoding/json"
	"math"
	"sync"
	"time"

	"github.com/nats-io/nats.go"
	"github.com/sirupsen/logrus"
)

type CommsState struct {
	Timestamp      int64   `json:"timestamp"`
	ActiveTDRS     string  `json:"active_tdrs"`
	InTDRSView     bool    `json:"in_tdrs_view"`
	NextPassSec    float64 `json:"next_pass_sec"`
	SBandActive    bool    `json:"s_band_active"`
	KuBandActive   bool    `json:"ku_band_active"`
	DownlinkRate   float64 `json:"downlink_rate"`
	UplinkRate     float64 `json:"uplink_rate"`
	Latency        float64 `json:"latency"`
	RxPower        float64 `json:"rx_power"`
	SNR            float64 `json:"snr"`
	HasVideo       bool    `json:"has_video"`
	DistanceM      float64 `json:"distance_m"`
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

type CommsSimulator struct {
	nc      *nats.Conn
	TDRS    *TDRSSystem
	SBand   *SBandLink
	KuBand  *KuBandLink
	State   CommsState
	mu      sync.RWMutex
	log     *logrus.Entry
}

func NewCommsSimulator(nc *nats.Conn) *CommsSimulator {
	return &CommsSimulator{
		nc:     nc,
		TDRS:   NewTDRSSystem(),
		SBand:  NewSBandLink(),
		KuBand: NewKuBandLink(),
		State: CommsState{
			SBandActive:  true,
			UplinkRate:   256000,
			DownlinkRate: 1024000,
			DistanceM:    400e3,
		},
		log: logrus.WithField("subsystem", "comms"),
	}
}

func (c *CommsSimulator) Run(ctx context.Context) error {
	var err error
	if _, err = c.nc.Subscribe("orchestrator.tick", func(msg *nats.Msg) {
		c.handleTick()
	}); err != nil {
		return err
	}

	if _, err = c.nc.Subscribe("command.uplink", func(msg *nats.Msg) {
		var cmd Command
		if err := json.Unmarshal(msg.Data, &cmd); err != nil {
			c.log.WithError(err).Error("failed to parse command")
			return
		}
		if cmd.Target == "comms" {
			c.handleCommand(cmd)
		}
	}); err != nil {
		return err
	}

	<-ctx.Done()
	return nil
}

func (c *CommsSimulator) handleTick() {
	c.mu.Lock()
	defer c.mu.Unlock()

	var dyn DynamicsState
	msg, err := c.nc.Request("telemetry.dynamics.state", nil, 100*time.Millisecond)
	if err == nil {
		json.Unmarshal(msg.Data, &dyn)
	}

	c.State.Timestamp = time.Now().UnixMilli()

	c.State.ActiveTDRS = c.TDRS.GetActiveTDRS()
	inView, nextPass := c.TDRS.ComputeCoverage(dyn.Lat, dyn.Lon, dyn.Alt)
	c.State.InTDRSView = inView
	c.State.NextPassSec = nextPass

	if dyn.Alt > 0 {
		c.State.DistanceM = dyn.Alt
	} else {
		c.State.DistanceM = 400e3
	}

	geoSyncAlt := 35786000.0
	distanceToTDRS := math.Sqrt(geoSyncAlt*geoSyncAlt + c.State.DistanceM*c.State.DistanceM)

	c.State.Latency = GetLatency(distanceToTDRS)

	if inView && c.State.SBandActive {
		rxPower, snr := ComputeLinkBudget(50.0, 30.0, 40.0, distanceToTDRS, 2.25e9)
		c.State.RxPower = rxPower
		c.State.SNR = snr
		c.State.DownlinkRate = c.SBand.DownlinkRate
		c.State.UplinkRate = c.SBand.UplinkRate
	} else {
		c.State.RxPower = 0
		c.State.SNR = -100
		c.State.DownlinkRate = 0
		c.State.UplinkRate = 0
	}

	c.State.KuBandActive = c.KuBand.IsActive
	c.State.SBandActive = c.SBand.IsActive
	c.State.HasVideo = c.KuBand.HasVideo && c.KuBand.IsActive

	data, err := json.Marshal(c.State)
	if err != nil {
		c.log.WithError(err).Error("failed to marshal Comms state")
		return
	}

	if err := c.nc.Publish("telemetry.comms.state", data); err != nil {
		c.log.WithError(err).Error("failed to publish Comms state")
	}
}

func (c *CommsSimulator) handleCommand(cmd Command) {
	c.mu.Lock()
	defer c.mu.Unlock()

	switch cmd.Action {
	case "S_BAND_ON":
		c.SBand.Activate()
		c.State.SBandActive = true
		c.log.Info("S-band activated")
	case "S_BAND_OFF":
		c.SBand.Deactivate()
		c.State.SBandActive = false
		c.log.Info("S-band deactivated")
	case "KU_BAND_ON":
		c.KuBand.Activate()
		c.State.KuBandActive = true
		c.log.Info("Ku-band activated")
	case "KU_BAND_OFF":
		c.KuBand.Deactivate()
		c.State.KuBandActive = false
		c.log.Info("Ku-band deactivated")
	}
}
