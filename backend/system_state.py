
"""
Live system state for Smart Machine Communication System.
"""

from threading import Lock
from datetime import datetime, timezone


_state_lock = Lock()

_state = {
    "system": "online",

    "machine01": {
        "status": "waiting",
        "temperature": None,
        "humidity": None,
        "motor": "OFF",
        "led": "OFF",
        "last_update": None,
    },

    "machine02": {
        "status": "waiting",
        "temperature": None,
        "humidity": None,
        "motor": "OFF",
        "led": "OFF",
        "last_update": None,
    },

    "communication": {
        "mqtt": "standby",
        "rs485": "not_connected",
        "modbus": "not_connected",
        "failover": "normal",
    },
}


def _now():
    return datetime.now(timezone.utc).isoformat()


def update_machine(machine, data):
    if machine not in ("machine01", "machine02"):
        return

    with _state_lock:
        target = _state[machine]

        if "status" in data:
            target["status"] = str(data["status"])

        if "temperature" in data:
            target["temperature"] = data["temperature"]

        if "humidity" in data:
            target["humidity"] = data["humidity"]

        if "motor" in data:
            target["motor"] = normalize_switch(data["motor"])

        if "led" in data:
            target["led"] = normalize_switch(data["led"])

        target["last_update"] = _now()


def update_communication(data):
    with _state_lock:
        if "mqtt" in data:
            _state["communication"]["mqtt"] = str(data["mqtt"])

        if "rs485" in data:
            _state["communication"]["rs485"] = str(data["rs485"])

        if "modbus" in data:
            _state["communication"]["modbus"] = str(data["modbus"])

        if "failover" in data:
            _state["communication"]["failover"] = str(data["failover"])


def set_mqtt_status(status):
    with _state_lock:
        _state["communication"]["mqtt"] = status


def normalize_switch(value):
    if isinstance(value, bool):
        return "ON" if value else "OFF"

    if isinstance(value, (int, float)):
        return "ON" if value else "OFF"

    text = str(value).strip().upper()

    if text in ("1", "TRUE", "ON", "HIGH", "ACTIVE"):
        return "ON"

    return "OFF"


def get_state():
    with _state_lock:
        return {
            "system": _state["system"],

            "machine01": dict(_state["machine01"]),
            "machine02": dict(_state["machine02"]),

            "communication": dict(_state["communication"]),
        }
