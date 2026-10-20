# Smart Machine Communication System

A resilient Industrial Machine-to-Machine (M2M) communication system using ESP32, RS-485 Modbus RTU, MQTT, Flask, and React.

The system connects two industrial machine nodes and supports primary communication through RS-485/Modbus RTU with MQTT as the backup communication path.

---

## 🚀 Project Overview

The Smart Machine Communication System is designed to demonstrate reliable communication between two machines in an industrial environment.

The system consists of two ESP32-based machine nodes:

- Machine 01
- Machine 02

Each machine monitors sensors and controls actuators while exchanging data with the other machine.

The communication architecture uses:

- **RS-485 + Modbus RTU** as the primary communication protocol
- **Wi-Fi + MQTT** as the backup communication path
- **Flask** as the backend API
- **React + Vite** as the monitoring dashboard

---

## 🏗️ System Architecture

```text
                 ┌──────────────────────┐
                 │      Machine 01      │
                 │       ESP32          │
                 │                      │
                 │ DHT11                │
                 │ Motor                │
                 │ LED                  │
                 │ Buzzer               │
                 │ Push Button          │
                 └──────────┬───────────┘
                            │
                     RS-485 / Modbus RTU
                            │
                 ┌──────────▼───────────┐
                 │      Machine 02      │
                 │       ESP32          │
                 │                      │
                 │ DHT11                │
                 │ Motor                │
                 │ LED                  │
                 └──────────┬───────────┘
                            │
                         Wi-Fi
                            │
                            ▼
                    ┌───────────────┐
                    │  MQTT Broker  │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ Flask Backend │
                    └───────┬───────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ React/Vite UI │
                    │   Dashboard   │
                    └───────────────┘