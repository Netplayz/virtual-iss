package eps

import (
	"math"
)

type Battery struct {
	SOC        float64 // State of Charge 0.0-1.0
	Capacity   float64 // Wh
	Voltage    float64 // V nominal
	Temp       float64 // C
	Cells      int
	Count      int // number of batteries in parallel
}

func NewBattery(soc, capacity, voltage float64, cells, count int) *Battery {
	return &Battery{
		SOC:      soc,
		Capacity: capacity,
		Voltage:  voltage,
		Temp:     20.0,
		Cells:    cells,
		Count:    count,
	}
}

func (b *Battery) Charge(powerW float64, dtSec float64) {
	if powerW <= 0 || b.SOC >= 1.0 {
		return
	}

	chargeEfficiency := 0.95
	maxChargeRate := b.Capacity * float64(b.Count) * 0.5 / 3600.0

	effectivePower := powerW * chargeEfficiency
	if effectivePower > maxChargeRate {
		effectivePower = maxChargeRate
	}

	energyIn := effectivePower * dtSec / 3600.0
	totalCapacity := b.Capacity * float64(b.Count)

	b.SOC += energyIn / totalCapacity
	if b.SOC > 1.0 {
		b.SOC = 1.0
	}
}

func (b *Battery) Discharge(loadW float64, dtSec float64) {
	if loadW <= 0 || b.SOC <= 0 {
		return
	}

	dischargeEfficiency := 0.95
	maxDischargeRate := b.Capacity * float64(b.Count) * 0.8 / 3600.0

	effectiveLoad := loadW / dischargeEfficiency
	if effectiveLoad > maxDischargeRate {
		effectiveLoad = maxDischargeRate
	}

	energyOut := effectiveLoad * dtSec / 3600.0
	totalCapacity := b.Capacity * float64(b.Count)

	b.SOC -= energyOut / totalCapacity
	if b.SOC < 0 {
		b.SOC = 0
	}
}

func (b *Battery) GetSOC() float64 {
	return b.SOC
}

func (b *Battery) GetVoltage() float64 {
	minSOC := 0.0
	maxSOC := 1.0
	minV := b.Voltage * 0.85
	maxV := b.Voltage * 1.15

	fraction := (b.SOC - minSOC) / (maxSOC - minSOC)
	return minV + fraction*(maxV-minV)
}

func (b *Battery) ThermalModel(ambientTemp float64) float64 {
	dissipationFactor := 0.05
	chargeDischargeRate := math.Abs(b.GetVoltage()*b.SOC*float64(b.Count)) * dissipationFactor

	thermalMass := float64(b.Cells*b.Count) * 0.01
	dT := chargeDischargeRate / thermalMass * 0.001

	coolingCoeff := 0.1
	b.Temp += dT - coolingCoeff*(b.Temp-ambientTemp)

	return b.Temp
}
