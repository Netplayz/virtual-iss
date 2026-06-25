package comms

import (
	"math"
	"math/rand"
	"time"
)

type TDRSSatellite struct {
	Name          string
	Longitude     float64 // geostationary longitude
	BeamWidthDeg  float64
	Active        bool
}

type TDREPass struct {
	StartTime time.Time
	EndTime   time.Time
}

type TDRSSystem struct {
	Satellites []*TDRSSatellite
	ActiveLink string
	Schedule   []TDREPass
}

func NewTDRSSystem() *TDRSSystem {
	return &TDRSSystem{
		Satellites: []*TDRSSatellite{
			{Name: "TDRS-1", Longitude: -41.0, BeamWidthDeg: 45.0, Active: true},
			{Name: "TDRS-2", Longitude: -174.0, BeamWidthDeg: 45.0, Active: true},
			{Name: "TDRS-3", Longitude: 85.0, BeamWidthDeg: 45.0, Active: true},
		},
		ActiveLink: "TDRS-1",
	}
}

func (ts *TDRSSystem) ComputeCoverage(latDeg, lonDeg, altM float64) (inView bool, nextPassSec float64) {
	for _, sat := range ts.Satellites {
		if !sat.Active {
			continue
		}

		lonDiff := math.Abs(lonDeg - sat.Longitude)
		if lonDiff > 180 {
			lonDiff = 360 - lonDiff
		}

		issLatRad := latDeg * math.Pi / 180.0
		issAltKm := altM / 1000.0

		R := 6371.0
		theta := math.Acos(R / (R + issAltKm))
		maxLonDiff := theta * 180.0 / math.Pi

		if lonDiff <= maxLonDiff {
			latFactor := math.Cos(issLatRad)
			if latFactor < 0 {
				latFactor = 0
			}
			latFactor = 1.0 - math.Abs(latDeg)*0.005
			if latFactor < 0.2 {
				latFactor = 0.2
			}

			if lonDiff <= sat.BeamWidthDeg*latFactor/2.0 {
				ts.ActiveLink = sat.Name
				return true, 0
			}
		}
	}

	orbitPeriod := 5400.0
	randomJitter := rand.Float64() * 1800.0
	nextPassSec = orbitPeriod/4.0 + randomJitter

	return false, nextPassSec
}

func (ts *TDRSSystem) GetActiveTDRS() string {
	return ts.ActiveLink
}

func (ts *TDRSSystem) SchedulePass(durationSec float64) (startTime, endTime time.Time) {
	startTime = time.Now().Add(time.Duration(rand.Float64() * 3600 * float64(time.Second)))
	endTime = startTime.Add(time.Duration(durationSec) * time.Second)

	ts.Schedule = append(ts.Schedule, TDREPass{
		StartTime: startTime,
		EndTime:   endTime,
	})

	return startTime, endTime
}
