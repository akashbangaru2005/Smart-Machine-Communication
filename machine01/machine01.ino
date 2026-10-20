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
    "smart-machine/machine01/telemetry";

const char* TOPIC_STATUS =
    "smart-machine/machine01/status";

const char* TOPIC_EVENT =
    "smart-machine/machine01/event";

const char* TOPIC_COMMUNICATION =
    "smart-machine/system/communication";

const char* TOPIC_FAILOVER =
    "smart-machine/system/failover";

// =====================================================
// MACHINE 01 PINS
// =====================================================

#define DHT_PIN       4
#define DHT_TYPE      DHT11

#define BUTTON_PIN    18
#define LED_PIN       2
#define BUZZER_PIN    19
#define MOTOR_PIN     23

// =====================================================
// RS485
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

bool motorState = false;
bool ledState = false;
bool buzzerState = false;

float temperature = 0.0;
float humidity = 0.0;

float machine02Temperature = 0.0;
float machine02Humidity = 0.0;

bool rs485Connected = false;

unsigned long lastTelemetry = 0;
unsigned long lastStatus = 0;
unsigned long lastModbusPoll = 0;
unsigned long lastButtonPress = 0;
unsigned long lastWiFiAttempt = 0;

// =====================================================
// RS485 DIRECTION
// =====================================================

void rs485Transmit()
{
  digitalWrite(RS485_DE, HIGH);
  delayMicroseconds(100);
}

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
  Serial.println("Connecting to Wi-Fi...");
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
// MQTT
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

  Serial.println("Connecting to MQTT broker...");

  String clientId =
      "Machine01-" +
      String((uint32_t)ESP.getEfuseMac(), HEX);

  if (mqttClient.connect(clientId.c_str()))
  {
    Serial.println("MQTT connected!");

    mqttClient.publish(
      TOPIC_STATUS,
      "{\"online\":true,\"protocol\":\"MQTT\"}",
      true
    );

    mqttClient.publish(
      TOPIC_EVENT,
      "{\"type\":\"SUCCESS\",\"message\":\"Machine 01 connected to MQTT\"}"
    );
  }
  else
  {
    Serial.print("MQTT failed. State: ");
    Serial.println(mqttClient.state());
  }
}

// =====================================================
// DHT11
// =====================================================

void readDHT()
{
  float newTemperature = dht.readTemperature();
  float newHumidity = dht.readHumidity();

  if (isnan(newTemperature) || isnan(newHumidity))
  {
    Serial.println("DHT11 reading failed.");
    return;
  }

  temperature = newTemperature;
  humidity = newHumidity;

  Serial.print("Machine 01 Temperature: ");
  Serial.print(temperature);
  Serial.println(" °C");

  Serial.print("Machine 01 Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
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

  Serial.print("Motor: ");
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

  Serial.print("LED: ");
  Serial.println(
    ledState ? "ON" : "OFF"
  );
}

// =====================================================
// BUZZER
// =====================================================

void beepBuzzer()
{
  buzzerState = true;

  digitalWrite(
    BUZZER_PIN,
    HIGH
  );

  delay(150);

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  buzzerState = false;
}

// =====================================================
// BUTTON
// =====================================================

void checkButton()
{
  static bool lastButtonState = HIGH;

  bool currentButtonState =
      digitalRead(BUTTON_PIN);

  if (
    lastButtonState == HIGH &&
    currentButtonState == LOW &&
    millis() - lastButtonPress > 300
  )
  {
    lastButtonPress = millis();

    setMotor(!motorState);

    setLED(motorState);

    beepBuzzer();

    if (mqttClient.connected())
    {
      String message =
          String("{\"type\":\"INFO\",\"message\":\"Machine 01 motor turned ") +
          (motorState ? "ON" : "OFF") +
          "\"}";

      mqttClient.publish(
        TOPIC_EVENT,
        message.c_str()
      );
    }
  }

  lastButtonState = currentButtonState;
}

// =====================================================
// MACHINE 02 MODBUS REQUEST
//
// Register 0 = Temperature × 10
// Register 1 = Humidity × 10
// Register 2 = Motor
// Register 3 = LED
// =====================================================

void requestMachine02Data()
{
  uint8_t request[8];

  request[0] = MACHINE02_ID;
  request[1] = 3;

  request[2] = 0x00;
  request[3] = 0x00;

  request[4] = 0x00;
  request[5] = 0x04;

  uint16_t crc =
      modbusCRC(request, 6);

  request[6] = crc & 0xFF;
  request[7] = (crc >> 8) & 0xFF;

  while (RS485Serial.available())
  {
    RS485Serial.read();
  }

  rs485Transmit();

  RS485Serial.write(
    request,
    sizeof(request)
  );

  RS485Serial.flush();

  rs485Receive();

  unsigned long startTime = millis();

  uint8_t response[13];
  uint8_t index = 0;

  while (
    millis() - startTime < 1000 &&
    index < sizeof(response)
  )
  {
    if (RS485Serial.available())
    {
      response[index++] =
          RS485Serial.read();
    }
  }

  if (index != 13)
  {
    rs485Connected = false;

    Serial.println(
      "Machine 02 Modbus response failed."
    );

    return;
  }

  if (response[0] != MACHINE02_ID)
  {
    rs485Connected = false;
    return;
  }

  if (response[1] != 3)
  {
    rs485Connected = false;
    return;
  }

  uint16_t receivedCRC =
      response[11] |
      (response[12] << 8);

  uint16_t calculatedCRC =
      modbusCRC(response, 11);

  if (receivedCRC != calculatedCRC)
  {
    rs485Connected = false;

    Serial.println(
      "Machine 02 CRC error."
    );

    return;
  }

  uint16_t tempRaw =
      ((uint16_t)response[3] << 8) |
      response[4];

  uint16_t humidityRaw =
      ((uint16_t)response[5] << 8) |
      response[6];

  uint16_t motorRaw =
      ((uint16_t)response[7] << 8) |
      response[8];

  uint16_t ledRaw =
      ((uint16_t)response[9] << 8) |
      response[10];

  machine02Temperature =
      tempRaw / 10.0;

  machine02Humidity =
      humidityRaw / 10.0;

  rs485Connected = true;

  Serial.println();
  Serial.println("===== MACHINE 02 DATA =====");

  Serial.print("Temperature: ");
  Serial.print(machine02Temperature);
  Serial.println(" °C");

  Serial.print("Humidity: ");
  Serial.print(machine02Humidity);
  Serial.println(" %");

  Serial.print("Motor: ");
  Serial.println(
    motorRaw ? "ON" : "OFF"
  );

  Serial.print("LED: ");
  Serial.println(
    ledRaw ? "ON" : "OFF"
  );

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

  payload += ",\"buzzer\":";
  payload += buzzerState ? "true" : "false";

  payload += ",\"protocol\":\"MQTT\"";

  payload += ",\"rssi\":";
  payload += String(WiFi.RSSI());

  payload += ",\"machine02Temperature\":";
  payload += String(machine02Temperature, 1);

  payload += ",\"machine02Humidity\":";
  payload += String(machine02Humidity, 1);

  payload += "}";

  mqttClient.publish(
    TOPIC_TELEMETRY,
    payload.c_str()
  );

  Serial.println();
  Serial.println("MQTT telemetry published:");
  Serial.println(payload);
}

// =====================================================
// STATUS
// =====================================================

void publishStatus()
{
  if (!mqttClient.connected())
  {
    return;
  }

  String payload = "{";

  payload += "\"online\":true";

  payload += ",\"protocol\":\"";

  payload +=
      rs485Connected
      ? "RS-485 + MQTT"
      : "MQTT";

  payload += "\"";

  payload += ",\"communication\":\"";

  payload +=
      rs485Connected
      ? "Connected"
      : "MQTT Backup";

  payload += "\"";

  payload += ",\"rssi\":";
  payload += String(WiFi.RSSI());

  payload += "}";

  mqttClient.publish(
    TOPIC_STATUS,
    payload.c_str(),
    true
  );

  String communicationPayload =
      String("{\"status\":\"") +
      (rs485Connected
       ? "Connected"
       : "Backup") +
      "\",\"protocol\":\"" +
      (rs485Connected
       ? "RS-485 / Modbus RTU"
       : "MQTT") +
      "\"}";

  mqttClient.publish(
    TOPIC_COMMUNICATION,
    communicationPayload.c_str(),
    true
  );

  String failoverPayload;

  if (rs485Connected)
  {
    failoverPayload =
        "{\"status\":\"Standby\",\"protocol\":\"RS-485 / Modbus RTU\"}";
  }
  else
  {
    failoverPayload =
        "{\"status\":\"FAILOVER ACTIVE\",\"protocol\":\"MQTT\"}";
  }

  mqttClient.publish(
    TOPIC_FAILOVER,
    failoverPayload.c_str(),
    true
  );
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
  Serial.println(" SMART MACHINE - MACHINE 01");
  Serial.println("======================================");

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
    MOTOR_PIN,
    OUTPUT
  );

  digitalWrite(
    LED_PIN,
    LOW
  );

  digitalWrite(
    BUZZER_PIN,
    LOW
  );

  digitalWrite(
    MOTOR_PIN,
    LOW
  );

  dht.begin();

  pinMode(
    RS485_DE,
    OUTPUT
  );

  rs485Receive();

  RS485Serial.begin(
    9600,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  connectWiFi();

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  connectMQTT();

  Serial.println();
  Serial.println("Machine 01 ready.");
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
  // -----------------------------
  // Wi-Fi
  // -----------------------------

  if (
    WiFi.status() != WL_CONNECTED &&
    millis() - lastWiFiAttempt >= 15000
  )
  {
    lastWiFiAttempt = millis();

    connectWiFi();
  }

  // -----------------------------
  // MQTT
  // -----------------------------

  if (
    WiFi.status() == WL_CONNECTED &&
    !mqttClient.connected()
  )
  {
    connectMQTT();
  }

  mqttClient.loop();

  // -----------------------------
  // BUTTON
  // -----------------------------

  checkButton();

  // -----------------------------
  // DHT + MQTT
  // -----------------------------

  if (
    millis() - lastTelemetry >= 5000
  )
  {
    lastTelemetry = millis();

    readDHT();

    publishTelemetry();
  }

  // -----------------------------
  // MODBUS
  // -----------------------------

  if (
    millis() - lastModbusPoll >= 5000
  )
  {
    lastModbusPoll = millis();

    requestMachine02Data();
  }

  // -----------------------------
  // STATUS
  // -----------------------------

  if (
    millis() - lastStatus >= 10000
  )
  {
    lastStatus = millis();

    publishStatus();
  }

  delay(10);
}