#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// MACHINE 01 - SMART MACHINE COMMUNICATION SYSTEM
// ============================================================

// -------------------- Wi-Fi --------------------
const char* WIFI_SSID = "Trisha's F15";
const char* WIFI_PASSWORD = "7095197758";

// -------------------- MQTT --------------------
const char* MQTT_SERVER = "test.mosquitto.org";
const int MQTT_PORT = 1883;

const char* MQTT_TELEMETRY =
  "smart-machine/machine01/telemetry";

const char* MQTT_STATUS =
  "smart-machine/machine01/status";

const char* MQTT_EVENT =
  "smart-machine/machine01/event";

const char* MQTT_COMMUNICATION =
  "smart-machine/system/communication";

const char* MQTT_FAILOVER =
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

HardwareSerial RS485Serial(2);

// Machine 02 Modbus slave ID
#define MACHINE02_ID 2

// -------------------- Network --------------------
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// STATE VARIABLES
// ============================================================

bool motorState = false;
bool ledState = false;
bool buzzerState = false;

bool rs485OK = false;
bool mqttOK = false;

float temperature = 0.0;
float humidity = 0.0;

float machine02Temperature = 0.0;
float machine02Humidity = 0.0;

bool machine02Motor = false;
bool machine02LED = false;

// Timers
unsigned long lastSensorRead = 0;
unsigned long lastModbusPoll = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastWiFiAttempt = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long MODBUS_INTERVAL = 3000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;

// ============================================================
// MODBUS CRC16
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

void rs485TransmitMode()
{
  digitalWrite(RS485_DE_RE, HIGH);
}

void rs485ReceiveMode()
{
  digitalWrite(RS485_DE_RE, LOW);
}

// ============================================================
// WIFI
// ============================================================

void handleWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  unsigned long now = millis();

  if (now - lastWiFiAttempt < WIFI_RETRY_INTERVAL)
  {
    return;
  }

  lastWiFiAttempt = now;

  Serial.println();
  Serial.println("Wi-Fi disconnected.");
  Serial.println("Trying Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

// ============================================================
// MQTT
// ============================================================

void connectMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqttClient.connected())
  {
    return;
  }

  unsigned long now = millis();

  if (now - lastMQTTAttempt < MQTT_RETRY_INTERVAL)
  {
    return;
  }

  lastMQTTAttempt = now;

  Serial.println("Connecting MQTT...");

  String clientID = "Machine01-";
  clientID += String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientID.c_str()))
  {
    mqttOK = true;

    Serial.println("MQTT connected.");

    mqttClient.publish(
      MQTT_STATUS,
      "Machine 01 ONLINE",
      true
    );

    mqttClient.publish(
      MQTT_COMMUNICATION,
      "Machine 01 MQTT ONLINE",
      true
    );
  }
  else
  {
    mqttOK = false;

    Serial.print("MQTT failed. State: ");
    Serial.println(mqttClient.state());
  }
}

// ============================================================
// HARDWARE CONTROL
// ============================================================

void setMotor(bool state)
{
  motorState = state;

  digitalWrite(RELAY_PIN, state ? HIGH : LOW);

  Serial.print("Motor 1: ");
  Serial.println(state ? "ON" : "OFF");
}

void setLED(bool state)
{
  ledState = state;

  digitalWrite(LED_PIN, state ? HIGH : LOW);

  Serial.print("LED 1: ");
  Serial.println(state ? "ON" : "OFF");
}

void setBuzzer(bool state)
{
  buzzerState = state;

  digitalWrite(BUZZER_PIN, state ? HIGH : LOW);

  Serial.print("Buzzer: ");
  Serial.println(state ? "ON" : "OFF");
}

// ============================================================
// DHT11
// ============================================================

void readDHT11()
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

  Serial.print("Machine 01 Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Machine 01 Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  // High temperature safety indication
  if (temperature >= 35.0)
  {
    setLED(true);
    setBuzzer(true);

    mqttClient.publish(
      MQTT_EVENT,
      "Machine 01 HIGH TEMPERATURE",
      false
    );
  }
}

// ============================================================
// BUTTON
// ============================================================

void handleButton()
{
  static bool lastButtonState = HIGH;
  static unsigned long lastDebounce = 0;

  bool currentState = digitalRead(BUTTON_PIN);

  if (currentState != lastButtonState)
  {
    lastDebounce = millis();
  }

  if ((millis() - lastDebounce) > 50)
  {
    if (lastButtonState == HIGH && currentState == LOW)
    {
      setMotor(!motorState);
      setLED(!ledState);
      setBuzzer(!buzzerState);

      if (mqttClient.connected())
      {
        mqttClient.publish(
          MQTT_EVENT,
          "Machine 01 BUTTON PRESSED",
          false
        );
      }
    }
  }

  lastButtonState = currentState;
}

// ============================================================
// MODBUS MASTER
// ============================================================

bool pollMachine02()
{
  // Request:
  // ID = 2
  // Function = 03
  // Start register = 0
  // Quantity = 4

  uint8_t request[8];

  request[0] = MACHINE02_ID;
  request[1] = 0x03;
  request[2] = 0x00;
  request[3] = 0x00;
  request[4] = 0x00;
  request[5] = 0x04;

  uint16_t crc = modbusCRC(request, 6);

  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  // Clear receive buffer
  while (RS485Serial.available())
  {
    RS485Serial.read();
  }

  // Transmit
  rs485TransmitMode();

  delayMicroseconds(100);

  RS485Serial.write(request, 8);
  RS485Serial.flush();

  delayMicroseconds(300);

  // Receive
  rs485ReceiveMode();

  uint8_t response[13];

  int index = 0;

  unsigned long startTime = millis();

  while (millis() - startTime < 500)
  {
    while (RS485Serial.available())
    {
      if (index < 13)
      {
        response[index++] = RS485Serial.read();
      }
      else
      {
        RS485Serial.read();
      }
    }

    if (index >= 13)
    {
      break;
    }

    delay(1);
  }

  if (index < 13)
  {
    Serial.println("Machine 02 Modbus timeout.");

    rs485OK = false;

    return false;
  }

  // Basic response validation
  if (response[0] != MACHINE02_ID)
  {
    Serial.println("Machine 02 invalid slave ID.");

    rs485OK = false;

    return false;
  }

  if (response[1] != 0x03)
  {
    Serial.println("Machine 02 invalid function.");

    rs485OK = false;

    return false;
  }

  if (response[2] != 8)
  {
    Serial.println("Machine 02 invalid byte count.");

    rs485OK = false;

    return false;
  }

  // Check CRC
  uint16_t receivedCRC =
    response[11] |
    ((uint16_t)response[12] << 8);

  uint16_t calculatedCRC =
    modbusCRC(response, 11);

  if (receivedCRC != calculatedCRC)
  {
    Serial.println("Machine 02 CRC error.");

    rs485OK = false;

    return false;
  }

  // Extract registers
  uint16_t tempValue =
    ((uint16_t)response[3] << 8) |
    response[4];

  uint16_t humidityValue =
    ((uint16_t)response[5] << 8) |
    response[6];

  uint16_t motorValue =
    ((uint16_t)response[7] << 8) |
    response[8];

  uint16_t ledValue =
    ((uint16_t)response[9] << 8) |
    response[10];

  machine02Temperature = tempValue / 10.0;
  machine02Humidity = humidityValue / 10.0;

  machine02Motor = motorValue != 0;
  machine02LED = ledValue != 0;

  rs485OK = true;

  Serial.println("Machine 02 Modbus SUCCESS.");

  Serial.print("Machine 02 Temperature: ");
  Serial.print(machine02Temperature);
  Serial.println(" C");

  Serial.print("Machine 02 Humidity: ");
  Serial.print(machine02Humidity);
  Serial.println(" %");

  Serial.print("Machine 02 Motor: ");
  Serial.println(machine02Motor ? "ON" : "OFF");

  Serial.print("Machine 02 LED: ");
  Serial.println(machine02LED ? "ON" : "OFF");

  return true;
}

// ============================================================
// MQTT TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String payload = "{";

  payload += "\"machine\":1,";
  payload += "\"temperature\":" + String(temperature, 2) + ",";
  payload += "\"humidity\":" + String(humidity, 2) + ",";
  payload += "\"motor\":" + String(motorState ? 1 : 0) + ",";
  payload += "\"led\":" + String(ledState ? 1 : 0) + ",";
  payload += "\"buzzer\":" + String(buzzerState ? 1 : 0) + ",";
  payload += "\"rs485\":" + String(rs485OK ? 1 : 0) + ",";
  payload += "\"mqtt\":" + String(mqttOK ? 1 : 0) + ",";
  payload += "\"machine02_temperature\":" +
             String(machine02Temperature, 2) + ",";
  payload += "\"machine02_humidity\":" +
             String(machine02Humidity, 2) + ",";
  payload += "\"machine02_motor\":" +
             String(machine02Motor ? 1 : 0) + ",";
  payload += "\"machine02_led\":" +
             String(machine02LED ? 1 : 0);

  payload += "}";

  mqttClient.publish(
    MQTT_TELEMETRY,
    payload.c_str()
  );
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("====================================");
  Serial.println(" MACHINE 01 STARTING");
  Serial.println(" Smart Machine Communication System");
  Serial.println("====================================");

  // GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(RS485_DE_RE, OUTPUT);

  // Initial states
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  rs485ReceiveMode();

  // DHT
  dht.begin();

  // RS-485
  RS485Serial.begin(
    9600,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  // MQTT
  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  // Wi-Fi
  WiFi.mode(WIFI_STA);

  Serial.println("Starting Wi-Fi...");

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.println("Machine 01 READY.");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  // ----------------------------------------------------------
  // IMPORTANT:
  // RS-485 is serviced independently of Wi-Fi.
  // ----------------------------------------------------------

  handleButton();

  // MQTT processing
  if (mqttClient.connected())
  {
    mqttClient.loop();
  }

  // Wi-Fi state machine
  handleWiFi();

  // MQTT connection
  connectMQTT();

  // DHT11
  if (millis() - lastSensorRead >= SENSOR_INTERVAL)
  {
    lastSensorRead = millis();

    readDHT11();
  }

  // Machine 02 Modbus
  if (millis() - lastModbusPoll >= MODBUS_INTERVAL)
  {
    lastModbusPoll = millis();

    Serial.println();
    Serial.println("Polling Machine 02...");

    if (pollMachine02())
    {
      Serial.println("RS-485 communication HEALTHY.");
    }
    else
    {
      Serial.println("RS-485 communication FAILED.");
    }
  }

  // MQTT telemetry
  static unsigned long lastTelemetry = 0;

  if (millis() - lastTelemetry >= 3000)
  {
    lastTelemetry = millis();

    publishTelemetry();
  }

  delay(5);
}