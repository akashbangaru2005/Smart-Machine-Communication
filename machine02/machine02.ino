#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// MACHINE 02
// SMART MACHINE COMMUNICATION SYSTEM
// MODBUS RTU SLAVE
// ============================================================

// -------------------- Wi-Fi --------------------
const char* WIFI_SSID = "Trisha's F15";
const char* WIFI_PASSWORD = "7095197758";

// -------------------- MQTT --------------------
const char* MQTT_SERVER = "test.mosquitto.org";
const int MQTT_PORT = 1883;

const char* MQTT_TELEMETRY =
  "smart-machine/machine02/telemetry";

const char* MQTT_STATUS =
  "smart-machine/machine02/status";

const char* MQTT_EVENT =
  "smart-machine/machine02/event";

// -------------------- DHT11 --------------------
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(DHT_PIN, DHT_TYPE);

// -------------------- Hardware --------------------
#define LED_PIN   2
#define RELAY_PIN 23

// -------------------- RS-485 --------------------
#define RS485_RX    16
#define RS485_TX    17
#define RS485_DE_RE 21

HardwareSerial RS485Serial(2);

// Modbus Slave ID
#define SLAVE_ID 2

// -------------------- Network --------------------
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ============================================================
// VARIABLES
// ============================================================

float temperature = 0.0;
float humidity = 0.0;

bool motorState = false;
bool ledState = false;

bool mqttOK = false;

// Timers
unsigned long lastSensorRead = 0;
unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;
unsigned long lastTelemetry = 0;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;
const unsigned long TELEMETRY_INTERVAL = 3000;

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
// RS-485
// ============================================================

void rs485ReceiveMode()
{
  digitalWrite(RS485_DE_RE, LOW);
}

void rs485TransmitMode()
{
  digitalWrite(RS485_DE_RE, HIGH);
}

// ============================================================
// HARDWARE CONTROL
// ============================================================

void setMotor(bool state)
{
  motorState = state;

  digitalWrite(
    RELAY_PIN,
    state ? HIGH : LOW
  );

  Serial.print("Machine 02 Motor: ");
  Serial.println(state ? "ON" : "OFF");
}

void setLED(bool state)
{
  ledState = state;

  digitalWrite(
    LED_PIN,
    state ? HIGH : LOW
  );

  Serial.print("Machine 02 LED: ");
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
    Serial.println("Machine 02 DHT11 read failed.");
    return;
  }

  temperature = t;
  humidity = h;

  Serial.print("Machine 02 Temperature: ");
  Serial.print(temperature);
  Serial.println(" C");

  Serial.print("Machine 02 Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
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

  Serial.println("Trying Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );
}

// ============================================================
// MQTT
// ============================================================

void handleMQTT()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    return;
  }

  if (mqttClient.connected())
  {
    mqttClient.loop();
    return;
  }

  unsigned long now = millis();

  if (now - lastMQTTAttempt < MQTT_RETRY_INTERVAL)
  {
    return;
  }

  lastMQTTAttempt = now;

  Serial.println("Connecting MQTT...");

  String clientID = "Machine02-";
  clientID += String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientID.c_str()))
  {
    mqttOK = true;

    Serial.println("Machine 02 MQTT connected.");

    mqttClient.publish(
      MQTT_STATUS,
      "Machine 02 ONLINE",
      true
    );
  }
  else
  {
    mqttOK = false;

    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqttClient.state());
  }
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

  payload += "\"machine\":2,";
  payload += "\"temperature\":" +
             String(temperature, 2) + ",";
  payload += "\"humidity\":" +
             String(humidity, 2) + ",";
  payload += "\"motor\":" +
             String(motorState ? 1 : 0) + ",";
  payload += "\"led\":" +
             String(ledState ? 1 : 0) + ",";
  payload += "\"mqtt\":" +
             String(mqttOK ? 1 : 0);

  payload += "}";

  mqttClient.publish(
    MQTT_TELEMETRY,
    payload.c_str()
  );
}

// ============================================================
// MODBUS SLAVE
// ============================================================

void handleModbus()
{
  // We need at least 8 bytes for a standard request:
  //
  // [ID]
  // [Function]
  // [Start Hi]
  // [Start Lo]
  // [Quantity Hi]
  // [Quantity Lo]
  // [CRC Lo]
  // [CRC Hi]

  if (RS485Serial.available() < 8)
  {
    return;
  }

  uint8_t request[8];

  for (int i = 0; i < 8; i++)
  {
    request[i] = RS485Serial.read();
  }

  // Validate slave ID
  if (request[0] != SLAVE_ID)
  {
    return;
  }

  // Only Function 03 is supported
  if (request[1] != 0x03)
  {
    return;
  }

  // Validate CRC
  uint16_t receivedCRC =
    request[6] |
    ((uint16_t)request[7] << 8);

  uint16_t calculatedCRC =
    modbusCRC(request, 6);

  if (receivedCRC != calculatedCRC)
  {
    Serial.println("Machine 02: Modbus CRC error.");
    return;
  }

  // Requested quantity
  uint16_t quantity =
    ((uint16_t)request[4] << 8) |
    request[5];

  // Machine 01 requests 4 registers
  if (quantity != 4)
  {
    return;
  }

  // ----------------------------------------------------------
  // Four registers
  //
  // Register 0 = Temperature x10
  // Register 1 = Humidity x10
  // Register 2 = Motor
  // Register 3 = LED
  // ----------------------------------------------------------

  uint16_t tempValue =
    (uint16_t)(temperature * 10.0);

  uint16_t humidityValue =
    (uint16_t)(humidity * 10.0);

  uint16_t motorValue =
    motorState ? 1 : 0;

  uint16_t ledValue =
    ledState ? 1 : 0;

  uint8_t response[13];

  response[0] = SLAVE_ID;
  response[1] = 0x03;
  response[2] = 8;

  response[3] = tempValue >> 8;
  response[4] = tempValue & 0xFF;

  response[5] = humidityValue >> 8;
  response[6] = humidityValue & 0xFF;

  response[7] = motorValue >> 8;
  response[8] = motorValue & 0xFF;

  response[9] = ledValue >> 8;
  response[10] = ledValue & 0xFF;

  uint16_t crc =
    modbusCRC(response, 11);

  response[11] = crc & 0xFF;
  response[12] = crc >> 8;

  // ----------------------------------------------------------
  // Send response
  // ----------------------------------------------------------

  rs485TransmitMode();

  delayMicroseconds(100);

  RS485Serial.write(
    response,
    13
  );

  RS485Serial.flush();

  delayMicroseconds(300);

  rs485ReceiveMode();

  Serial.println(
    "Machine 02: Modbus response sent."
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
  Serial.println(" MACHINE 02 STARTING");
  Serial.println(" MODBUS RTU SLAVE ID: 2");
  Serial.println("====================================");

  // Hardware
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RS485_DE_RE, OUTPUT);

  // Initial states
  digitalWrite(LED_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  motorState = false;
  ledState = false;

  // RS-485 receive mode
  rs485ReceiveMode();

  // DHT11
  dht.begin();

  // RS-485 UART
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

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  Serial.println("Machine 02 READY.");
  Serial.println("Waiting for Modbus requests...");
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  // ==========================================================
  // IMPORTANT:
  // MODBUS IS HANDLED FIRST AND CONTINUOUSLY.
  // ==========================================================

  handleModbus();

  // ----------------------------------------------------------
  // Wi-Fi
  // ----------------------------------------------------------

  handleWiFi();

  // ----------------------------------------------------------
  // MQTT
  // ----------------------------------------------------------

  handleMQTT();

  // ----------------------------------------------------------
  // DHT11
  // ----------------------------------------------------------

  if (millis() - lastSensorRead >= SENSOR_INTERVAL)
  {
    lastSensorRead = millis();

    readDHT11();
  }

  // ----------------------------------------------------------
  // MQTT telemetry
  // ----------------------------------------------------------

  if (millis() - lastTelemetry >= TELEMETRY_INTERVAL)
  {
    lastTelemetry = millis();

    publishTelemetry();
  }

  // Very small delay
  // Keeps the loop responsive to RS-485.
  delay(2);
}