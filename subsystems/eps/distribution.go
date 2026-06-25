package eps

import (
	"sort"
)

type LoadEntry struct {
	Name     string
	PowerW   float64
	Priority int
	Active   bool
}

type DistributionBus struct {
	BusVoltage float64
	Loads      []*LoadEntry
	MaxPower   float64
}

func NewDistributionBus(busVoltage, maxPower float64) *DistributionBus {
	return &DistributionBus{
		BusVoltage: busVoltage,
		Loads:      []*LoadEntry{},
		MaxPower:   maxPower,
	}
}

func (db *DistributionBus) AddLoad(name string, powerW float64, priority int) {
	for _, l := range db.Loads {
		if l.Name == name {
			l.PowerW = powerW
			l.Priority = priority
			l.Active = true
			return
		}
	}
	db.Loads = append(db.Loads, &LoadEntry{
		Name:     name,
		PowerW:   powerW,
		Priority: priority,
		Active:   true,
	})
}

func (db *DistributionBus) RemoveLoad(name string) {
	for i, l := range db.Loads {
		if l.Name == name {
			db.Loads = append(db.Loads[:i], db.Loads[i+1:]...)
			return
		}
	}
}

func (db *DistributionBus) ComputeTotalLoad() float64 {
	var total float64
	for _, l := range db.Loads {
		if l.Active {
			total += l.PowerW
		}
	}
	return total
}

func (db *DistributionBus) ShedLoad() {
	sort.SliceStable(db.Loads, func(i, j int) bool {
		return db.Loads[i].Priority > db.Loads[j].Priority
	})

	for db.ComputeTotalLoad() > db.MaxPower {
		var lowest *LoadEntry
		for _, l := range db.Loads {
			if l.Active && (lowest == nil || l.Priority < lowest.Priority) {
				lowest = l
			}
		}
		if lowest == nil {
			break
		}
		lowest.Active = false
	}
}

func (db *DistributionBus) RestoreLoad() {
	sort.SliceStable(db.Loads, func(i, j int) bool {
		return db.Loads[i].Priority < db.Loads[j].Priority
	})

	for _, l := range db.Loads {
		if !l.Active {
			l.Active = true
			if db.ComputeTotalLoad() > db.MaxPower {
				l.Active = false
				break
			}
		}
	}
}
