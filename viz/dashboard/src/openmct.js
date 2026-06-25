import openmct from "openmct";
import { TelemetryBridge } from "./telemetry.js";

const telemetry = new TelemetryBridge();

openmct.install(openmct.plugins.LocalStorage());
openmct.install(openmct.plugins.MyItems());
openmct.install(openmct.plugins.Conductor({
  replayTimer: true,
  useRealtime: true,
}));
openmct.install(openmct.plugins.Generator());
openmct.install(openmct.plugins.SummaryWidget());
openmct.install(openmct.plugins.DisplayLayout());

openmct.setAssetPath("/");

const ISS_TELEMETRY = [
  {
    key: "dynamics",
    name: "Orbit & Attitude",
    measurements: [
      { key: "altitude_m", name: "Altitude", unit: "m", format: "float" },
      { key: "latitude_deg", name: "Latitude", unit: "deg", format: "float" },
      { key: "longitude_deg", name: "Longitude", unit: "deg", format: "float" },
      { key: "beta_angle_deg", name: "Beta Angle", unit: "deg", format: "float" },
      { key: "in_sun", name: "In Sun", unit: "", format: "enum" },
    ],
  },
  {
    key: "eps",
    name: "Power",
    measurements: [
      { key: "total_power_gen_w", name: "Solar Generation", unit: "W", format: "float" },
      { key: "total_power_load_w", name: "Load", unit: "W", format: "float" },
      { key: "battery_soc_pct", name: "Battery SOC", unit: "%", format: "percent" },
      { key: "bus_voltage_v", name: "Bus Voltage", unit: "V", format: "float" },
    ],
  },
  {
    key: "eclss",
    name: "ECLSS",
    measurements: [
      { key: "cabin_pressure_kpa", name: "Cabin Pressure", unit: "kPa", format: "float" },
      { key: "cabin_temp_c", name: "Temperature", unit: "°C", format: "float" },
      { key: "o2_partial_pressure_kpa", name: "O₂ Partial", unit: "kPa", format: "float" },
      { key: "co2_partial_pressure_kpa", name: "CO₂ Partial", unit: "kPa", format: "float" },
    ],
  },
  {
    key: "thermal",
    name: "Thermal",
    measurements: [
      { key: "loop_a_temp_in_c", name: "Loop A In", unit: "°C", format: "float" },
      { key: "loop_b_temp_in_c", name: "Loop B In", unit: "°C", format: "float" },
      { key: "radiator_angle_deg", name: "Radiator Angle", unit: "deg", format: "float" },
    ],
  },
  {
    key: "comms",
    name: "Communications",
    measurements: [
      { key: "s_band_uplink_kbps", name: "S-Band Uplink", unit: "kbps", format: "float" },
      { key: "s_band_downlink_kbps", name: "S-Band Downlink", unit: "kbps", format: "float" },
      { key: "ku_band_data_rate_mbps", name: "Ku-Band Rate", unit: "Mbps", format: "float" },
    ],
  },
  {
    key: "gnc",
    name: "GNC",
    measurements: [
      { key: "control_mode", name: "Control Mode", unit: "", format: "string" },
      { key: "cmg_momentum_nms", name: "CMG Momentum", unit: "Nms", format: "float" },
    ],
  },
];

function createTelemetryType(subsystem) {
  const provider = {
    supportsRequest: () => true,
    supportsSubscribe: () => true,

    request: (domainObject, options) => {
      return telemetry.request(subsystem.key, options);
    },

    subscribe: (domainObject, callback) => {
      return telemetry.subscribe(subsystem.key, callback);
    },
  };

  const objectProvider = {
    get: (identifier) => {
      return Promise.resolve({
        identifier,
        name: subsystem.name,
        type: "iss.telemetry",
        telemetry: {
          values: subsystem.measurements.map((m) => ({
            key: m.key,
            name: m.name,
            units: m.unit,
            format: m.format,
          })),
        },
      });
    },
  };

  openmct.objectTypes.addType("iss.telemetry", {
    name: "ISS Telemetry",
    description: "ISS subsystem telemetry point",
    cssClass: "icon-telemetry",
  });

  openmct.objects.addRoot({
    namespace: "",
    key: `iss.${subsystem.key}`,
  });

  openmct.objects.addProvider("", objectProvider);
  openmct.telemetry.addProvider({
    ...provider,
    key: `iss.${subsystem.key}`,
  });
}

ISS_TELEMETRY.forEach(createTelemetryType);

openmct.time.clock("realtime", { period: 100 });
openmct.time.timeSystem("utc", {
  key: "utc",
  name: "UTC",
  timeFormat: "utc",
  durationFormat: "duration",
});

openmct.theme("dark");

telemetry.connect().then(() => {
  console.log("Telemetry bridge connected");
}).catch((err) => {
  console.error("Failed to connect telemetry bridge:", err);
});

openmct.start();
