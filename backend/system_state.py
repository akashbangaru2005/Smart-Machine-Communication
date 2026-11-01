"""
Live system state for
Smart Machine Communication System.
"""

from threading import Lock
from datetime import datetime, timezone


_state_lock = Lock()


# ============================================================
# INITIAL STATE
# ============================================================

_state = {

    "system": "online",

    "machine01": {

        "online": False,

        "status": "waiting",

        "temperature": None,

        "humidity": None,

        "motor": "OFF",

        "mode": "STANDBY",

        "led": "OFF",

        "buzzer": "OFF",

        "fault": "NONE",

        "safety_shutdown": False,

        "auto_mode": True,

        "last_update": None,
    },

    "machine02": {

        "online": False,

        "status": "waiting",

        "temperature": None,

        "humidity": None,

        "motor": "OFF",

        "mode": "STANDBY",

        "led": "OFF",

        "buzzer": "OFF",

        "fault": "NONE",

        "safety_shutdown": False,

        "auto_mode": True,

        "last_update": None,
    },

    "communication": {

        "mqtt": "standby",

        "rs485": "not_connected",

        "modbus": "not_connected",

        "failover": "normal",
    },

    "events": [],
}


# ============================================================
# TIME
# ============================================================

def _now():

    return datetime.now(
        timezone.utc
    ).isoformat()


# ============================================================
# NORMALIZE MOTOR
# ============================================================

def normalize_switch(value):

    if isinstance(
        value,
        bool
    ):

        return (
            "ON"
            if value
            else "OFF"
        )

    if isinstance(
        value,
        (int, float)
    ):

        return (
            "ON"
            if value != 0
            else "OFF"
        )

    text = str(
        value
    ).strip().upper()

    if text in (
        "1",
        "TRUE",
        "ON",
        "HIGH",
        "ACTIVE",
        "RUNNING",
    ):

        return "ON"

    return "OFF"


# ============================================================
# MODE
# ============================================================

def normalize_mode(value):

    if value is None:
        return "STANDBY"

    text = str(
        value
    ).strip().upper()

    if text in (
        "ACTIVE",
        "RUNNING",
        "HIGH",
    ):

        return "ACTIVE"

    if text in (
        "SAFETY STOP",
        "SAFETY_STOP",
    ):

        return "SAFETY STOP"

    return "STANDBY"


# ============================================================
# UPDATE MACHINE
# ============================================================

def update_machine(
    machine,
    data
):

    if machine not in (
        "machine01",
        "machine02"
    ):

        return

    with _state_lock:

        target = _state[
            machine
        ]

        target["online"] = True

        if "status" in data:

            target["status"] = str(
                data["status"]
            )

        if "temperature" in data:

            target["temperature"] = (
                data["temperature"]
            )

        if "humidity" in data:

            target["humidity"] = (
                data["humidity"]
            )

        if "motor" in data:

            target["motor"] = (
                normalize_switch(
                    data["motor"]
                )
            )

        if "mode" in data:

            target["mode"] = (
                normalize_mode(
                    data["mode"]
                )
            )

        if "led" in data:

            target["led"] = (
                normalize_switch(
                    data["led"]
                )
            )

        if "buzzer" in data:

            target["buzzer"] = (
                normalize_switch(
                    data["buzzer"]
                )
            )

        if "fault" in data:

            fault = str(
                data["fault"]
            ).upper()

            target["fault"] = fault

            target["safety_shutdown"] = (
                fault != "NONE"
            )

        if "auto_mode" in data:

            target["auto_mode"] = bool(
                data["auto_mode"]
            )

        target["last_update"] = _now()

        # ----------------------------------------------------
        # Status calculation
        # ----------------------------------------------------

        if target["fault"] != "NONE":

            target["status"] = "fault"

        elif target["motor"] == "ON":

            target["status"] = "running"

        else:

            target["status"] = "standby"


# ============================================================
# COMMUNICATION
# ============================================================

def update_communication(data):

    with _state_lock:

        if "mqtt" in data:

            _state[
                "communication"
            ]["mqtt"] = str(
                data["mqtt"]
            )

        if "rs485" in data:

            value = data["rs485"]

            if isinstance(
                value,
                bool
            ):

                value = (
                    "connected"
                    if value
                    else "not_connected"
                )

            elif isinstance(
                value,
                (int, float)
            ):

                value = (
                    "connected"
                    if value
                    else "not_connected"
                )

            _state[
                "communication"
            ]["rs485"] = str(
                value
            )

        if "modbus" in data:

            _state[
                "communication"
            ]["modbus"] = str(
                data["modbus"]
            )

        if "failover" in data:

            _state[
                "communication"
            ]["failover"] = str(
                data["failover"]
            )


# ============================================================
# MQTT STATUS
# ============================================================

def set_mqtt_status(status):

    with _state_lock:

        _state[
            "communication"
        ]["mqtt"] = status


# ============================================================
# EVENTS
# ============================================================

def register_event(
    machine,
    message
):

    with _state_lock:

        event = {

            "machine":
                machine,

            "message":
                str(message),

            "time":
                _now(),
        }

        _state[
            "events"
        ].insert(
            0,
            event
        )

        _state[
            "events"
        ] = _state[
            "events"
        ][:50]


# ============================================================
# CONTROL STATE
# ============================================================

def set_control_state(
    machine,
    command
):

    if machine not in (
        "machine01",
        "machine02"
    ):

        return

    with _state_lock:

        target = _state[
            machine
        ]

        if command == "MOTOR_ON":

            target["motor"] = "ON"

            target["mode"] = "ACTIVE"

            target["fault"] = "NONE"

            target["safety_shutdown"] = False

        elif command == "MOTOR_OFF":

            target["motor"] = "OFF"

            target["mode"] = "STANDBY"

        elif command == "AUTO_START":

            target["auto_mode"] = True

        elif command == "AUTO_STOP":

            target["auto_mode"] = False

            target["motor"] = "OFF"

        elif command == "SAFETY_RESET":

            target["fault"] = "NONE"

            target["safety_shutdown"] = False

            target["mode"] = "STANDBY"

        target["last_update"] = _now()


# ============================================================
# GET STATE
# ============================================================

def get_state():

    with _state_lock:

        return {

            "system":
                _state["system"],

            "machine01":
                dict(
                    _state["machine01"]
                ),

            "machine02":
                dict(
                    _state["machine02"]
                ),

            "communication":
                dict(
                    _state["communication"]
                ),

            "events":
                list(
                    _state["events"]
                ),
        }