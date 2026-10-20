import React, { useEffect, useMemo, useRef, useState } from "react";
import { createRoot } from "react-dom/client";
import mqtt from "mqtt";

import {
  Activity,
  Bell,
  Cpu,
  Droplets,
  Gauge,
  Radio,
  RefreshCw,
  Server,
  Thermometer,
  Wifi,
  Zap,
  CircleCheck,
  CircleX,
  ArrowRight,
  Clock3,
} from "lucide-react";

import {
  ResponsiveContainer,
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  CartesianGrid,
} from "recharts";

import "./index.css";

/* =========================================================
   CONFIGURATION
========================================================= */

const MQTT_URL = "wss://test.mosquitto.org:8081/mqtt";

const MQTT_TOPICS = [
  "smart-machine/machine01/telemetry",
  "smart-machine/machine01/status",
  "smart-machine/machine01/event",

  "smart-machine/machine02/telemetry",
  "smart-machine/machine02/status",
  "smart-machine/machine02/event",

  "smart-machine/system/communication",
  "smart-machine/system/failover",
];

const API_URL = "http://localhost:5000/api/status";

/* =========================================================
   INITIAL MACHINE STATE
========================================================= */

const createInitialMachine = () => ({
  online: false,
  temperature: null,
  humidity: null,
  relay: false,
  led: false,
  buzzer: false,
  button: false,
  protocol: "WAITING",
  lastSeen: null,
});

/* =========================================================
   APP
========================================================= */

function App() {
  const mqttClientRef = useRef(null);

  const [mqttState, setMqttState] = useState("Connecting");
  const [mqttPackets, setMqttPackets] = useState(0);

  const [machines, setMachines] = useState({
    1: createInitialMachine(),
    2: createInitialMachine(),
  });

  const [communication, setCommunication] = useState({
    rs485: "WAITING",
    modbus: "WAITING",
    mqtt: "CONNECTING",
    failover: "STANDBY",
  });

  const [apiState, setApiState] = useState("Checking");

  const [events, setEvents] = useState([]);

  const [chartData, setChartData] = useState([]);

  /* =======================================================
     EVENT LOGGER
  ======================================================= */

  const addEvent = (message, type = "info") => {
    setEvents((previous) => [
      {
        id: Date.now() + Math.random(),
        time: new Date().toLocaleTimeString(),
        message,
        type,
      },
      ...previous,
    ].slice(0, 15));
  };

  /* =======================================================
     MACHINE UPDATE
  ======================================================= */

  const updateMachine = (machineId, data) => {
    setMachines((previous) => ({
      ...previous,
      [machineId]: {
        ...previous[machineId],
        ...data,
        lastSeen: new Date(),
      },
    }));
  };

  /* =======================================================
     MQTT MESSAGE HANDLER
  ======================================================= */

  const handleMqttMessage = (topic, messageBuffer) => {
    setMqttPackets((previous) => previous + 1);

    let payload;

    try {
      payload = JSON.parse(messageBuffer.toString());
    } catch {
      addEvent(
        `Invalid JSON received on ${topic}`,
        "error"
      );
      return;
    }

    const machineId =
      Number(payload.machine) ||
      (topic.includes("machine01")
        ? 1
        : topic.includes("machine02")
        ? 2
        : null);

    /* -----------------------------------------------
       TELEMETRY
    ----------------------------------------------- */

    if (topic.includes("/telemetry") && machineId) {
      const temperature = Number(
        payload.temperature ?? payload.temp ?? 0
      );

      const humidity = Number(
        payload.humidity ?? payload.hum ?? 0
      );

      updateMachine(machineId, {
        online: true,
        temperature,
        humidity,
        relay: Boolean(payload.relay),
        led: Boolean(payload.led),
        buzzer: Boolean(payload.buzzer),
        button: Boolean(payload.button),
        protocol: payload.protocol || "MQTT",
      });

      setChartData((previous) => [
        ...previous.slice(-29),
        {
          time: new Date().toLocaleTimeString(),
          machine01:
            machineId === 1 ? temperature : null,
          machine02:
            machineId === 2 ? temperature : null,
        },
      ]);
    }

    /* -----------------------------------------------
       MACHINE STATUS
    ----------------------------------------------- */

    if (topic.includes("/status") && machineId) {
      updateMachine(machineId, {
        online: payload.status !== "offline",
        protocol: payload.protocol || "MQTT",
      });
    }

    /* -----------------------------------------------
       COMMUNICATION STATUS
    ----------------------------------------------- */

    if (topic.includes("/communication")) {
      setCommunication((previous) => ({
        ...previous,
        ...payload,
        mqtt: "ONLINE",
      }));
    }

    /* -----------------------------------------------
       FAILOVER
    ----------------------------------------------- */

    if (topic.includes("/failover")) {
      const active = Boolean(payload.active);

      setCommunication((previous) => ({
        ...previous,
        failover: active ? "ACTIVE" : "STANDBY",
        rs485: active ? "FAILED" : "HEALTHY",
        modbus: active ? "INACTIVE" : "ACTIVE",
      }));

      addEvent(
        active
          ? "MQTT failover activated"
          : "Primary Modbus communication restored",
        active ? "warning" : "success"
      );
    }

    /* -----------------------------------------------
       EVENTS
    ----------------------------------------------- */

    if (topic.includes("/event")) {
      addEvent(
        payload.message ||
          `Machine ${machineId ?? "system"} event received`,
        "info"
      );
    }
  };

  /* =======================================================
     MQTT CONNECTION
  ======================================================= */

  const connectMqtt = () => {
    if (mqttClientRef.current) {
      try {
        mqttClientRef.current.end(true);
      } catch {
        // Ignore cleanup errors.
      }
    }

    setMqttState("Connecting");

    addEvent("Connecting to MQTT broker...");

    const client = mqtt.connect(MQTT_URL, {
      clientId:
        "smart-dashboard-" +
        Math.random().toString(16).substring(2),

      clean: true,

      connectTimeout: 10000,

      reconnectPeriod: 3000,
    });

    mqttClientRef.current = client;

    /* CONNECTED */

    client.on("connect", () => {
      setMqttState("Online");

      setCommunication((previous) => ({
        ...previous,
        mqtt: "ONLINE",
      }));

      client.subscribe(
        MQTT_TOPICS,
        (error) => {
          if (error) {
            addEvent(
              `MQTT subscription failed: ${error.message}`,
              "error"
            );
            return;
          }

          addEvent(
            "MQTT connected and subscribed successfully",
            "success"
          );
        }
      );
    });

    /* RECONNECT */

    client.on("reconnect", () => {
      setMqttState("Reconnecting");

      addEvent(
        "MQTT reconnecting...",
        "warning"
      );
    });

    /* OFFLINE */

    client.on("offline", () => {
      setMqttState("Offline");

      setCommunication((previous) => ({
        ...previous,
        mqtt: "OFFLINE",
      }));
    });

    /* ERROR */

    client.on("error", (error) => {
      setMqttState("Error");

      addEvent(
        `MQTT error: ${error.message}`,
        "error"
      );
    });

    /* MESSAGE */

    client.on(
      "message",
      handleMqttMessage
    );
  };

  /* =======================================================
     INITIAL MQTT CONNECTION
  ======================================================= */

  useEffect(() => {
    connectMqtt();

    const offlineTimer = setInterval(() => {
      setMachines((previous) => {
        const now = Date.now();

        const updated = {
          ...previous,
        };

        [1, 2].forEach((machineId) => {
          const machine = updated[machineId];

          if (
            machine.lastSeen &&
            now - machine.lastSeen.getTime() > 15000
          ) {
            updated[machineId] = {
              ...machine,
              online: false,
              protocol: "OFFLINE",
            };
          }
        });

        return updated;
      });
    }, 3000);

    return () => {
      clearInterval(offlineTimer);

      if (mqttClientRef.current) {
        mqttClientRef.current.end(true);
      }
    };
  }, []);

  /* =======================================================
     FLASK API CHECK
  ======================================================= */

  useEffect(() => {
    fetch(API_URL)
      .then((response) => {
        if (!response.ok) {
          throw new Error("Backend unavailable");
        }

        return response.json();
      })
      .then(() => {
        setApiState("Online");
      })
      .catch(() => {
        setApiState("Offline");
      });
  }, []);

  /* =======================================================
     SYSTEM STATUS
  ======================================================= */

  const systemStatus = useMemo(() => {
    if (
      mqttState === "Error" ||
      mqttState === "Offline"
    ) {
      return "DEGRADED";
    }

    if (
      machines[1].online ||
      machines[2].online
    ) {
      return "OPERATIONAL";
    }

    return "STANDBY";
  }, [mqttState, machines]);

  /* =======================================================
     UI
  ======================================================= */

  return (
    <div className="app-shell">

      {/* FLOATING BACKGROUND ELEMENTS */}

      <div className="ambient ambient-one" />
      <div className="ambient ambient-two" />
      <div className="ambient ambient-three" />

      <div className="floating-particle particle-1" />
      <div className="floating-particle particle-2" />
      <div className="floating-particle particle-3" />
      <div className="floating-particle particle-4" />
      <div className="floating-particle particle-5" />

      {/* =================================================
          HEADER
      ================================================= */}

      <header className="topbar glass floating-card">

        <div className="brand">

          <div className="brand-icon">
            <Cpu size={23} />
          </div>

          <div>
            <div className="eyebrow">
              INDUSTRIAL M2M • LIVE PORTAL
            </div>

            <h1>
              Smart Machine Communication System
            </h1>
          </div>

        </div>

        <div
          className={`system-pill ${
            systemStatus.toLowerCase()
          }`}
        >
          <span className="status-dot" />

          {systemStatus}
        </div>

      </header>

      <main className="container">

        {/* =================================================
            HERO
        ================================================= */}

        <section className="hero glass floating-card">

          <div className="hero-content">

            <span className="eyebrow">
              RESILIENT MACHINE-TO-MACHINE NETWORK
            </span>

            <h2>
              Modbus RTU with automatic MQTT failover
            </h2>

            <p>
              Two ESP32 machines exchange industrial
              data over RS-485. MQTT provides the
              wireless backup path and feeds the live
              monitoring dashboard.
            </p>

          </div>

          <div className="flow">

            <FlowNode
              icon={<Cpu />}
              label="Machine 01"
            />

            <ArrowRight className="flow-arrow" />

            <FlowNode
              icon={<Radio />}
              label="RS-485 / Modbus"
            />

            <ArrowRight className="flow-arrow" />

            <FlowNode
              icon={<Cpu />}
              label="Machine 02"
            />

            <ArrowRight className="flow-arrow" />

            <FlowNode
              icon={<Gauge />}
              label="Dashboard"
            />

          </div>

        </section>

        {/* =================================================
            MACHINE CARDS
        ================================================= */}

        <section className="grid machines-grid">

          <MachineCard
            number={1}
            machine={machines[1]}
          />

          <MachineCard
            number={2}
            machine={machines[2]}
          />

        </section>

        {/* =================================================
            COMMUNICATION CARDS
        ================================================= */}

        <section className="grid stats-grid">

          <StatCard
            icon={<Radio />}
            title="RS-485"
            value={communication.rs485}
            tone={
              communication.rs485 === "HEALTHY" ||
              communication.rs485 === "ACTIVE"
                ? "good"
                : "neutral"
            }
          />

          <StatCard
            icon={<Activity />}
            title="Modbus RTU"
            value={communication.modbus}
            tone={
              communication.modbus === "ACTIVE"
                ? "good"
                : "neutral"
            }
          />

          <StatCard
            icon={<Wifi />}
            title="MQTT"
            value={mqttState.toUpperCase()}
            tone={
              mqttState === "Online"
                ? "good"
                : "neutral"
            }
          />

          <StatCard
            icon={<Server />}
            title="Flask API"
            value={apiState.toUpperCase()}
            tone={
              apiState === "Online"
                ? "good"
                : "neutral"
            }
          />

        </section>

        {/* =================================================
            CHART + HEALTH
        ================================================= */}

        <section className="grid lower-grid">

          <div className="glass panel floating-card">

            <div className="panel-head">

              <div>

                <span className="eyebrow">
                  LIVE TELEMETRY
                </span>

                <h3>
                  Temperature
                </h3>

              </div>

              <span className="small-badge">
                <Activity size={14} />

                {mqttPackets} packets
              </span>

            </div>

            <div className="chart-wrap">

              <ResponsiveContainer
                width="100%"
                height="100%"
              >

                <LineChart data={chartData}>

                  <CartesianGrid
                    strokeDasharray="3 3"
                    stroke="rgba(150,170,200,.12)"
                  />

                  <XAxis
                    dataKey="time"
                    minTickGap={30}
                    stroke="#71839c"
                  />

                  <YAxis
                    stroke="#71839c"
                  />

                  <Tooltip />

                  <Line
                    type="monotone"
                    dataKey="machine01"
                    name="Machine 01"
                    stroke="#6ea8ff"
                    strokeWidth={3}
                    dot={false}
                    connectNulls
                  />

                  <Line
                    type="monotone"
                    dataKey="machine02"
                    name="Machine 02"
                    stroke="#c084fc"
                    strokeWidth={3}
                    dot={false}
                    connectNulls
                  />

                </LineChart>

              </ResponsiveContainer>

            </div>

          </div>

          <div className="glass panel floating-card">

            <div className="panel-head">

              <div>

                <span className="eyebrow">
                  COMMUNICATION
                </span>

                <h3>
                  Network health
                </h3>

              </div>

              <Zap size={20} />

            </div>

            <HealthRow
              label="RS-485 bus"
              value={communication.rs485}
            />

            <HealthRow
              label="Modbus RTU"
              value={communication.modbus}
            />

            <HealthRow
              label="MQTT backup"
              value={mqttState.toUpperCase()}
            />

            <HealthRow
              label="Failover"
              value={communication.failover}
            />

            <HealthRow
              label="Flask API"
              value={apiState.toUpperCase()}
            />

          </div>

        </section>

        {/* =================================================
            EVENT STREAM
        ================================================= */}

        <section className="glass panel floating-card">

          <div className="panel-head">

            <div>

              <span className="eyebrow">
                EVENT STREAM
              </span>

              <h3>
                Recent system events
              </h3>

            </div>

            <Bell size={20} />

          </div>

          <div className="events">

            {events.length === 0 ? (

              <div className="empty">
                Waiting for machine events...
              </div>

            ) : (

              events.map((event) => (

                <div
                  className={`event ${event.type}`}
                  key={event.id}
                >

                  <Clock3 size={15} />

                  <span>
                    {event.time}
                  </span>

                  <strong>
                    {event.message}
                  </strong>

                </div>

              ))

            )}

          </div>

        </section>

        {/* =================================================
            ACTION
        ================================================= */}

        <div className="actions">

          <button
            className="primary-btn"
            onClick={connectMqtt}
          >
            <RefreshCw size={17} />

            Reconnect MQTT
          </button>

        </div>

      </main>
    </div>
  );
}

/* =========================================================
   FLOW NODE
========================================================= */

function FlowNode({ icon, label }) {
  return (
    <div className="flow-node floating-mini">

      <div className="flow-icon">
        {icon}
      </div>

      <span>
        {label}
      </span>

    </div>
  );
}

/* =========================================================
   MACHINE CARD
========================================================= */

function MachineCard({ number, machine }) {
  return (
    <div className="glass machine-card floating-card">

      <div className="machine-head">

        <div>

          <span className="eyebrow">
            MACHINE{" "}
            {String(number).padStart(2, "0")}
          </span>

          <h3>
            {number === 1
              ? "Modbus Master"
              : "Modbus Slave"}
          </h3>

        </div>

        <span
          className={`online-badge ${
            machine.online ? "online" : ""
          }`}
        >

          <span className="status-dot" />

          {machine.online
            ? "ONLINE"
            : "WAITING"}

        </span>

      </div>

      <div className="metric-row">

        <Metric
          icon={<Thermometer />}
          label="Temperature"
          value={
            machine.temperature == null
              ? "--"
              : `${machine.temperature.toFixed(1)} °C`
          }
        />

        <Metric
          icon={<Droplets />}
          label="Humidity"
          value={
            machine.humidity == null
              ? "--"
              : `${machine.humidity.toFixed(1)} %`
          }
        />

      </div>

      <div className="device-state">

        <StateItem
          label="Relay"
          value={machine.relay}
        />

        <StateItem
          label="LED"
          value={machine.led}
        />

        <StateItem
          label="Buzzer"
          value={machine.buzzer}
        />

        <StateItem
          label="Button"
          value={machine.button}
        />

      </div>

      <div className="protocol-line">

        <Radio size={16} />

        Protocol:

        <strong>
          {machine.protocol}
        </strong>

      </div>

    </div>
  );
}

/* =========================================================
   METRIC
========================================================= */

function Metric({ icon, label, value }) {
  return (
    <div className="metric">

      <div className="metric-icon">
        {icon}
      </div>

      <div>

        <span>
          {label}
        </span>

        <strong>
          {value}
        </strong>

      </div>

    </div>
  );
}

/* =========================================================
   STATE ITEM
========================================================= */

function StateItem({ label, value }) {
  return (
    <div className="state-item">

      <span>
        {label}
      </span>

      {value ? (
        <CircleCheck
          className="good-icon"
          size={17}
        />
      ) : (
        <CircleX
          className="muted-icon"
          size={17}
        />
      )}

    </div>
  );
}

/* =========================================================
   STAT CARD
========================================================= */

function StatCard({
  icon,
  title,
  value,
  tone,
}) {
  return (
    <div className="glass stat-card floating-card">

      <div className="stat-icon">
        {icon}
      </div>

      <div>

        <span>
          {title}
        </span>

        <strong className={tone}>
          {value}
        </strong>

      </div>

    </div>
  );
}

/* =========================================================
   HEALTH ROW
========================================================= */

function HealthRow({ label, value }) {
  const good = [
    "ONLINE",
    "HEALTHY",
    "ACTIVE",
    "STANDBY",
  ].includes(value);

  return (
    <div className="health-row">

      <span>
        {label}
      </span>

      <strong
        className={
          good ? "good" : "warning"
        }
      >
        {value}
      </strong>

    </div>
  );
}

/* =========================================================
   START REACT
========================================================= */

createRoot(
  document.getElementById("root")
).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>
);