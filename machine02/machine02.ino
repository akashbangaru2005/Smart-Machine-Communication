#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// =====================================================
// WIFI
// =====================================================

const char* WIFI_SSID = "Trisha's F15";
const char* WIFI_PASSWORD = "7095197758";

// =====================================================
// MQTT
// =====================================================

const char* MQTT_SERVER = "test.mosquitto.org";
const int MQTT_PORT = 1883;

const char* TOPIC_TELEMETRY =
    "smart-machine/machine02/telemetry";

const char* TOPIC_STATUS =
    "smart-machine/machine02/status";

const char* TOPIC_EVENT =
    "smart-machine/machine02/event";

// =====================================================
// MACHINE 02 PINS
// =====================================================

#define DHT_PIN       4
#define DHT_TYPE      DHT11

#define LED_PIN       2
#define MOTOR_PIN     23

// =====================================================
// RS-485
// =====================================================

#define RS485_RX      16
#define RS485_TX      17
#define RS485_DE      21

#define MACHINE02_ID  2

HardwareSerial RS485Serial(2);

// =====================================================
// OBJECTS
// =====================================================

DHT dht(DHT_PIN, DHT_TYPE);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// =====================================================
// VARIABLES
// =====================================================

float temperature = 0.0;
float humidity = 0.0;

bool motorState = false;
bool ledState = false;

unsigned long lastDHTRead = 0;
unsigned long lastMQTTPublish = 0;
unsigned long lastWiFiAttempt = 0;

// =====================================================
// RS485 TRANSMIT
// =====================================================

void rs485Transmit()
{
  digitalWrite(RS485_DE, HIGH);
  delayMicroseconds(100);
}

// =====================================================
// RS485 RECEIVE
// =====================================================

void rs485Receive()
{
  digitalWrite(RS485_DE, LOW);
  delayMicroseconds(100);
}

// =====================================================
// MODBUS CRC
// =====================================================

uint16_t modbusCRC(uint8_t* data, uint8_t length)
{
  uint16_t crc = 0xFFFF;

  for (uint8_t i = 0; i < length; i++)
  {
    crc ^= data[i];

    for (uint8_t j = 0; j < 8; j++)
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

// =====================================================
// WIFI
// =====================================================

void connectWiFi()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    return;
  }

  Serial.println();
  Serial.println("================================");
  Serial.println("Machine 02 - Connecting Wi-Fi");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);

  WiFi.disconnect(true);
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 15000
  )
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("Wi-Fi connected!");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.print("RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  }
  else
  {
    Serial.println("Wi-Fi connection timeout.");
  }
}

// =====================================================
// MQTT CONNECT
// =====================================================

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

  Serial.println("Connecting Machine 02 to MQTT...");

  String clientId =
      "Machine02-" +
      String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientId.c_str()))
  {
    Serial.println("Machine 02 MQTT connected!");

    mqttClient.publish(
      TOPIC_STATUS,
      "{\"online\":true,\"protocol\":\"MQTT\"}",
      true
    );

    mqttClient.publish(
      TOPIC_EVENT,
      "{\"type\":\"SUCCESS\",\"message\":\"Machine 02 connected to MQTT\"}"
    );
  }
  else
  {
    Serial.print("MQTT connection failed. State: ");
    Serial.println(mqttClient.state());
  }
}

// =====================================================
// READ DHT11
// =====================================================

void readDHT()
{
  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  if (isnan(newTemperature) || isnan(newHumidity))
  {
    Serial.println("Machine 02 DHT11 reading failed.");
    return;
  }

  temperature = newTemperature;
  humidity = newHumidity;

  Serial.println();
  Serial.println("===== MACHINE 02 SENSOR =====");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.println("==============================");
}

// =====================================================
// MOTOR
// =====================================================

void setMotor(bool state)
{
  motorState = state;

  digitalWrite(
    MOTOR_PIN,
    motorState ? HIGH : LOW
  );

  Serial.print("Machine 02 Motor: ");
  Serial.println(
    motorState ? "ON" : "OFF"
  );
}

// =====================================================
// LED
// =====================================================

void setLED(bool state)
{
  ledState = state;

  digitalWrite(
    LED_PIN,
    ledState ? HIGH : LOW
  );

  Serial.print("Machine 02 LED: ");
  Serial.println(
    ledState ? "ON" : "OFF"
  );
}

// =====================================================
// MODBUS SLAVE RESPONSE
//
// Register 0 = Temperature × 10
// Register 1 = Humidity × 10
// Register 2 = Motor
// Register 3 = LED
// =====================================================

void handleModbusRequest()
{
  if (RS485Serial.available() < 8)
  {
    return;
  }

  uint8_t request[8];

  for (uint8_t i = 0; i < 8; i++)
  {
    request[i] = RS485Serial.read();
  }

  // Check Slave ID
  if (request[0] != MACHINE02_ID)
  {
    return;
  }

  // Check Function Code
  if (request[1] != 3)
  {
    return;
  }

  // Check CRC
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

  // Check starting address = 0
  uint16_t startingAddress =
      ((uint16_t)request[2] << 8) |
      request[3];

  // Check quantity = 4
  uint16_t quantity =
      ((uint16_t)request[4] << 8) |
      request[5];

  if (startingAddress != 0 || quantity != 4)
  {
    Serial.println("Machine 02: Invalid Modbus request.");
    return;
  }

  // ===================================================
  // CREATE RESPONSE
  // ===================================================

  uint8_t response[13];

  response[0] = MACHINE02_ID;
  response[1] = 3;
  response[2] = 8;

  // Temperature × 10
  uint16_t tempRaw =
      (uint16_t)(temperature * 10.0);

  response[3] = (tempRaw >> 8) & 0xFF;
  response[4] = tempRaw & 0xFF;

  // Humidity × 10
  uint16_t humidityRaw =
      (uint16_t)(humidity * 10.0);

  response[5] = (humidityRaw >> 8) & 0xFF;
  response[6] = humidityRaw & 0xFF;

  // Motor
  uint16_t motorRaw =
      motorState ? 1 : 0;

  response[7] = (motorRaw >> 8) & 0xFF;
  response[8] = motorRaw & 0xFF;

  // LED
  uint16_t ledRaw =
      ledState ? 1 : 0;

  response[9] = (ledRaw >> 8) & 0xFF;
  response[10] = ledRaw & 0xFF;

  // CRC
  uint16_t crc =
      modbusCRC(response, 11);

  response[11] = crc & 0xFF;
  response[12] = (crc >> 8) & 0xFF;

  // ===================================================
  // SEND RESPONSE
  // ===================================================

  rs485Transmit();

  RS485Serial.write(
    response,
    sizeof(response)
  );

  RS485Serial.flush();

  rs485Receive();

  // ===================================================
  // SERIAL DEBUG
  // ===================================================

  Serial.println();
  Serial.println("===== MODBUS RESPONSE =====");

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  Serial.print("Motor: ");
  Serial.println(
    motorState ? "ON" : "OFF"
  );

  Serial.print("LED: ");
  Serial.println(
    ledState ? "ON" : "OFF"
  );

  Serial.println("Response sent to Machine 01.");
  Serial.println("============================");
}

// =====================================================
// MQTT TELEMETRY
// =====================================================

void publishTelemetry()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String payload = "{";

  payload += "\"temperature\":";
  payload += String(temperature, 1);

  payload += ",\"humidity\":";
  payload += String(humidity, 1);

  payload += ",\"motor\":";
  payload += motorState ? "true" : "false";

  payload += ",\"led\":";
  payload += ledState ? "true" : "false";

  payload += ",\"buzzer\":false";

  payload += ",\"protocol\":\"RS-485 + MQTT\"";

  payload += ",\"rssi\":";
  payload += String(WiFi.RSSI());

  payload += "}";

  mqttClient.publish(
    TOPIC_TELEMETRY,
    payload.c_str()
  );

  Serial.println();
  Serial.println("Machine 02 MQTT telemetry:");
  Serial.println(payload);
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println("======================================");
  Serial.println(" SMART MACHINE - MACHINE 02");
  Serial.println(" MODBUS SLAVE ID = 2");
  Serial.println("======================================");

  // GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);

  // Initial state
  digitalWrite(LED_PIN, LOW);
  digitalWrite(MOTOR_PIN, LOW);

  motorState = false;
  ledState = false;

  // DHT
  dht.begin();

  // RS485
  pinMode(RS485_DE, OUTPUT);

  rs485Receive();

  RS485Serial.begin(
    9600,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  Serial.println("RS-485 initialized.");
  Serial.println("Slave ID: 2");

  // Wi-Fi
  connectWiFi();

  // MQTT
  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  connectMQTT();

  // Initial sensor reading
  readDHT();

  Serial.println();
  Serial.println("======================================");
  Serial.println(" MACHINE 02 READY");
  Serial.println(" Waiting for Machine 01...");
  Serial.println("======================================");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // -----------------------------------------------
  // Wi-Fi reconnect
  // -----------------------------------------------

  if (
    WiFi.status() != WL_CONNECTED &&
    millis() - lastWiFiAttempt >= 15000
  )
  {
    lastWiFiAttempt = millis();

    connectWiFi();
  }

  // -----------------------------------------------
  // MQTT reconnect
  // -----------------------------------------------

  if (
    WiFi.status() == WL_CONNECTED &&
    !mqttClient.connected()
  )
  {
    connectMQTT();
  }

  mqttClient.loop();

  // -----------------------------------------------
  // DHT11
  // -----------------------------------------------

  if (
    millis() - lastDHTRead >= 5000
  )
  {
    lastDHTRead = millis();

    readDHT();
  }

  // -----------------------------------------------
  // MQTT telemetry
  // -----------------------------------------------

  if (
    millis() - lastMQTTPublish >= 5000
  )
  {
    lastMQTTPublish = millis();

    publishTelemetry();
  }

  // -----------------------------------------------
  // MODBUS SLAVE
  // -----------------------------------------------

  handleModbusRequest();

  delay(2);
}