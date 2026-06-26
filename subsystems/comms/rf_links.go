package comms

import (
	"math"
)

type SBandLink struct {
	UplinkRate   float64 // bps
	DownlinkRate float64 // bps
	IsActive     bool
	FrequencyHz  float64
}

type KuBandLink struct {
	DataRateMbps float64
	IsActive     bool
	HasVideo     bool
	FrequencyHz  float64
}

func NewSBandLink() *SBandLink {
	return &SBandLink{
		UplinkRate:   256000,
		DownlinkRate: 1024000,
		IsActive:     true,
		FrequencyHz:  2.25e9,
	}
}

func NewKuBandLink() *KuBandLink {
	return &KuBandLink{
		DataRateMbps: 25.0,
		IsActive:     false,
		HasVideo:     true,
		FrequencyHz:  13.75e9,
	}
}

func ComputeLinkBudget(txPowerW, txGainDb, rxGainDb, distanceM, freqHz float64) (rxPowerW, snrDb float64) {
	c := 299792458.0

	txPowerDBW := 10 * math.Log10(txPowerW)

	fspl := 20*math.Log10(distanceM) + 20*math.Log10(freqHz) + 20*math.Log10(4*math.Pi/c)

	rxPowerDBW := txPowerDBW + txGainDb + rxGainDb - fspl

	rxPowerW = math.Pow(10, rxPowerDBW/10.0)

	bandwidthHz := 1e6
	n0 := -174.0 // dBm/Hz noise floor
	noiseFloorDBm := n0 + 10*math.Log10(bandwidthHz)
	noiseFloorDBW := noiseFloorDBm - 30.0

	snrDb = rxPowerDBW - noiseFloorDBW

	if snrDb < -50 {
		snrDb = -50
	}

	return rxPowerW, snrDb
}

func GetLatency(distanceM float64) float64 {
	c := 299792458.0
	return distanceM / c
}

func (s *SBandLink) Activate() {
	s.IsActive = true
}

func (s *SBandLink) Deactivate() {
	s.IsActive = false
}

func (k *KuBandLink) Activate() {
	k.IsActive = true
}

func (k *KuBandLink) Deactivate() {
	k.IsActive = false
}
