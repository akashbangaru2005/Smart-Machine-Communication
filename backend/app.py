
import json
import threading
import time

from flask import Flask, jsonify
from flask_cors import CORS
import paho.mqtt.client as mqtt

from system_state import (
    get_state,
    update_machine,
    update_communication,
    set_mqtt_status,
)


# ============================================================
# FLASK
# ============================================================

app = Flask(__name__)
CORS(app)


# ============================================================
# MQTT CONFIGURATION
# ============================================================

MQTT_BROKER = "test.mosquitto.org"
MQTT_PORT = 1883

MQTT_TOPICS = [
    "smart-machine/machine01/telemetry",
    "smart-machine/machine01/status",
    "smart-machine/machine01/event",

    "smart-machine/machine02/telemetry",
    "smart-machine/machine02/status",
    "smart-machine/machine02/event",

    "smart-machine/system/communication",
    "smart-machine/system/failover",
]


mqtt_client = None
mqtt_connected = False


# ============================================================
# SAFE JSON PARSER
# ============================================================

def parse_payload(payload):
    """
    Convert MQTT payload into a Python dictionary.

    Expected ESP32 payload:
    {
        "temperature": 31.1,
        "humidity": 41.0,
        "motor": "OFF",
        "led": "OFF"
    }
    """

    try:
        text = payload.decode("utf-8").strip()

        if not text:
            return {}

        data = json.loads(text)

        if isinstance(data, dict):
            return data

        return {}

    except Exception as error:
        print("MQTT JSON parse error:", error)
        return {}


# ============================================================
# MQTT CALLBACKS
# ============================================================

def on_connect(client, userdata, flags, reason_code, properties=None):

    global mqtt_connected

    if reason_code == 0:

        mqtt_connected = True

        print("=" * 60)
        print("MQTT CONNECTED")
        print("Broker:", MQTT_BROKER)
        print("=" * 60)

        set_mqtt_status("online")

        for topic in MQTT_TOPICS:

            result = client.subscribe(topic)

            if result[0] == mqtt.MQTT_ERR_SUCCESS:
                print("Subscribed:", topic)
            else:
                print("Subscription failed:", topic)

    else:

        mqtt_connected = False

        print("MQTT connection failed:", reason_code)

        set_mqtt_status("offline")


def on_disconnect(client, userdata, disconnect_flags, reason_code, properties=None):

    global mqtt_connected

    mqtt_connected = False

    print("MQTT disconnected:", reason_code)

    set_mqtt_status("offline")


def on_message(client, userdata, message):

    topic = message.topic
    payload = message.payload

    print()
    print("MQTT MESSAGE")
    print("Topic:", topic)
    print("Payload:", payload.decode("utf-8", errors="ignore"))

    data = parse_payload(payload)

    if not data:
        return


    # --------------------------------------------------------
    # MACHINE 01
    # --------------------------------------------------------

    if topic.startswith("smart-machine/machine01/"):

        update_machine("machine01", data)

        return


    # --------------------------------------------------------
    # MACHINE 02
    # --------------------------------------------------------

    if topic.startswith("smart-machine/machine02/"):

        update_machine("machine02", data)

        return


    # --------------------------------------------------------
    # COMMUNICATION STATUS
    # --------------------------------------------------------

    if topic == "smart-machine/system/communication":

        update_communication(data)

        return


    # --------------------------------------------------------
    # FAILOVER STATUS
    # --------------------------------------------------------

    if topic == "smart-machine/system/failover":

        if "failover" in data:
            update_communication({
                "failover": data["failover"]
            })

        return


# ============================================================
# MQTT STARTUP THREAD
# ============================================================

def mqtt_worker():

    global mqtt_client

    while True:

        try:

            print("Connecting to MQTT broker...")

            client = mqtt.Client(
                mqtt.CallbackAPIVersion.VERSION2,
                client_id="smart-machine-flask-backend"
            )

            client.on_connect = on_connect
            client.on_disconnect = on_disconnect
            client.on_message = on_message

            mqtt_client = client

            client.connect(
                MQTT_BROKER,
                MQTT_PORT,
                keepalive=60
            )

            client.loop_forever()

        except Exception as error:

            print("MQTT ERROR:", error)

            set_mqtt_status("offline")

            time.sleep(10)


# Start MQTT in background
mqtt_thread = threading.Thread(
    target=mqtt_worker,
    daemon=True
)

mqtt_thread.start()


# ============================================================
# FLASK ROUTES
# ============================================================

@app.route("/")
def home():

    return jsonify({
        "project": "Smart Machine Communication System",
        "status": "Backend Online",
        "mqtt": "online" if mqtt_connected else "offline"
    })


@app.route("/api/status")
def status():

    state = get_state()

    return jsonify({

        "system": state["system"],

        "machine01": state["machine01"],
        "machine02": state["machine02"],

        "rs485": state["communication"]["rs485"],
        "modbus": state["communication"]["modbus"],
        "mqtt": state["communication"]["mqtt"],

        "failover": state["communication"]["failover"],

    })


@app.route("/api/health")
def health():

    return jsonify({
        "status": "healthy",
        "mqtt_connected": mqtt_connected
    })


# ============================================================
# LOCAL DEVELOPMENT
# ============================================================

if __name__ == "__main__":

    print("=" * 60)
    print("SMART MACHINE COMMUNICATION SYSTEM")
    print("Flask Backend Starting...")
    print("MQTT Broker:", MQTT_BROKER)
    print("=" * 60)

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False
    )
