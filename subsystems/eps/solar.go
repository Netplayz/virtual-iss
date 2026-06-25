package eps

import (
	"math"
)

type SolarArray struct {
	PanelCount int
	Area       float64 // m^2 per panel
	Efficiency float64
	Degradation float64 // fraction lost per year
}

func NewSolarArray(panelCount int, area, efficiency, degradation float64) *SolarArray {
	return &SolarArray{
		PanelCount:  panelCount,
		Area:        area,
		Efficiency:  efficiency,
		Degradation: degradation,
	}
}

func (sa *SolarArray) ComputePower(betaAngleDeg float64, inSun bool, sunFlux float64) (current, voltage, power float64) {
	if !inSun {
		return 0, 0, 0
	}

	betaRad := betaAngleDeg * math.Pi / 180.0
	cosBeta := math.Cos(betaRad)
	if cosBeta < 0 {
		cosBeta = 0
	}

	totalArea := float64(sa.PanelCount) * sa.Area * cosBeta
	degradationFactor := 1.0 - sa.Degradation

	rawPower := totalArea * sa.Efficiency * sunFlux * degradationFactor

	voltage = 160.0
	current = rawPower / voltage
	power = voltage * current

	if power < 0 {
		power = 0
		current = 0
	}

	return current, voltage, power
}

func (sa *SolarArray) ArrayIVCurve(betaAngleDeg float64) (currents, voltages []float64) {
	betaRad := betaAngleDeg * math.Pi / 180.0
	cosBeta := math.Cos(betaRad)
	if cosBeta < 0 {
		cosBeta = 0
	}

	totalArea := float64(sa.PanelCount) * sa.Area * cosBeta
	degradationFactor := 1.0 - sa.Degradation

	sunFlux := 1361.0
	isc := totalArea * sa.Efficiency * sunFlux * degradationFactor / 160.0
	voc := 160.0

	for i := 0; i <= 20; i++ {
		fraction := float64(i) / 20.0
		v := fraction * voc * 1.2
		var iVal float64
		if v <= voc {
			iVal = isc * (1.0 - math.Exp(-v/(voc*0.05)))
		} else {
			iVal = 0
		}
		currents = append(currents, iVal)
		voltages = append(voltages, v)
	}

	return currents, voltages
}

func (sa *SolarArray) BetaAngleTracking(currentBetaDeg, targetBetaDeg float64) float64 {
	diff := targetBetaDeg - currentBetaDeg
	driveRate := 0.5
	maxDrive := 10.0

	if math.Abs(diff) < 0.1 {
		return 0
	}

	driveAngle := math.Copysign(1.0, diff) * math.Min(math.Abs(diff), driveRate)
	if math.Abs(driveAngle) > maxDrive {
		driveAngle = math.Copysign(maxDrive, driveAngle)
	}

	return driveAngle
}
