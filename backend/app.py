from flask import Flask, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)


@app.route("/")
def home():
    return jsonify({
        "project": "Smart Machine Communication System",
        "status": "Backend Online"
    })


@app.route("/api/status")
def status():
    return jsonify({
        "system": "online",
        "machine01": "waiting",
        "machine02": "waiting",
        "rs485": "not_connected",
        "modbus": "not_connected",
        "mqtt": "standby"
    })


if __name__ == "__main__":
    print("=" * 50)
    print("SMART MACHINE COMMUNICATION SYSTEM")
    print("Flask Backend Starting...")
    print("=" * 50)
    app.run(host="0.0.0.0", port=5000, debug=True)