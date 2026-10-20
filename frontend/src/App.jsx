import React, { useEffect, useMemo, useRef, useState } from "react";
import mqtt from "mqtt";
import {
  Activity,
  AlertTriangle,
  Bell,
  Bluetooth,
  Bot,
  CheckCircle2,
  CircleGauge,
  Cpu,
  Droplets,
  EthernetPort,
  Gauge,
  HardDrive,
  HeartPulse,
  Info,
  Lightbulb,
  Link2,
  Loader2,
  LockKeyhole,
  MessageSquare,
  Network,
  Power,
  Radio,
  RefreshCw,
  Server,
  Settings2,
  ShieldCheck,
  Signal,
  Thermometer,
  Wifi,
  WifiOff,
  Zap,
  Volume2,
  Waves,
  XCircle,
} from "lucide-react";

import {
  Area,
  AreaChart,
  CartesianGrid,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

/* =========================================================
   SMART MACHINE MQTT DASHBOARD
   Final Hardware:
   - ESP32 #1
   - ESP32 #2
   - DHT11 x2
   - Motor x2
   - LED x2
   - Buzzer x1
   - RS-485 / Modbus RTU
   - MQTT backup communication
   ========================================================= */

/* ---------------- MQTT CONFIGURATION ---------------- */

const MQTT_URL = "wss://test.mosquitto.org:8081/mqtt";

const TOPICS = {
  machine01Telemetry: "smart-machine/machine01/telemetry",
  machine01Status: "smart-machine/machine01/status",
  machine01Event: "smart-machine/machine01/event",

  machine02Telemetry: "smart-machine/machine02/telemetry",
  machine02Status: "smart-machine/machine02/status",
  machine02Event: "smart-machine/machine02/event",

  systemCommunication: "smart-machine/system/communication",
  systemFailover: "smart-machine/system/failover",
};

/* =========================================================
   HELPERS
   ========================================================= */

const clamp = (value, min, max) => {
  const number = Number(value);

  if (!Number.isFinite(number)) {
    return min;
  }

  return Math.min(Math.max(number, min), max);
};

const safeJsonParse = (message) => {
  try {
    return JSON.parse(message);
  } catch {
    return null;
  }
};

const formatTime = (date = new Date()) =>
  date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });

const normalizeBoolean = (value) => {
  if (typeof value === "boolean") return value;

  if (typeof value === "number") {
    return value !== 0;
  }

  if (typeof value === "string") {
    const normalized = value.toLowerCase().trim();

    return ["true", "1", "on", "active", "enabled", "yes"].includes(
      normalized
    );
  }

  return false;
};

const getNumber = (data, keys, fallback = null) => {
  for (const key of keys) {
    if (
      data &&
      data[key] !== undefined &&
      data[key] !== null &&
      Number.isFinite(Number(data[key]))
    ) {
      return Number(data[key]);
    }
  }

  return fallback;
};

const getValue = (data, keys, fallback = null) => {
  for (const key of keys) {
    if (data && data[key] !== undefined && data[key] !== null) {
      return data[key];
    }
  }

  return fallback;
};

const getBoolean = (data, keys, fallback = false) => {
  for (const key of keys) {
    if (data && data[key] !== undefined && data[key] !== null) {
      return normalizeBoolean(data[key]);
    }
  }

  return fallback;
};

/* =========================================================
   INITIAL MACHINE STATE
   ========================================================= */

const createMachine = (id, name) => ({
  id,
  name,

  online: false,

  temperature: null,
  humidity: null,

  motor: false,
  led: false,
  buzzer: id === "machine01" ? false : null,

  communication: "Waiting",
  protocol: "—",

  lastSeen: null,

  rssi: null,

  uptime: null,

  messageCount: 0,

  error: null,
});

/* =========================================================
   MAIN COMPONENT
   ========================================================= */

export default function App() {
  /* ---------------- STATE ---------------- */

  const [mqttConnected, setMqttConnected] = useState(false);
  const [mqttConnecting, setMqttConnecting] = useState(false);
  const [mqttError, setMqttError] = useState("");

  const [rs485Status, setRs485Status] = useState("Waiting");
  const [activeProtocol, setActiveProtocol] = useState("—");
  const [failoverStatus, setFailoverStatus] = useState("Standby");

  const [machine01, setMachine01] = useState(
    createMachine("machine01", "Machine 01")
  );

  const [machine02, setMachine02] = useState(
    createMachine("machine02", "Machine 02")
  );

  const [events, setEvents] = useState([]);

  const [temperatureHistory, setTemperatureHistory] = useState([]);
  const [humidityHistory, setHumidityHistory] = useState([]);

  const [lastUpdated, setLastUpdated] = useState(null);

  const clientRef = useRef(null);

  /* =========================================================
     EVENT LOGGER
     ========================================================= */

  const addEvent = (type, message, machine = "System") => {
    const event = {
      id: `${Date.now()}-${Math.random()}`,
      time: formatTime(),
      type,
      message,
      machine,
    };

    setEvents((previous) => [event, ...previous].slice(0, 50));
  };

  /* =========================================================
     MQTT CONNECT
     ========================================================= */

  const connectMQTT = () => {
    if (clientRef.current) {
      try {
        clientRef.current.end(true);
      } catch {
        // Ignore disconnect errors.
      }

      clientRef.current = null;
    }

    setMqttConnecting(true);
    setMqttError("");

    addEvent("INFO", "Connecting to MQTT broker...", "MQTT");

    const clientId =
      "smart-machine-dashboard-" +
      Math.random().toString(16).slice(2) +
      Date.now();

    const client = mqtt.connect(MQTT_URL, {
      clientId,
      clean: true,
      connectTimeout: 10000,
      reconnectPeriod: 5000,
      keepalive: 30,
    });

    clientRef.current = client;

    client.on("connect", () => {
      setMqttConnected(true);
      setMqttConnecting(false);
      setMqttError("");

      addEvent("SUCCESS", "MQTT broker connected", "MQTT");

      const subscriptions = Object.values(TOPICS);

      client.subscribe(subscriptions, { qos: 0 }, (error) => {
        if (error) {
          console.error("MQTT subscription error:", error);

          setMqttError("Topic subscription failed");

          addEvent(
            "ERROR",
            "MQTT topic subscription failed",
            "MQTT"
          );

          return;
        }

        addEvent(
          "SUCCESS",
          `${subscriptions.length} MQTT topics subscribed`,
          "MQTT"
        );
      });
    });

    client.on("reconnect", () => {
      setMqttConnecting(true);

      addEvent(
        "WARNING",
        "MQTT reconnecting...",
        "MQTT"
      );
    });

    client.on("close", () => {
      setMqttConnected(false);
      setMqttConnecting(false);

      addEvent(
        "WARNING",
        "MQTT connection closed",
        "MQTT"
      );
    });

    client.on("offline", () => {
      setMqttConnected(false);

      addEvent(
        "WARNING",
        "MQTT broker unavailable",
        "MQTT"
      );
    });

    client.on("error", (error) => {
      console.error("MQTT error:", error);

      setMqttConnected(false);
      setMqttConnecting(false);

      setMqttError(error?.message || "MQTT connection error");

      addEvent(
        "ERROR",
        error?.message || "MQTT connection error",
        "MQTT"
      );
    });

    client.on("message", (topic, payload) => {
      processMQTTMessage(topic, payload.toString());
    });
  };

  /* =========================================================
     MQTT MESSAGE PROCESSOR
     ========================================================= */

  const processMQTTMessage = (topic, rawMessage) => {
    const data = safeJsonParse(rawMessage);

    if (!data) {
      addEvent(
        "INFO",
        `Received non-JSON MQTT message on ${topic}`,
        "MQTT"
      );

      return;
    }

    setLastUpdated(new Date());

    /* ---------------- MACHINE 01 TELEMETRY ---------------- */

    if (topic === TOPICS.machine01Telemetry) {
      updateMachineTelemetry("machine01", data);

      return;
    }

    /* ---------------- MACHINE 02 TELEMETRY ---------------- */

    if (topic === TOPICS.machine02Telemetry) {
      updateMachineTelemetry("machine02", data);

      return;
    }

    /* ---------------- MACHINE 01 STATUS ---------------- */

    if (topic === TOPICS.machine01Status) {
      updateMachineStatus("machine01", data);

      return;
    }

    /* ---------------- MACHINE 02 STATUS ---------------- */

    if (topic === TOPICS.machine02Status) {
      updateMachineStatus("machine02", data);

      return;
    }

    /* ---------------- MACHINE EVENTS ---------------- */

    if (topic === TOPICS.machine01Event) {
      handleMachineEvent("Machine 01", data);

      return;
    }

    if (topic === TOPICS.machine02Event) {
      handleMachineEvent("Machine 02", data);

      return;
    }

    /* ---------------- SYSTEM COMMUNICATION ---------------- */

    if (topic === TOPICS.systemCommunication) {
      handleCommunication(data);

      return;
    }

    /* ---------------- FAILOVER ---------------- */

    if (topic === TOPICS.systemFailover) {
      handleFailover(data);

      return;
    }
  };

  /* =========================================================
     UPDATE MACHINE TELEMETRY
     ========================================================= */

  const updateMachineTelemetry = (machineId, data) => {
    const temperature = getNumber(
      data,
      [
        "temperature",
        "temp",
        "temperatureC",
        "temperature_c",
      ],
      null
    );

    const humidity = getNumber(
      data,
      [
        "humidity",
        "hum",
        "humidityPercent",
        "humidity_percent",
      ],
      null
    );

    const motor = getBoolean(
      data,
      ["motor", "motorState", "motor_state"],
      false
    );

    const led = getBoolean(
      data,
      ["led", "ledState", "led_state"],
      false
    );

    const buzzer =
      machineId === "machine01"
        ? getBoolean(
            data,
            ["buzzer", "buzzerState", "buzzer_state"],
            false
          )
        : null;

    const rssi = getNumber(
      data,
      ["rssi", "wifiRSSI", "wifi_rssi"],
      null
    );

    const uptime = getValue(
      data,
      ["uptime", "uptimeSeconds", "uptime_seconds"],
      null
    );

    const now = new Date();

    const update = {
      online: true,
      temperature,
      humidity,
      motor,
      led,
      buzzer,
      communication: "Connected",
      protocol:
        getValue(data, ["protocol", "communicationProtocol"], null) ||
        "MQTT",
      lastSeen: now,
      rssi,
      uptime,
      messageCount: undefined,
      error: null,
    };

    if (machineId === "machine01") {
      setMachine01((previous) => ({
        ...previous,
        ...update,
        messageCount: previous.messageCount + 1,
      }));
    } else {
      setMachine02((previous) => ({
        ...previous,
        ...update,
        messageCount: previous.messageCount + 1,
      }));
    }

    addEvent(
      "DATA",
      `Temperature ${temperature ?? "—"}°C | Humidity ${
        humidity ?? "—"
      }%`,
      machineId === "machine01" ? "Machine 01" : "Machine 02"
    );

    /* ---------------- GRAPH DATA ---------------- */

    setTemperatureHistory((previous) => {
      const next = [
        ...previous,
        {
          time: formatTime(now),
          machine01:
            machineId === "machine01"
              ? temperature
              : previous.at(-1)?.machine01 ?? null,
          machine02:
            machineId === "machine02"
              ? temperature
              : previous.at(-1)?.machine02 ?? null,
        },
      ];

      return next.slice(-30);
    });

    setHumidityHistory((previous) => {
      const next = [
        ...previous,
        {
          time: formatTime(now),
          machine01:
            machineId === "machine01"
              ? humidity
              : previous.at(-1)?.machine01 ?? null,
          machine02:
            machineId === "machine02"
              ? humidity
              : previous.at(-1)?.machine02 ?? null,
        },
      ];

      return next.slice(-30);
    });
  };

  /* =========================================================
     MACHINE STATUS
     ========================================================= */

  const updateMachineStatus = (machineId, data) => {
    const online = getBoolean(
      data,
      ["online", "connected", "status"],
      true
    );

    const protocol =
      getValue(
        data,
        ["protocol", "communicationProtocol"],
        null
      ) || "MQTT";

    const communication =
      getValue(
        data,
        ["communication", "connection", "state"],
        null
      ) || (online ? "Connected" : "Offline");

    const update = {
      online,
      protocol,
      communication,
      lastSeen: new Date(),
      error: online ? null : "Machine offline",
    };

    if (machineId === "machine01") {
      setMachine01((previous) => ({
        ...previous,
        ...update,
      }));

      addEvent(
        online ? "SUCCESS" : "ERROR",
        online
          ? "Machine 01 online"
          : "Machine 01 offline",
        "Machine 01"
      );
    } else {
      setMachine02((previous) => ({
        ...previous,
        ...update,
      }));

      addEvent(
        online ? "SUCCESS" : "ERROR",
        online
          ? "Machine 02 online"
          : "Machine 02 offline",
        "Machine 02"
      );
    }
  };

  /* =========================================================
     MACHINE EVENT
     ========================================================= */

  const handleMachineEvent = (machineName, data) => {
    const message =
      getValue(
        data,
        ["message", "event", "description"],
        "Machine event received"
      );

    addEvent(
      getValue(data, ["type", "level"], "INFO").toUpperCase(),
      message,
      machineName
    );
  };

  /* =========================================================
     COMMUNICATION
     ========================================================= */

  const handleCommunication = (data) => {
    const status =
      getValue(
        data,
        ["status", "state"],
        "Unknown"
      );

    const protocol =
      getValue(
        data,
        ["protocol", "activeProtocol"],
        "—"
      );

    setRs485Status(status);
    setActiveProtocol(protocol);

    addEvent(
      "COMMUNICATION",
      `Protocol: ${protocol} | Status: ${status}`,
      "RS-485"
    );
  };

  /* =========================================================
     FAILOVER
     ========================================================= */

  const handleFailover = (data) => {
    const status =
      getValue(
        data,
        ["status", "state"],
        "Standby"
      );

    const protocol =
      getValue(
        data,
        ["protocol", "activeProtocol"],
        null
      );

    setFailoverStatus(status);

    if (protocol) {
      setActiveProtocol(protocol);
    }

    addEvent(
      status.toLowerCase().includes("fail")
        ? "WARNING"
        : "INFO",
      `Failover status: ${status}${
        protocol ? ` | Active protocol: ${protocol}` : ""
      }`,
      "System"
    );
  };

  /* =========================================================
     CONNECT ON PAGE LOAD
     ========================================================= */

  useEffect(() => {
    connectMQTT();

    return () => {
      if (clientRef.current) {
        try {
          clientRef.current.end(true);
        } catch {
          // Ignore cleanup errors.
        }
      }
    };
  }, []);

  /* =========================================================
     MACHINE ONLINE TIMEOUT
     ========================================================= */

  useEffect(() => {
    const interval = setInterval(() => {
      const now = Date.now();

      setMachine01((previous) => {
        if (
          previous.lastSeen &&
          now - previous.lastSeen.getTime() > 20000
        ) {
          return {
            ...previous,
            online: false,
            communication: "Offline",
          };
        }

        return previous;
      });

      setMachine02((previous) => {
        if (
          previous.lastSeen &&
          now - previous.lastSeen.getTime() > 20000
        ) {
          return {
            ...previous,
            online: false,
            communication: "Offline",
          };
        }

        return previous;
      });
    }, 5000);

    return () => clearInterval(interval);
  }, []);

  /* =========================================================
     DERIVED SYSTEM STATE
     ========================================================= */

  const onlineCount = useMemo(
    () =>
      Number(machine01.online) +
      Number(machine02.online),
    [machine01.online, machine02.online]
  );

  const motorCount = useMemo(
    () =>
      Number(machine01.motor) +
      Number(machine02.motor),
    [machine01.motor, machine02.motor]
  );

  const ledCount = useMemo(
    () =>
      Number(machine01.led) +
      Number(machine02.led),
    [machine01.led, machine02.led]
  );

  /* =========================================================
     UI
     ========================================================= */

  return (
    <div className="dashboard-shell">
      <style>{GLOBAL_STYLES}</style>

      {/* =====================================================
          SIDEBAR
          ===================================================== */}

      <aside className="sidebar">
        <div className="brand">
          <div className="brand-logo">
            <Cpu size={25} />
          </div>

          <div>
            <div className="brand-title">
              SMART MACHINE
            </div>

            <div className="brand-subtitle">
              MQTT CONTROL CENTER
            </div>
          </div>
        </div>

        <div className="sidebar-section">
          <div className="sidebar-label">
            SYSTEM
          </div>

          <a className="side-link active">
            <CircleGauge size={18} />
            Dashboard
          </a>

          <a className="side-link">
            <Activity size={18} />
            Live Monitoring
          </a>

          <a className="side-link">
            <Network size={18} />
            Communication
          </a>

          <a className="side-link">
            <MessageSquare size={18} />
            Event Logs
          </a>
        </div>

        <div className="sidebar-section">
          <div className="sidebar-label">
            MACHINES
          </div>

          <a className="side-link">
            <Bot size={18} />
            Machine 01
          </a>

          <a className="side-link">
            <Bot size={18} />
            Machine 02
          </a>
        </div>

        <div className="sidebar-bottom">
          <div className="system-mini-card">
            <div className="mini-icon">
              <ShieldCheck size={17} />
            </div>

            <div>
              <strong>System Protected</strong>
              <span>Industrial M2M</span>
            </div>
          </div>
        </div>
      </aside>

      {/* =====================================================
          MAIN CONTENT
          ===================================================== */}

      <main className="main-content">

        {/* HEADER */}

        <header className="topbar">
          <div>
            <div className="eyebrow">
              INDUSTRIAL M2M COMMUNICATION
            </div>

            <h1>
              Smart Machine Dashboard
            </h1>

            <p className="header-description">
              Real-time monitoring of two connected machines,
              environmental sensors and communication health.
            </p>
          </div>

          <div className="header-actions">

            <div
              className={`mqtt-pill ${
                mqttConnected
                  ? "connected"
                  : "disconnected"
              }`}
            >
              {mqttConnected ? (
                <Wifi size={17} />
              ) : (
                <WifiOff size={17} />
              )}

              <span>
                MQTT{" "}
                {mqttConnected
                  ? "CONNECTED"
                  : "OFFLINE"}
              </span>
            </div>

            <button
              className="refresh-button"
              onClick={connectMQTT}
              disabled={mqttConnecting}
            >
              {mqttConnecting ? (
                <Loader2
                  size={18}
                  className="spin"
                />
              ) : (
                <RefreshCw size={18} />
              )}

              Reconnect
            </button>

          </div>
        </header>

        {/* MQTT ERROR */}

        {mqttError && (
          <div className="alert-banner warning">
            <AlertTriangle size={19} />

            <div>
              <strong>MQTT connection issue</strong>
              <span>{mqttError}</span>
            </div>
          </div>
        )}

        {/* =====================================================
            OVERVIEW CARDS
            ===================================================== */}

        <section className="overview-grid">

          <MetricCard
            icon={<Bot size={21} />}
            label="Machines Online"
            value={`${onlineCount}/2`}
            status={
              onlineCount === 2
                ? "All systems online"
                : `${2 - onlineCount} machine offline`
            }
            positive={onlineCount === 2}
          />

          <MetricCard
            icon={<Radio size={21} />}
            label="RS-485 / Modbus"
            value={rs485Status}
            status={`Active: ${activeProtocol}`}
            positive={
              String(rs485Status)
                .toLowerCase()
                .includes("connect")
            }
          />

          <MetricCard
            icon={<Power size={21} />}
            label="Motors Running"
            value={`${motorCount}/2`}
            status={
              motorCount > 0
                ? "Motor activity detected"
                : "All motors stopped"
            }
            positive={motorCount === 0}
          />

          <MetricCard
            icon={<Lightbulb size={21} />}
            label="LED Indicators"
            value={`${ledCount}/2`}
            status={
              ledCount > 0
                ? "Active indicators"
                : "Indicators idle"
            }
            positive
          />

        </section>

        {/* =====================================================
            MACHINE CARDS
            ===================================================== */}

        <section className="section-block">

          <SectionHeading
            title="Machine Status"
            subtitle="Live status of each ESP32 industrial node"
            icon={<Server size={19} />}
          />

          <div className="machine-grid">

            <MachineCard
              machine={machine01}
              machineNumber="01"
              colorClass="blue"
            />

            <MachineCard
              machine={machine02}
              machineNumber="02"
              colorClass="purple"
            />

          </div>
        </section>

        {/* =====================================================
            SENSOR GRAPH
            ===================================================== */}

        <section className="section-block">

          <SectionHeading
            title="Environmental Monitoring"
            subtitle="Real-time DHT11 temperature and humidity"
            icon={<Activity size={19} />}
          />

          <div className="charts-grid">

            <ChartCard
              title="Temperature"
              unit="°C"
              icon={<Thermometer size={18} />}
              data={temperatureHistory}
              machine01Key="machine01"
              machine02Key="machine02"
              yDomain={["auto", "auto"]}
            />

            <ChartCard
              title="Humidity"
              unit="%"
              icon={<Droplets size={18} />}
              data={humidityHistory}
              machine01Key="machine01"
              machine02Key="machine02"
              yDomain={[0, 100]}
            />

          </div>
        </section>

        {/* =====================================================
            COMMUNICATION PANEL
            ===================================================== */}

        <section className="section-block">

          <SectionHeading
            title="Communication & Failover"
            subtitle="Industrial communication health"
            icon={<Network size={19} />}
          />

          <div className="communication-grid">

            <CommunicationCard
              icon={<EthernetPort size={21} />}
              title="RS-485 / Modbus RTU"
              value={rs485Status}
              description="Primary machine-to-machine communication"
              active={String(rs485Status)
                .toLowerCase()
                .includes("connect")}
            />

            <CommunicationCard
              icon={<Wifi size={21} />}
              title="MQTT"
              value={
                mqttConnected
                  ? "Connected"
                  : "Disconnected"
              }
              description="Backup / cloud communication channel"
              active={mqttConnected}
            />

            <CommunicationCard
              icon={<Zap size={21} />}
              title="Failover"
              value={failoverStatus}
              description="Automatic communication recovery"
              active={
                String(failoverStatus)
                  .toLowerCase()
                  .includes("standby")
              }
            />

          </div>
        </section>

        {/* =====================================================
            EVENT LOG
            ===================================================== */}

        <section className="section-block">

          <SectionHeading
            title="Live Event Stream"
            subtitle="Latest communication and machine events"
            icon={<MessageSquare size={19} />}
          />

          <div className="event-panel">

            {events.length === 0 ? (
              <div className="empty-events">
                <Info size={28} />

                <strong>
                  Waiting for machine data
                </strong>

                <span>
                  MQTT events will appear here when
                  the ESP32 devices start publishing.
                </span>
              </div>
            ) : (
              <div className="event-list">

                {events.map((event) => (
                  <div
                    className="event-row"
                    key={event.id}
                  >
                    <EventIcon type={event.type} />

                    <div className="event-content">
                      <div className="event-message">
                        {event.message}
                      </div>

                      <div className="event-meta">
                        <span>
                          {event.machine}
                        </span>

                        <span>•</span>

                        <span>
                          {event.time}
                        </span>
                      </div>
                    </div>

                    <span
                      className={`event-type ${String(
                        event.type
                      ).toLowerCase()}`}
                    >
                      {event.type}
                    </span>
                  </div>
                ))}

              </div>
            )}

          </div>
        </section>

        {/* =====================================================
            FOOTER
            ===================================================== */}

        <footer className="footer">

          <div>
            SMART MACHINE COMMUNICATION SYSTEM
          </div>

          <div>
            MQTT • RS-485 • MODBUS RTU • ESP32
          </div>

          <div>
            {lastUpdated
              ? `Last update: ${formatTime(lastUpdated)}`
              : "Waiting for telemetry"}
          </div>

        </footer>

      </main>
    </div>
  );
}

/* =========================================================
   METRIC CARD
   ========================================================= */

function MetricCard({
  icon,
  label,
  value,
  status,
  positive,
}) {
  return (
    <div className="metric-card">

      <div className="metric-top">
        <div className="metric-icon">
          {icon}
        </div>

        <span
          className={`status-dot ${
            positive ? "green" : "yellow"
          }`}
        />
      </div>

      <div className="metric-label">
        {label}
      </div>

      <div className="metric-value">
        {value}
      </div>

      <div className="metric-status">
        {positive ? (
          <CheckCircle2 size={14} />
        ) : (
          <AlertTriangle size={14} />
        )}

        {status}
      </div>

    </div>
  );
}

/* =========================================================
   MACHINE CARD
   ========================================================= */

function MachineCard({
  machine,
  machineNumber,
  colorClass,
}) {
  return (
    <div className={`machine-card ${colorClass}`}>

      <div className="machine-header">

        <div className="machine-title-group">

          <div className="machine-icon">
            <Bot size={24} />
          </div>

          <div>
            <div className="machine-overline">
              ESP32 NODE
            </div>

            <h3>
              Machine {machineNumber}
            </h3>
          </div>

        </div>

        <div
          className={`online-badge ${
            machine.online
              ? "online"
              : "offline"
          }`}
        >
          <span />

          {machine.online
            ? "ONLINE"
            : "OFFLINE"}
        </div>

      </div>

      {/* SENSOR VALUES */}

      <div className="sensor-grid">

        <SensorBox
          icon={<Thermometer size={19} />}
          label="Temperature"
          value={
            machine.temperature !== null
              ? `${machine.temperature.toFixed(1)}°C`
              : "—"
          }
        />

        <SensorBox
          icon={<Droplets size={19} />}
          label="Humidity"
          value={
            machine.humidity !== null
              ? `${machine.humidity.toFixed(1)}%`
              : "—"
          }
        />

      </div>

      {/* ACTUATORS */}

      <div className="actuator-section">

        <div className="subsection-title">
          ACTUATORS
        </div>

        <div className="actuator-grid">

          <Actuator
            icon={<Power size={17} />}
            label="Motor"
            active={machine.motor}
          />

          <Actuator
            icon={<Lightbulb size={17} />}
            label="LED"
            active={machine.led}
          />

          {machineNumber === "01" && (
            <Actuator
              icon={<Volume2 size={17} />}
              label="Buzzer"
              active={machine.buzzer}
            />
          )}

        </div>

      </div>

      {/* CONNECTION */}

      <div className="machine-footer">

        <div className="connection-item">
          <Link2 size={15} />

          <span>
            {machine.protocol || "—"}
          </span>
        </div>

        <div className="connection-item">
          <Signal size={15} />

          <span>
            {machine.rssi !== null
              ? `${machine.rssi} dBm`
              : "—"}
          </span>
        </div>

        <div className="connection-item">
          <HeartPulse size={15} />

          <span>
            {machine.communication}
          </span>
        </div>

      </div>

    </div>
  );
}

/* =========================================================
   SENSOR BOX
   ========================================================= */

function SensorBox({
  icon,
  label,
  value,
}) {
  return (
    <div className="sensor-box">

      <div className="sensor-icon">
        {icon}
      </div>

      <div>
        <span className="sensor-label">
          {label}
        </span>

        <strong className="sensor-value">
          {value}
        </strong>
      </div>

    </div>
  );
}

/* =========================================================
   ACTUATOR
   ========================================================= */

function Actuator({
  icon,
  label,
  active,
}) {
  return (
    <div
      className={`actuator ${
        active ? "active" : ""
      }`}
    >

      <div className="actuator-icon">
        {icon}
      </div>

      <div className="actuator-info">

        <span>
          {label}
        </span>

        <strong>
          {active ? "ON" : "OFF"}
        </strong>

      </div>

      <div
        className={`actuator-status ${
          active ? "active" : ""
        }`}
      />

    </div>
  );
}

/* =========================================================
   CHART CARD
   ========================================================= */

function ChartCard({
  title,
  unit,
  icon,
  data,
  machine01Key,
  machine02Key,
  yDomain,
}) {
  return (
    <div className="chart-card">

      <div className="chart-header">

        <div className="chart-title">

          <div className="chart-icon">
            {icon}
          </div>

          <div>
            <strong>{title}</strong>

            <span>
              Live values
            </span>
          </div>

        </div>

        <div className="chart-unit">
          {unit}
        </div>

      </div>

      <div className="chart-area">

        {data.length === 0 ? (
          <div className="chart-empty">

            <Activity size={25} />

            <span>
              Waiting for telemetry...
            </span>

          </div>
        ) : (
          <ResponsiveContainer
            width="100%"
            height="100%"
          >
            <AreaChart data={data}>

              <defs>

                <linearGradient
                  id={`machine01-${title}`}
                  x1="0"
                  y1="0"
                  x2="0"
                  y2="1"
                >
                  <stop
                    offset="0%"
                    stopColor="#5b8cff"
                    stopOpacity={0.35}
                  />

                  <stop
                    offset="100%"
                    stopColor="#5b8cff"
                    stopOpacity={0}
                  />
                </linearGradient>

                <linearGradient
                  id={`machine02-${title}`}
                  x1="0"
                  y1="0"
                  x2="0"
                  y2="1"
                >
                  <stop
                    offset="0%"
                    stopColor="#b36cff"
                    stopOpacity={0.35}
                  />

                  <stop
                    offset="100%"
                    stopColor="#b36cff"
                    stopOpacity={0}
                  />
                </linearGradient>

              </defs>

              <CartesianGrid
                strokeDasharray="3 3"
                stroke="rgba(255,255,255,0.06)"
              />

              <XAxis
                dataKey="time"
                stroke="#64748b"
                tick={{
                  fontSize: 10,
                }}
                tickLine={false}
                axisLine={false}
              />

              <YAxis
                stroke="#64748b"
                tick={{
                  fontSize: 10,
                }}
                tickLine={false}
                axisLine={false}
                domain={yDomain}
              />

              <Tooltip
                contentStyle={{
                  background:
                    "rgba(15, 23, 42, 0.96)",
                  border:
                    "1px solid rgba(255,255,255,0.1)",
                  borderRadius: "12px",
                  color: "#fff",
                }}
              />

              <Area
                type="monotone"
                dataKey={machine01Key}
                name="Machine 01"
                stroke="#5b8cff"
                strokeWidth={2}
                fill={`url(#machine01-${title})`}
                connectNulls
              />

              <Area
                type="monotone"
                dataKey={machine02Key}
                name="Machine 02"
                stroke="#b36cff"
                strokeWidth={2}
                fill={`url(#machine02-${title})`}
                connectNulls
              />

            </AreaChart>
          </ResponsiveContainer>
        )}

      </div>

      <div className="chart-legend">

        <span>
          <i className="legend-dot machine-one" />
          Machine 01
        </span>

        <span>
          <i className="legend-dot machine-two" />
          Machine 02
        </span>

      </div>

    </div>
  );
}

/* =========================================================
   COMMUNICATION CARD
   ========================================================= */

function CommunicationCard({
  icon,
  title,
  value,
  description,
  active,
}) {
  return (
    <div className="communication-card">

      <div className="communication-icon">
        {icon}
      </div>

      <div className="communication-content">

        <div className="communication-title">
          {title}
        </div>

        <div
          className={`communication-value ${
            active ? "active" : ""
          }`}
        >
          <span />

          {value}
        </div>

        <p>
          {description}
        </p>

      </div>

    </div>
  );
}

/* =========================================================
   SECTION HEADING
   ========================================================= */

function SectionHeading({
  title,
  subtitle,
  icon,
}) {
  return (
    <div className="section-heading">

      <div className="section-heading-icon">
        {icon}
      </div>

      <div>
        <h2>{title}</h2>

        <p>{subtitle}</p>
      </div>

    </div>
  );
}

/* =========================================================
   EVENT ICON
   ========================================================= */

function EventIcon({ type }) {
  const normalized = String(type).toLowerCase();

  if (normalized === "error") {
    return (
      <div className="event-icon error">
        <XCircle size={17} />
      </div>
    );
  }

  if (normalized === "warning") {
    return (
      <div className="event-icon warning">
        <AlertTriangle size={17} />
      </div>
    );
  }

  if (
    normalized === "success" ||
    normalized === "data"
  ) {
    return (
      <div className="event-icon success">
        <CheckCircle2 size={17} />
      </div>
    );
  }

  if (normalized === "communication") {
    return (
      <div className="event-icon communication">
        <Network size={17} />
      </div>
    );
  }

  return (
    <div className="event-icon info">
      <Info size={17} />
    </div>
  );
}

/* =========================================================
   COMPLETE DASHBOARD CSS
   ========================================================= */

const GLOBAL_STYLES = `
* {
  box-sizing: border-box;
}

html {
  scroll-behavior: smooth;
}

body {
  margin: 0;
  padding: 0;
  background: #060914;
  color: #e5e7eb;
  font-family:
    Inter,
    ui-sans-serif,
    system-ui,
    -apple-system,
    BlinkMacSystemFont,
    "Segoe UI",
    sans-serif;
}

button,
input {
  font-family: inherit;
}

.dashboard-shell {
  min-height: 100vh;
  display: flex;
  background:
    radial-gradient(
      circle at 15% 10%,
      rgba(49, 91, 180, 0.14),
      transparent 30%
    ),
    radial-gradient(
      circle at 85% 30%,
      rgba(132, 64, 190, 0.12),
      transparent 30%
    ),
    #060914;
}

/* ================= SIDEBAR ================= */

.sidebar {
  width: 250px;
  min-height: 100vh;
  position: fixed;
  left: 0;
  top: 0;
  bottom: 0;
  padding: 25px 18px;
  background:
    rgba(8, 12, 25, 0.92);
  border-right:
    1px solid rgba(255,255,255,0.07);
  backdrop-filter: blur(24px);
  z-index: 20;
  display: flex;
  flex-direction: column;
}

.brand {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 6px 8px 30px;
}

.brand-logo {
  width: 44px;
  height: 44px;
  border-radius: 13px;
  display: flex;
  align-items: center;
  justify-content: center;
  background:
    linear-gradient(
      135deg,
      rgba(79, 139, 255, 0.25),
      rgba(158, 84, 255, 0.2)
    );
  border:
    1px solid rgba(105, 150, 255, 0.3);
  color: #7ca7ff;
  box-shadow:
    0 0 30px rgba(72, 113, 255, 0.15);
}

.brand-title {
  font-size: 13px;
  font-weight: 800;
  letter-spacing: 0.12em;
  color: #f8fafc;
}

.brand-subtitle {
  font-size: 8px;
  letter-spacing: 0.15em;
  color: #64748b;
  margin-top: 4px;
}

.sidebar-section {
  margin-bottom: 26px;
}

.sidebar-label {
  font-size: 9px;
  letter-spacing: 0.16em;
  font-weight: 800;
  color: #475569;
  padding: 0 11px;
  margin-bottom: 9px;
}

.side-link {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 43px;
  padding: 0 12px;
  margin: 3px 0;
  border-radius: 11px;
  color: #7d8aa2;
  font-size: 13px;
  text-decoration: none;
  transition:
    background 0.2s ease,
    color 0.2s ease,
    transform 0.2s ease;
}

.side-link:hover {
  background: rgba(255,255,255,0.04);
  color: #dbeafe;
  transform: translateX(2px);
}

.side-link.active {
  background:
    linear-gradient(
      90deg,
      rgba(72, 117, 255, 0.16),
      rgba(72, 117, 255, 0.04)
    );
  color: #90b4ff;
  border:
    1px solid rgba(91, 140, 255, 0.12);
}

.sidebar-bottom {
  margin-top: auto;
}

.system-mini-card {
  display: flex;
  gap: 10px;
  align-items: center;
  padding: 12px;
  border-radius: 13px;
  background: rgba(255,255,255,0.025);
  border: 1px solid rgba(255,255,255,0.06);
}

.mini-icon {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
  color: #6ee7b7;
  background: rgba(52,211,153,0.08);
}

.system-mini-card strong {
  display: block;
  font-size: 10px;
  color: #cbd5e1;
}

.system-mini-card span {
  display: block;
  margin-top: 3px;
  font-size: 9px;
  color: #64748b;
}

/* ================= MAIN ================= */

.main-content {
  width: calc(100% - 250px);
  margin-left: 250px;
  padding: 30px 36px 25px;
  min-width: 0;
}

.topbar {
  display: flex;
  justify-content: space-between;
  gap: 30px;
  align-items: flex-start;
  margin-bottom: 27px;
}

.eyebrow {
  color: #5b8cff;
  font-size: 9px;
  font-weight: 800;
  letter-spacing: 0.2em;
  margin-bottom: 8px;
}

.topbar h1 {
  margin: 0;
  font-size: clamp(25px, 3vw, 36px);
  letter-spacing: -0.04em;
  color: #f8fafc;
}

.header-description {
  color: #64748b;
  font-size: 12px;
  max-width: 650px;
  line-height: 1.7;
  margin: 9px 0 0;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-shrink: 0;
}

.mqtt-pill,
.refresh-button {
  height: 39px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 0 13px;
  font-size: 11px;
  font-weight: 700;
}

.mqtt-pill {
  border: 1px solid rgba(255,255,255,0.07);
  background: rgba(255,255,255,0.025);
}

.mqtt-pill.connected {
  color: #6ee7b7;
}

.mqtt-pill.disconnected {
  color: #fca5a5;
}

.refresh-button {
  border:
    1px solid rgba(91,140,255,0.22);
  color: #9dbaff;
  background: rgba(91,140,255,0.08);
  cursor: pointer;
}

.refresh-button:hover {
  background: rgba(91,140,255,0.14);
}

.refresh-button:disabled {
  opacity: 0.6;
  cursor: wait;
}

.spin {
  animation: spin 1s linear infinite;
}

@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

/* ================= ALERT ================= */

.alert-banner {
  display: flex;
  align-items: flex-start;
  gap: 11px;
  padding: 13px 15px;
  margin-bottom: 20px;
  border-radius: 13px;
}

.alert-banner.warning {
  color: #fbbf24;
  background: rgba(245,158,11,0.07);
  border: 1px solid rgba(245,158,11,0.15);
}

.alert-banner strong {
  display: block;
  font-size: 11px;
}

.alert-banner span {
  display: block;
  color: #94a3b8;
  margin-top: 3px;
  font-size: 10px;
}

/* ================= OVERVIEW ================= */

.overview-grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 14px;
}

.metric-card {
  position: relative;
  overflow: hidden;
  min-height: 150px;
  padding: 18px;
  border-radius: 17px;
  background:
    linear-gradient(
      145deg,
      rgba(255,255,255,0.055),
      rgba(255,255,255,0.018)
    );
  border:
    1px solid rgba(255,255,255,0.07);
  box-shadow:
    0 18px 60px rgba(0,0,0,0.18);
}

.metric-card::after {
  content: "";
  position: absolute;
  width: 120px;
  height: 120px;
  border-radius: 50%;
  right: -65px;
  top: -65px;
  background: rgba(91,140,255,0.07);
  filter: blur(5px);
}

.metric-top {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.metric-icon {
  width: 38px;
  height: 38px;
  border-radius: 11px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #8eafff;
  background: rgba(91,140,255,0.09);
}

.status-dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
}

.status-dot.green {
  background: #34d399;
  box-shadow: 0 0 12px rgba(52,211,153,0.65);
}

.status-dot.yellow {
  background: #fbbf24;
  box-shadow: 0 0 12px rgba(251,191,36,0.55);
}

.metric-label {
  margin-top: 15px;
  font-size: 10px;
  font-weight: 700;
  color: #64748b;
  text-transform: uppercase;
  letter-spacing: 0.08em;
}

.metric-value {
  margin-top: 4px;
  font-size: 25px;
  font-weight: 800;
  letter-spacing: -0.04em;
  color: #f8fafc;
}

.metric-status {
  display: flex;
  align-items: center;
  gap: 5px;
  margin-top: 6px;
  color: #64748b;
  font-size: 9px;
}

/* ================= SECTIONS ================= */

.section-block {
  margin-top: 30px;
}

.section-heading {
  display: flex;
  align-items: center;
  gap: 11px;
  margin-bottom: 14px;
}

.section-heading-icon {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  color: #8eafff;
  background: rgba(91,140,255,0.08);
}

.section-heading h2 {
  margin: 0;
  color: #e2e8f0;
  font-size: 15px;
  letter-spacing: -0.02em;
}

.section-heading p {
  margin: 3px 0 0;
  color: #475569;
  font-size: 10px;
}

/* ================= MACHINES ================= */

.machine-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.machine-card {
  padding: 19px;
  border-radius: 19px;
  background:
    linear-gradient(
      145deg,
      rgba(255,255,255,0.055),
      rgba(255,255,255,0.018)
    );
  border: 1px solid rgba(255,255,255,0.07);
  box-shadow:
    0 20px 70px rgba(0,0,0,0.2);
}

.machine-card.blue {
  box-shadow:
    inset 0 1px 0 rgba(91,140,255,0.05),
    0 20px 70px rgba(0,0,0,0.2);
}

.machine-card.purple {
  box-shadow:
    inset 0 1px 0 rgba(179,108,255,0.05),
    0 20px 70px rgba(0,0,0,0.2);
}

.machine-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 15px;
  padding-bottom: 17px;
  border-bottom: 1px solid rgba(255,255,255,0.05);
}

.machine-title-group {
  display: flex;
  align-items: center;
  gap: 11px;
}

.machine-icon {
  width: 44px;
  height: 44px;
  border-radius: 13px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #9ab8ff;
  background: rgba(91,140,255,0.09);
}

.machine-card.purple .machine-icon {
  color: #c39aff;
  background: rgba(179,108,255,0.09);
}

.machine-overline {
  font-size: 8px;
  letter-spacing: 0.15em;
  font-weight: 800;
  color: #475569;
}

.machine-header h3 {
  margin: 3px 0 0;
  font-size: 17px;
  color: #f1f5f9;
}

.online-badge {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 6px 9px;
  border-radius: 8px;
  font-size: 8px;
  font-weight: 800;
  letter-spacing: 0.08em;
}

.online-badge span {
  width: 6px;
  height: 6px;
  border-radius: 50%;
}

.online-badge.online {
  color: #6ee7b7;
  background: rgba(52,211,153,0.07);
}

.online-badge.online span {
  background: #34d399;
  box-shadow: 0 0 9px rgba(52,211,153,0.7);
}

.online-badge.offline {
  color: #fca5a5;
  background: rgba(248,113,113,0.07);
}

.online-badge.offline span {
  background: #f87171;
}

/* ================= SENSORS ================= */

.sensor-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
  margin-top: 16px;
}

.sensor-box {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 13px;
  border-radius: 13px;
  background: rgba(255,255,255,0.025);
  border: 1px solid rgba(255,255,255,0.045);
}

.sensor-icon {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
  color: #7da2ff;
  background: rgba(91,140,255,0.07);
}

.sensor-label {
  display: block;
  color: #64748b;
  font-size: 8px;
  text-transform: uppercase;
  letter-spacing: 0.08em;
  font-weight: 700;
}

.sensor-value {
  display: block;
  margin-top: 3px;
  color: #e2e8f0;
  font-size: 17px;
}

/* ================= ACTUATORS ================= */

.actuator-section {
  margin-top: 16px;
}

.subsection-title {
  margin-bottom: 8px;
  color: #475569;
  font-size: 8px;
  letter-spacing: 0.14em;
  font-weight: 800;
}

.actuator-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
}

.actuator {
  min-height: 58px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 9px;
  border-radius: 11px;
  background: rgba(255,255,255,0.022);
  border: 1px solid rgba(255,255,255,0.045);
}

.actuator.active {
  border-color: rgba(52,211,153,0.15);
  background: rgba(52,211,153,0.045);
}

.actuator-icon {
  color: #64748b;
}

.actuator.active .actuator-icon {
  color: #6ee7b7;
}

.actuator-info {
  min-width: 0;
  flex: 1;
}

.actuator-info span {
  display: block;
  color: #64748b;
  font-size: 8px;
}

.actuator-info strong {
  display: block;
  margin-top: 2px;
  color: #cbd5e1;
  font-size: 10px;
}

.actuator-status {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: #334155;
}

.actuator-status.active {
  background: #34d399;
  box-shadow: 0 0 9px rgba(52,211,153,0.7);
}

/* ================= MACHINE FOOTER ================= */

.machine-footer {
  display: flex;
  align-items: center;
  gap: 15px;
  flex-wrap: wrap;
  margin-top: 15px;
  padding-top: 13px;
  border-top: 1px solid rgba(255,255,255,0.045);
}

.connection-item {
  display: flex;
  align-items: center;
  gap: 5px;
  color: #64748b;
  font-size: 9px;
}

/* ================= CHARTS ================= */

.charts-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px;
}

.chart-card {
  min-height: 335px;
  padding: 17px;
  border-radius: 17px;
  background: rgba(255,255,255,0.025);
  border: 1px solid rgba(255,255,255,0.065);
}

.chart-header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.chart-title {
  display: flex;
  align-items: center;
  gap: 9px;
}

.chart-icon {
  width: 34px;
  height: 34px;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
  color: #8eafff;
  background: rgba(91,140,255,0.08);
}

.chart-title strong {
  display: block;
  color: #e2e8f0;
  font-size: 11px;
}

.chart-title span {
  display: block;
  color: #475569;
  font-size: 8px;
  margin-top: 2px;
}

.chart-unit {
  color: #94a3b8;
  font-size: 11px;
  font-weight: 700;
}

.chart-area {
  height: 245px;
  margin-top: 8px;
}

.chart-empty {
  height: 100%;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  gap: 9px;
  color: #475569;
  font-size: 10px;
}

.chart-legend {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 18px;
  color: #64748b;
  font-size: 9px;
}

.chart-legend span {
  display: flex;
  align-items: center;
  gap: 6px;
}

.legend-dot {
  width: 7px;
  height: 7px;
  display: inline-block;
  border-radius: 50%;
}

.legend-dot.machine-one {
  background: #5b8cff;
}

.legend-dot.machine-two {
  background: #b36cff;
}

/* ================= COMMUNICATION ================= */

.communication-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 13px;
}

.communication-card {
  display: flex;
  gap: 12px;
  padding: 16px;
  border-radius: 15px;
  background: rgba(255,255,255,0.025);
  border: 1px solid rgba(255,255,255,0.06);
}

.communication-icon {
  width: 38px;
  height: 38px;
  flex-shrink: 0;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  color: #8eafff;
  background: rgba(91,140,255,0.08);
}

.communication-title {
  color: #cbd5e1;
  font-size: 11px;
  font-weight: 700;
}

.communication-value {
  display: flex;
  align-items: center;
  gap: 6px;
  margin-top: 5px;
  color: #fbbf24;
  font-size: 10px;
  font-weight: 700;
}

.communication-value.active {
  color: #6ee7b7;
}

.communication-value span {
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: currentColor;
}

.communication-content p {
  color: #475569;
  font-size: 8px;
  line-height: 1.5;
  margin: 5px 0 0;
}

/* ================= EVENTS ================= */

.event-panel {
  border-radius: 16px;
  background: rgba(255,255,255,0.025);
  border: 1px solid rgba(255,255,255,0.06);
  overflow: hidden;
}

.empty-events {
  min-height: 180px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  gap: 8px;
  color: #475569;
  text-align: center;
}

.empty-events strong {
  color: #64748b;
  font-size: 12px;
}

.empty-events span {
  font-size: 9px;
}

.event-list {
  max-height: 420px;
  overflow-y: auto;
}

.event-row {
  display: flex;
  align-items: center;
  gap: 11px;
  padding: 11px 14px;
  border-bottom: 1px solid rgba(255,255,255,0.04);
}

.event-row:last-child {
  border-bottom: none;
}

.event-icon {
  width: 31px;
  height: 31px;
  flex-shrink: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 9px;
}

.event-icon.success {
  color: #6ee7b7;
  background: rgba(52,211,153,0.07);
}

.event-icon.error {
  color: #fca5a5;
  background: rgba(248,113,113,0.07);
}

.event-icon.warning {
  color: #fbbf24;
  background: rgba(245,158,11,0.07);
}

.event-icon.communication {
  color: #8eafff;
  background: rgba(91,140,255,0.07);
}

.event-icon.info {
  color: #94a3b8;
  background: rgba(148,163,184,0.07);
}

.event-content {
  flex: 1;
  min-width: 0;
}

.event-message {
  color: #cbd5e1;
  font-size: 10px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.event-meta {
  display: flex;
  gap: 5px;
  margin-top: 3px;
  color: #475569;
  font-size: 8px;
}

.event-type {
  font-size: 7px;
  font-weight: 800;
  letter-spacing: 0.08em;
}

.event-type.success,
.event-type.data {
  color: #6ee7b7;
}

.event-type.error {
  color: #fca5a5;
}

.event-type.warning {
  color: #fbbf24;
}

.event-type.communication {
  color: #8eafff;
}

/* ================= FOOTER ================= */

.footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 25px 2px 10px;
  color: #334155;
  font-size: 8px;
  letter-spacing: 0.04em;
}

/* ================= RESPONSIVE ================= */

@media (max-width: 1200px) {
  .overview-grid {
    grid-template-columns: repeat(2, 1fr);
  }

  .communication-grid {
    grid-template-columns: 1fr;
  }
}

@media (max-width: 950px) {
  .sidebar {
    width: 72px;
    padding: 20px 10px;
  }

  .brand {
    justify-content: center;
    padding-bottom: 25px;
  }

  .brand > div:last-child,
  .sidebar-label,
  .side-link:not(.active) {
    display: none;
  }

  .side-link {
    justify-content: center;
    padding: 0;
  }

  .system-mini-card {
    justify-content: center;
  }

  .system-mini-card > div:last-child {
    display: none;
  }

  .main-content {
    width: calc(100% - 72px);
    margin-left: 72px;
  }
}

@media (max-width: 760px) {
  .main-content {
    padding: 20px 15px;
  }

  .topbar {
    flex-direction: column;
  }

  .header-actions {
    width: 100%;
  }

  .mqtt-pill,
  .refresh-button {
    flex: 1;
    justify-content: center;
  }

  .overview-grid {
    grid-template-columns: 1fr 1fr;
  }

  .machine-grid,
  .charts-grid {
    grid-template-columns: 1fr;
  }

  .footer {
    flex-direction: column;
    align-items: flex-start;
  }
}

@media (max-width: 520px) {
  .sidebar {
    display: none;
  }

  .main-content {
    width: 100%;
    margin-left: 0;
    padding: 17px 12px;
  }

  .overview-grid {
    grid-template-columns: 1fr;
  }

  .sensor-grid {
    grid-template-columns: 1fr;
  }

  .actuator-grid {
    grid-template-columns: 1fr;
  }

  .machine-footer {
    gap: 10px;
  }

  .topbar h1 {
    font-size: 25px;
  }
}
`;