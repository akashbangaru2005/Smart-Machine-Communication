#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// MACHINE 01 - SMART MACHINE COMMUNICATION SYSTEM
// RS-485 MASTER
// ============================================================

// -------------------- Wi-Fi --------------------
const char* WIFI_SSID = "Trisha's F15";
const char* WIFI_PASSWORD = "7095197758";


// -------------------- MQTT --------------------
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

const char* TOPIC_TELEMETRY =
  "smart-machine/machine01/telemetry";

const char* TOPIC_STATUS =
  "smart-machine/machine01/status";

const char* TOPIC_EVENT =
  "smart-machine/machine01/event";

const char* TOPIC_CONTROL =
  "smart-machine/machine01/control";

const char* TOPIC_COMMUNICATION =
  "smart-machine/system/communication";

const char* TOPIC_FAILOVER =
  "smart-machine/system/failover";

// -------------------- DHT11 --------------------
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// -------------------- Hardware --------------------
#define LED_PIN     2
#define BUTTON_PIN  18
#define BUZZER_PIN  19
#define RELAY_PIN   23

// -------------------- RS-485 --------------------
#define RS485_RX    16
#define RS485_TX    17
#define RS485_DE_RE 21

#define MACHINE02_ID 2

HardwareSerial RS485Serial(2);

// -------------------- Network --------------------
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// ============================================================
// SYSTEM STATE
// ============================================================

bool motorState = false;
bool ledState = false;
bool buzzerState = false;

bool autoMode = true;
bool machine01ManualStop = false;

bool rs485OK = false;
bool mqttOK = false;

bool machine02Motor = false;
bool machine02Online = false;

float temperature = 0.0;
float humidity = 0.0;

float machine02Temperature = 0.0;
float machine02Humidity = 0.0;

String machine02Mode = "UNKNOWN";
String machine02Fault = "NONE";

// ============================================================
// TIMERS
// ============================================================

unsigned long lastSensorRead = 0;
unsigned long lastTelemetry = 0;
unsigned long lastModbusPoll = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastButtonTime = 0;

unsigned long autoCycleStart = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long TELEMETRY_INTERVAL = 1000;
const unsigned long MODBUS_INTERVAL = 500;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;
const unsigned long BUTTON_DEBOUNCE = 80;

const unsigned long AUTO_INTERVAL = 50000;

// ============================================================
// BUTTON
// ============================================================

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

// ============================================================
// MODBUS REGISTERS
// ============================================================
//
// Machine 02:
//
// Register 0 = Temperature x10
// Register 1 = Humidity x10
// Register 2 = Motor
// Register 3 = Mode
// Register 4 = Fault
// Register 5 = Heartbeat
// Register 6 = Manual Stop
//
// Mode:
// 0 = STANDBY
// 1 = ACTIVE
// 2 = SAFETY STOP
//
// Fault:
// 0 = NONE
// 1 = COMMUNICATION FAULT
// 2 = MOTOR FAULT
// ============================================================

// ============================================================
// CRC16 MODBUS
// ============================================================

uint16_t modbusCRC(uint8_t* data, uint8_t length)
{
  uint16_t crc = 0xFFFF;

  for (uint8_t pos = 0; pos < length; pos++)
  {
    crc ^= data[pos];

    for (uint8_t i = 0; i < 8; i++)
    {
      if (crc & 0x0001)
      {
        crc >>= 1;
        crc ^= 0xA001;
      }
      else
      {
        crc >>= 1;
      }
    }
  }

  return crc;
}

// ============================================================
// RS-485 MODE
// ============================================================

void rs485Transmit()
{
  digitalWrite(RS485_DE_RE, HIGH);
}

void rs485Receive()
{
  digitalWrite(RS485_DE_RE, LOW);
}

// ============================================================
// MOTOR CONTROL
// ============================================================

void setMotor(bool state)
{
  motorState = state;

  // ACTIVE LOW RELAY
  // LOW  = MOTOR ON
  // HIGH = MOTOR OFF

  digitalWrite(RELAY_PIN, state ? LOW : HIGH);

  Serial.print("Machine 01 Motor: ");
  Serial.println(state ? "ON" : "OFF");
}

// ============================================================
// LED
// ============================================================

void setLED(bool state)
{
  ledState = state;
  digitalWrite(LED_PIN, state ? HIGH : LOW);
}

// ============================================================
// BUZZER
// ============================================================

void setBuzzer(bool state)
{
  buzzerState = state;
  digitalWrite(BUZZER_PIN, state ? HIGH : LOW);
}

void playSwitchMelody()
{
  const int notes[] = {880, 988, 1175, 1319, 1568};
  const int durations[] = {100, 100, 100, 100, 180};

  for (int i = 0; i < 5; i++)
  {
    tone(BUZZER_PIN, notes[i], durations[i]);
    delay(durations[i] + 20);
  }

  noTone(BUZZER_PIN);
  buzzerState = false;
  digitalWrite(BUZZER_PIN, LOW);
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
    return;

  if (millis() - lastWiFiAttempt < WIFI_RETRY_INTERVAL)
    return;

  lastWiFiAttempt = millis();

  Serial.println("Connecting to Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - start < 8000
  )
  {
    delay(250);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Wi-Fi connected.");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("Wi-Fi connection failed.");
  }
}

// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(
  char* topic,
  byte* payload,
  unsigned int length
)
{
  String message;

  for (unsigned int i = 0; i < length; i++)
  {
    message += (char)payload[i];
  }

  message.trim();

  Serial.print("MQTT COMMAND: ");
  Serial.println(message);

  if (String(topic) != TOPIC_CONTROL)
    return;

  if (message == "AUTO_START")
  {
    autoMode = true;
    autoCycleStart = millis();
  machine01ManualStop = false;

    setMotor(true);

    machine02Fault = "NONE";

    mqttClient.publish(
      TOPIC_EVENT,
      "AUTO MODE STARTED"
    );
  }

  else if (message == "AUTO_STOP")
  {
    autoMode = false;

    setMotor(false);

    mqttClient.publish(
      TOPIC_EVENT,
      "AUTO MODE STOPPED"
    );
  }

  else if (message == "MOTOR_ON")
  {
    autoMode = false;
    setMotor(true);

    mqttClient.publish(
      TOPIC_EVENT,
      "MACHINE 01 MOTOR ON"
    );
  }

  else if (message == "MOTOR_OFF")
  {
    autoMode = false;
    setMotor(false);

    mqttClient.publish(
      TOPIC_EVENT,
      "MACHINE 01 MOTOR OFF"
    );
  }

  else if (message == "LED_ON")
  {
    setLED(true);
  }

  else if (message == "LED_OFF")
  {
    setLED(false);
  }

  else if (message == "BUZZER_ON")
  {
    setBuzzer(true);
  }

  else if (message == "BUZZER_OFF")
  {
    setBuzzer(false);
  }

  else if (message == "SAFETY_RESET")
  {
    machine02Fault = "NONE";
    rs485OK = false;

    mqttClient.publish(
      TOPIC_EVENT,
      "SAFETY RESET REQUESTED"
    );
  }
}

// ============================================================
// MQTT CONNECT
// ============================================================

void connectMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (mqttClient.connected())
    return;

  if (millis() - lastMQTTAttempt < MQTT_RETRY_INTERVAL)
    return;

  lastMQTTAttempt = millis();

  Serial.println("Connecting to MQTT...");

  String clientID =
    "SmartMachine01-" +
    String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientID.c_str()))
  {
    mqttOK = true;

    Serial.println("MQTT connected.");

    mqttClient.subscribe(TOPIC_CONTROL);

    mqttClient.publish(
      TOPIC_STATUS,
      "ONLINE"
    );

    mqttClient.publish(
      TOPIC_COMMUNICATION,
      "MACHINE01_MQTT_ONLINE"
    );
  }
  else
  {
    mqttOK = false;

    Serial.print("MQTT failed: ");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// DHT11
// ============================================================

void readDHT()
{
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t))
  {
    Serial.println("DHT11 read failed.");
    return;
  }

  temperature = t;
  humidity = h;
}

// ============================================================
// MODBUS SEND
// ============================================================

void sendModbusFrame(
  uint8_t* frame,
  uint8_t length
)
{
  rs485Transmit();

  delayMicroseconds(100);

  RS485Serial.write(frame, length);
  RS485Serial.flush();

  delayMicroseconds(100);

  rs485Receive();
}

// ============================================================
// MODBUS WRITE SINGLE REGISTER
// ============================================================

bool modbusWriteRegister(
  uint8_t slave,
  uint16_t reg,
  uint16_t value
)
{
  uint8_t request[8];

  request[0] = slave;
  request[1] = 0x06;

  request[2] = highByte(reg);
  request[3] = lowByte(reg);

  request[4] = highByte(value);
  request[5] = lowByte(value);

  uint16_t crc = modbusCRC(request, 6);

  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  while (RS485Serial.available())
    RS485Serial.read();

  sendModbusFrame(request, 8);

  unsigned long start = millis();

  uint8_t response[8];
  uint8_t index = 0;

  while (
    millis() - start < 200
  )
  {
    while (RS485Serial.available())
    {
      response[index++] = RS485Serial.read();

      if (index >= 8)
      {
        uint16_t receivedCRC =
          response[6] |
          ((uint16_t)response[7] << 8);

        uint16_t calculatedCRC =
          modbusCRC(response, 6);

        if (
          response[0] == slave &&
          response[1] == 0x06 &&
          receivedCRC == calculatedCRC
        )
        {
          return true;
        }

        return false;
      }
    }
  }

  return false;
}

// ============================================================
// MODBUS READ REGISTERS
// ============================================================

bool modbusReadRegisters(
  uint8_t slave,
  uint16_t startRegister,
  uint16_t quantity
)
{
  uint8_t request[8];

  request[0] = slave;
  request[1] = 0x03;
  request[2] = highByte(startRegister);
  request[3] = lowByte(startRegister);
  request[4] = highByte(quantity);
  request[5] = lowByte(quantity);

  uint16_t crc = modbusCRC(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  while (RS485Serial.available()) RS485Serial.read();
  sendModbusFrame(request, 8);

  unsigned long start = millis();
  uint8_t response[32];
  uint8_t index = 0;
  const uint8_t expectedLength = 3 + (quantity * 2) + 2;

  while (millis() - start < 300)
  {
    while (RS485Serial.available())
    {
      if (index < sizeof(response))
        response[index++] = RS485Serial.read();
      else
        RS485Serial.read();

      if (index >= expectedLength)
      {
        uint16_t receivedCRC =
          response[index - 2] | ((uint16_t)response[index - 1] << 8);
        uint16_t calculatedCRC = modbusCRC(response, index - 2);

        if (response[0] != slave || response[1] != 0x03 ||
            response[2] != quantity * 2 || receivedCRC != calculatedCRC)
          return false;

        uint16_t reg0 = ((uint16_t)response[3] << 8) | response[4];
        uint16_t reg1 = ((uint16_t)response[5] << 8) | response[6];
        uint16_t reg2 = ((uint16_t)response[7] << 8) | response[8];
        uint16_t reg3 = ((uint16_t)response[9] << 8) | response[10];
        uint16_t reg4 = ((uint16_t)response[11] << 8) | response[12];
        uint16_t reg5 = ((uint16_t)response[13] << 8) | response[14];
        uint16_t reg6 = ((uint16_t)response[15] << 8) | response[16];

        machine02Temperature = reg0 / 10.0;
        machine02Humidity = reg1 / 10.0;
        machine02Motor = reg2 == 1;

        if (reg3 == 1) machine02Mode = "ACTIVE";
        else if (reg3 == 2) machine02Mode = "SAFETY STOP";
        else if (reg3 == 3) machine02Mode = "MANUAL STOP";
        else machine02Mode = "STANDBY";

        if (reg4 == 1) machine02Fault = "COMMUNICATION FAULT";
        else if (reg4 == 2) machine02Fault = "MOTOR FAULT";
        else machine02Fault = "NONE";

        machine02Online = true;
        return true;
      }
    }
    delay(1);
  }

  return false;
}

// ============================================================
// SEND MACHINE 02 COMMAND
// ============================================================

void commandMachine02(uint16_t command)
{
  bool result =
    modbusWriteRegister(
      MACHINE02_ID,
      10,
      command
    );

  if (!result)
  {
    Serial.println("Machine 02 command failed.");
    rs485OK = false;
    machine02Online = false;
  }
}

// ============================================================
// POLL MACHINE 02
// ============================================================

bool pollMachine02()
{
  bool result =
    modbusReadRegisters(
      MACHINE02_ID,
      0,
      7
    );

  if (result)
  {
    rs485OK = true;
    machine02Online = true;
  }
  else
  {
    rs485OK = false;
    machine02Online = false;
  }

  return result;
}

// ============================================================
// AUTOMATIC 10-SECOND COORDINATION
// ============================================================

void handleAutomaticMode()
{
  if (!autoMode) return;

  unsigned long elapsed = millis() - autoCycleStart;
  bool active = ((elapsed / AUTO_INTERVAL) % 2) == 0;
  static bool lastActive = true;

  if (active != lastActive)
  {
    lastActive = active;
    playSwitchMelody();

    Serial.println("==================================");
    Serial.print("MACHINE 01 -> ");
    Serial.println(active ? "ACTIVE" : "STANDBY");
    Serial.print("MACHINE 02 -> ");
    Serial.println(active ? "STANDBY" : "ACTIVE");
    Serial.println("==================================");
  }

  if (machine01ManualStop)
    setMotor(false);
  else
    setMotor(active);

  commandMachine02(active ? 0 : 1);
}

// ============================================================
// BUTTON
// ============================================================
//
// Machine 01 button:
// AUTO ON/OFF
//
// Press once -> automatic system ON
// Press again -> automatic system OFF
// ============================================================

void handleButton()
{
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != lastButtonReading)
    lastButtonTime = millis();

  if (millis() - lastButtonTime > BUTTON_DEBOUNCE &&
      reading != stableButtonState)
  {
    stableButtonState = reading;

    if (stableButtonState == LOW)
    {
      machine01ManualStop = !machine01ManualStop;

      if (machine01ManualStop)
      {
        setMotor(false);
        if (mqttClient.connected())
          mqttClient.publish(TOPIC_EVENT, "MACHINE 01 MANUAL STOP");
        Serial.println("MACHINE 01 MANUAL STOP");
      }
      else
      {
        if (mqttClient.connected())
          mqttClient.publish(TOPIC_EVENT, "MACHINE 01 MANUAL STOP RELEASED");
        Serial.println("MACHINE 01 MANUAL STOP RELEASED");
      }
    }
  }

  lastButtonReading = reading;
}

// ============================================================
// TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
    return;

  char payload[700];

  snprintf(
    payload,
    sizeof(payload),

    "{"
    "\"machine\":1,"
    "\"temperature\":%.1f,"
    "\"humidity\":%.1f,"
    "\"motor\":%d,"
    "\"led\":%d,"
    "\"buzzer\":%d,"
    "\"auto_mode\":%d,"
    "\"mode\":\"%s\","
    "\"rs485\":%d,"
    "\"mqtt\":%d,"
    "\"machine02_online\":%d,"
    "\"machine02_temperature\":%.1f,"
    "\"machine02_humidity\":%.1f,"
    "\"machine02_motor\":%d,"
    "\"machine02_mode\":\"%s\","
    "\"machine02_fault\":\"%s\""
    "}",

    temperature,
    humidity,

    motorState ? 1 : 0,
    ledState ? 1 : 0,
    buzzerState ? 1 : 0,

    autoMode ? 1 : 0,

    motorState
      ? "ACTIVE"
      : "STANDBY",

    rs485OK ? 1 : 0,
    mqttClient.connected() ? 1 : 0,

    machine02Online ? 1 : 0,

    machine02Temperature,
    machine02Humidity,

    machine02Motor ? 1 : 0,

    machine02Mode.c_str(),
    machine02Fault.c_str(),
    machine01ManualStop ? 1 : 0,
    AUTO_INTERVAL
  );

  mqttClient.publish(
    TOPIC_TELEMETRY,
    payload
  );
}

// ============================================================
// SAFETY
// ============================================================

void handleSafety()
{
  if (!machine02Online)
  {
    if (motorState)
    {
      setMotor(false);
      commandMachine02(2);

      Serial.println(
        "SAFETY STOP: MACHINE 02 LOST"
      );

      if (mqttClient.connected())
      {
        mqttClient.publish(
          TOPIC_FAILOVER,
          "MACHINE02_FAILURE_SAFETY_STOP"
        );

        mqttClient.publish(
          TOPIC_EVENT,
          "MACHINE 02 LOST - MACHINE 01 STOPPED"
        );
      }
    }
  }
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "========================================"
  );
  Serial.println(
    " SMART MACHINE COMMUNICATION SYSTEM"
  );
  Serial.println(
    " MACHINE 01 - MASTER"
  );
  Serial.println(
    "========================================"
  );

  pinMode(
    BUTTON_PIN,
    INPUT_PULLUP
  );

  pinMode(
    LED_PIN,
    OUTPUT
  );

  pinMode(
    BUZZER_PIN,
    OUTPUT
  );

  pinMode(
    RELAY_PIN,
    OUTPUT
  );

  pinMode(
    RS485_DE_RE,
    OUTPUT
  );

  setMotor(false);
  setLED(false);
  setBuzzer(false);

  rs485Receive();

  RS485Serial.begin(
    9600,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  dht.begin();

  WiFi.mode(WIFI_STA);

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  mqttClient.setCallback(
    mqttCallback
  );

  autoCycleStart = millis();
  machine01ManualStop = false;

  Serial.println(
    "Machine 01 ready."
  );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  connectWiFi();
  connectMQTT();

  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  handleButton();

  unsigned long now = millis();

  if (
    now - lastSensorRead >=
    SENSOR_INTERVAL
  )
  {
    lastSensorRead = now;
    readDHT();
  }

  if (
    now - lastModbusPoll >=
    MODBUS_INTERVAL
  )
  {
    lastModbusPoll = now;

    handleAutomaticMode();

    pollMachine02();

    handleSafety();
  }

  if (
    now - lastTelemetry >=
    TELEMETRY_INTERVAL
  )
  {
    lastTelemetry = now;

    publishTelemetry();
  }
}