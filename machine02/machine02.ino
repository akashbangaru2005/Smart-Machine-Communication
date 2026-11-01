#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ============================================================
// MACHINE 02 - SMART MACHINE COMMUNICATION SYSTEM
// RS-485 SLAVE
// ============================================================

// -------------------- Wi-Fi --------------------
// Put your existing Wi-Fi credentials here.
const char* WIFI_SSID = "Trisha's F15";
const char* WIFI_PASSWORD = "7095197758";

// -------------------- MQTT --------------------
const char* MQTT_SERVER = "broker.hivemq.com";
const int MQTT_PORT = 1883;

const char* TOPIC_TELEMETRY =
  "smart-machine/machine02/telemetry";

const char* TOPIC_STATUS =
  "smart-machine/machine02/status";

const char* TOPIC_EVENT =
  "smart-machine/machine02/event";

const char* TOPIC_CONTROL =
  "smart-machine/machine02/control";

const char* TOPIC_COMMUNICATION =
  "smart-machine/system/communication";

const char* TOPIC_FAILOVER =
  "smart-machine/system/failover";

// -------------------- DHT11 --------------------
#define DHT_PIN 4
#define DHT_TYPE DHT11

DHT dht(
  DHT_PIN,
  DHT_TYPE
);

// -------------------- Hardware --------------------
#define LED_PIN     2
#define BUTTON_PIN  18
#define RELAY_PIN   23

// -------------------- RS-485 --------------------
#define RS485_RX    16
#define RS485_TX    17
#define RS485_DE_RE 21

#define SLAVE_ID 2

HardwareSerial RS485Serial(2);

// -------------------- Network --------------------
WiFiClient wifiClient;

PubSubClient mqttClient(
  wifiClient
);

// ============================================================
// STATE
// ============================================================

bool motorState = false;
bool ledState = false;

bool autoMode = true;

bool manualStop = false;

bool mqttOK = false;

float temperature = 0.0;
float humidity = 0.0;

uint16_t heartbeat = 0;

uint8_t currentMode = 0;
uint8_t faultCode = 0;

// Mode:
// 0 = STANDBY
// 1 = ACTIVE
// 2 = SAFETY STOP
// 3 = MANUAL STOP

// Fault:
// 0 = NONE
// 1 = COMMUNICATION FAULT
// 2 = MOTOR FAULT

// ============================================================
// TIMERS
// ============================================================

unsigned long lastMasterHeartbeat = 0;
unsigned long lastSensorRead = 0;
unsigned long lastTelemetry = 0;

unsigned long lastWiFiAttempt = 0;
unsigned long lastMQTTAttempt = 0;

unsigned long lastButtonTime = 0;

const unsigned long MASTER_TIMEOUT = 3000;

const unsigned long SENSOR_INTERVAL = 2000;
const unsigned long TELEMETRY_INTERVAL = 1000;

const unsigned long WIFI_RETRY_INTERVAL = 10000;
const unsigned long MQTT_RETRY_INTERVAL = 5000;

const unsigned long BUTTON_DEBOUNCE = 80;

// ============================================================
// BUTTON
// ============================================================

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

// ============================================================
// CRC
// ============================================================

uint16_t modbusCRC(
  uint8_t* data,
  uint8_t length
)
{
  uint16_t crc = 0xFFFF;

  for (
    uint8_t pos = 0;
    pos < length;
    pos++
  )
  {
    crc ^= data[pos];

    for (
      uint8_t i = 0;
      i < 8;
      i++
    )
    {
      if (
        crc & 0x0001
      )
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
  digitalWrite(
    RS485_DE_RE,
    HIGH
  );
}

void rs485Receive()
{
  digitalWrite(
    RS485_DE_RE,
    LOW
  );
}

// ============================================================
// MOTOR
// ============================================================

void setMotor(
  bool state
)
{
  motorState =
    state;

  // ACTIVE LOW RELAY

  digitalWrite(
    RELAY_PIN,
    state ? LOW : HIGH
  );

  Serial.print(
    "Machine 02 Motor: "
  );

  Serial.println(
    state ? "ON" : "OFF"
  );
}

// ============================================================
// MODE
// ============================================================

void applyMode(
  uint8_t mode
)
{
  currentMode =
    mode;

  // ----------------------------------------------------------
  // STANDBY
  // ----------------------------------------------------------

  if (
    mode == 0
  )
  {
    if (!manualStop)
    {
      setMotor(false);
    }

    faultCode = 0;
  }

  // ----------------------------------------------------------
  // ACTIVE
  // ----------------------------------------------------------

  else if (
    mode == 1
  )
  {
    faultCode = 0;

    if (!manualStop)
    {
      setMotor(true);
    }
    else
    {
      setMotor(false);

      currentMode =
        3;
    }
  }

  // ----------------------------------------------------------
  // SAFETY STOP
  // ----------------------------------------------------------

  else if (
    mode == 2
  )
  {
    setMotor(false);

    faultCode = 1;

    autoMode = false;

    manualStop = false;
  }

  // ----------------------------------------------------------
  // MANUAL STOP
  // ----------------------------------------------------------

  else if (
    mode == 3
  )
  {
    setMotor(false);

    faultCode = 0;

    manualStop = true;
  }
}

// ============================================================
// WIFI
// ============================================================

void connectWiFi()
{
  if (
    WiFi.status() ==
    WL_CONNECTED
  )
    return;

  if (
    millis() -
    lastWiFiAttempt <
    WIFI_RETRY_INTERVAL
  )
    return;

  lastWiFiAttempt =
    millis();

  Serial.println(
    "Connecting to Wi-Fi..."
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.begin(
    WIFI_SSID,
    WIFI_PASSWORD
  );

  unsigned long start =
    millis();

  while (
    WiFi.status() !=
    WL_CONNECTED &&
    millis() - start < 8000
  )
  {
    delay(250);

    Serial.print(".");
  }

  Serial.println();

  if (
    WiFi.status() ==
    WL_CONNECTED
  )
  {
    Serial.println(
      "Wi-Fi connected."
    );

    Serial.print(
      "IP: "
    );

    Serial.println(
      WiFi.localIP()
    );
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

  for (
    unsigned int i = 0;
    i < length;
    i++
  )
  {
    message +=
      (char)payload[i];
  }

  message.trim();

  if (
    String(topic) !=
    TOPIC_CONTROL
  )
    return;

  Serial.print(
    "MQTT COMMAND: "
  );

  Serial.println(
    message
  );

  // ----------------------------------------------------------
  // MOTOR ON
  // ----------------------------------------------------------

  if (
    message ==
    "MOTOR_ON"
  )
  {
    manualStop = false;

    autoMode = false;

    currentMode = 1;

    setMotor(true);

    mqttClient.publish(
      TOPIC_EVENT,
      "MACHINE 02 MOTOR ON"
    );
  }

  // ----------------------------------------------------------
  // MOTOR OFF
  // ----------------------------------------------------------

  else if (
    message ==
    "MOTOR_OFF"
  )
  {
    manualStop = true;

    autoMode = false;

    currentMode = 3;

    setMotor(false);

    mqttClient.publish(
      TOPIC_EVENT,
      "MACHINE 02 MOTOR OFF"
    );
  }

  // ----------------------------------------------------------
  // LED
  // ----------------------------------------------------------

  else if (
    message ==
    "LED_ON"
  )
  {
    ledState = true;

    digitalWrite(
      LED_PIN,
      HIGH
    );
  }

  else if (
    message ==
    "LED_OFF"
  )
  {
    ledState = false;

    digitalWrite(
      LED_PIN,
      LOW
    );
  }

  // ----------------------------------------------------------
  // SAFETY RESET
  // ----------------------------------------------------------

  else if (
    message ==
    "SAFETY_RESET"
  )
  {
    faultCode = 0;

    manualStop = false;

    autoMode = true;

    currentMode = 0;

    setMotor(false);

    mqttClient.publish(
      TOPIC_EVENT,
      "MACHINE 02 SAFETY RESET"
    );
  }
}

// ============================================================
// MQTT
// ============================================================

void connectMQTT()
{
  if (
    WiFi.status() !=
    WL_CONNECTED
  )
    return;

  if (
    mqttClient.connected()
  )
    return;

  if (
    millis() -
    lastMQTTAttempt <
    MQTT_RETRY_INTERVAL
  )
    return;

  lastMQTTAttempt =
    millis();

  String clientID =
    "SmartMachine02-" +
    String(
      (uint32_t)
      ESP.getEfuseMac(),
      HEX
    );

  Serial.println(
    "Connecting to MQTT..."
  );

  if (
    mqttClient.connect(
      clientID.c_str()
    )
  )
  {
    mqttOK = true;

    mqttClient.subscribe(
      TOPIC_CONTROL
    );

    mqttClient.publish(
      TOPIC_STATUS,
      "ONLINE"
    );

    mqttClient.publish(
      TOPIC_COMMUNICATION,
      "MACHINE02_MQTT_ONLINE"
    );

    Serial.println(
      "MQTT connected."
    );
  }
  else
  {
    mqttOK = false;

    Serial.print(
      "MQTT failed: "
    );

    Serial.println(
      mqttClient.state()
    );
  }
}

// ============================================================
// DHT
// ============================================================

void readDHT()
{
  float h =
    dht.readHumidity();

  float t =
    dht.readTemperature();

  if (
    isnan(h) ||
    isnan(t)
  )
  {
    Serial.println(
      "Machine 02 DHT11 read failed."
    );

    return;
  }

  temperature =
    t;

  humidity =
    h;
}

// ============================================================
// SEND MODBUS RESPONSE
// ============================================================

void sendResponse(
  uint8_t* frame,
  uint8_t length
)
{
  rs485Transmit();

  delayMicroseconds(100);

  RS485Serial.write(
    frame,
    length
  );

  RS485Serial.flush();

  delayMicroseconds(100);

  rs485Receive();
}

// ============================================================
// PROCESS MODBUS
// ============================================================

void processModbus()
{
  if (
    RS485Serial.available() <
    8
  )
    return;

  uint8_t request[8];

  for (
    uint8_t i = 0;
    i < 8;
    i++
  )
  {
    request[i] =
      RS485Serial.read();
  }

  uint16_t receivedCRC =
    request[6] |
    (
      (uint16_t)
      request[7]
      << 8
    );

  uint16_t calculatedCRC =
    modbusCRC(
      request,
      6
    );

  if (
    receivedCRC !=
    calculatedCRC
  )
    return;

  if (
    request[0] !=
    SLAVE_ID
  )
    return;

  // ==========================================================
  // FUNCTION 06
  // WRITE SINGLE REGISTER
  // ==========================================================

  if (
    request[1] ==
    0x06
  )
  {
    uint16_t reg =
      (
        (uint16_t)
        request[2]
        << 8
      ) |
      request[3];

    uint16_t value =
      (
        (uint16_t)
        request[4]
        << 8
      ) |
      request[5];

    if (
      reg == 10
    )
    {
      lastMasterHeartbeat =
        millis();

      heartbeat++;

      // ------------------------------------------------------
      // STANDBY
      // ------------------------------------------------------

      if (
        value == 0
      )
      {
        if (!manualStop)
        {
          autoMode = true;

          currentMode =
            0;

          applyMode(0);
        }
      }

      // ------------------------------------------------------
      // ACTIVE
      // ------------------------------------------------------

      else if (
        value == 1
      )
      {
        if (!manualStop)
        {
          autoMode = true;

          currentMode =
            1;

          applyMode(1);
        }
      }

      // ------------------------------------------------------
      // SAFETY STOP
      // ------------------------------------------------------

      else if (
        value == 2
      )
      {
        autoMode = false;

        applyMode(2);
      }
    }

    // Echo write request
    sendResponse(
      request,
      8
    );

    return;
  }

  // ==========================================================
  // FUNCTION 03
  // READ HOLDING REGISTERS
  // ==========================================================

  if (
    request[1] ==
    0x03
  )
  {
    uint16_t startReg =
      (
        (uint16_t)
        request[2]
        << 8
      ) |
      request[3];

    uint16_t quantity =
      (
        (uint16_t)
        request[4]
        << 8
      ) |
      request[5];

    // We support registers 0-6
    if (
      startReg != 0 ||
      quantity < 7
    )
      return;

    lastMasterHeartbeat =
      millis();

    heartbeat++;

    uint8_t response[17];

    response[0] =
      SLAVE_ID;

    response[1] =
      0x03;

    response[2] =
      14;

    // --------------------------------------------------------
    // Register 0
    // Temperature x10
    // --------------------------------------------------------

    uint16_t reg0 =
      (uint16_t)
      (
        temperature *
        10
      );

    // --------------------------------------------------------
    // Register 1
    // Humidity x10
    // --------------------------------------------------------

    uint16_t reg1 =
      (uint16_t)
      (
        humidity *
        10
      );

    // --------------------------------------------------------
    // Register 2
    // Motor
    // --------------------------------------------------------

    uint16_t reg2 =
      motorState
      ? 1
      : 0;

    // --------------------------------------------------------
    // Register 3
    // Mode
    // --------------------------------------------------------

    uint16_t reg3 =
      currentMode;

    // --------------------------------------------------------
    // Register 4
    // Fault
    // --------------------------------------------------------

    uint16_t reg4 =
      faultCode;

    // --------------------------------------------------------
    // Register 5
    // Heartbeat
    // --------------------------------------------------------

    uint16_t reg5 =
      heartbeat;

    // --------------------------------------------------------
    // Register 6
    // Manual stop
    // --------------------------------------------------------

    uint16_t reg6 =
      manualStop
      ? 1
      : 0;

    response[3] =
      highByte(reg0);

    response[4] =
      lowByte(reg0);

    response[5] =
      highByte(reg1);

    response[6] =
      lowByte(reg1);

    response[7] =
      highByte(reg2);

    response[8] =
      lowByte(reg2);

    response[9] =
      highByte(reg3);

    response[10] =
      lowByte(reg3);

    response[11] =
      highByte(reg4);

    response[12] =
      lowByte(reg4);

    response[13] =
      highByte(reg5);

    response[14] =
      lowByte(reg5);

    response[15] =
      highByte(reg6);

    response[16] =
      lowByte(reg6);

    // --------------------------------------------------------
    // CRC
    // --------------------------------------------------------

    uint16_t crc =
      modbusCRC(
        response,
        15
      );

    uint8_t finalResponse[19];

    for (
      uint8_t i = 0;
      i < 17;
      i++
    )
    {
      finalResponse[i] =
        response[i];
    }

    finalResponse[17] =
      lowByte(crc);

    finalResponse[18] =
      highByte(crc);

    sendResponse(
      finalResponse,
      19
    );

    return;
  }
}

// ============================================================
// MASTER WATCHDOG
// ============================================================

void checkMasterHeartbeat()
{
  if (
    millis() -
    lastMasterHeartbeat >
    MASTER_TIMEOUT
  )
  {
    if (
      motorState
    )
    {
      Serial.println(
        "MASTER HEARTBEAT LOST!"
      );

      setMotor(false);
    }

    currentMode =
      2;

    faultCode =
      1;

    autoMode =
      false;
  }
}

// ============================================================
// BUTTON
// ============================================================
//
// Machine 02:
//
// Press once:
//      MOTOR OFF
//
// Press again:
//      MOTOR can run again
//
// The automatic master coordination will decide whether
// Machine 02 should be ACTIVE or STANDBY after restart.
// ============================================================

void handleButton()
{
  bool reading =
    digitalRead(
      BUTTON_PIN
    );

  if (
    reading !=
    lastButtonReading
  )
  {
    lastButtonTime =
      millis();
  }

  if (
    millis() -
    lastButtonTime >
    BUTTON_DEBOUNCE
  )
  {
    if (
      reading !=
      stableButtonState
    )
    {
      stableButtonState =
        reading;

      if (
        stableButtonState ==
        LOW
      )
      {
        // ----------------------------------------------------
        // STOP
        // ----------------------------------------------------

        if (!manualStop)
        {
          manualStop =
            true;

          setMotor(false);

          currentMode =
            3;

          faultCode =
            0;

          if (
            mqttClient.connected()
          )
          {
            mqttClient.publish(
              TOPIC_EVENT,
              "MACHINE 02 MANUAL STOP"
            );
          }

          Serial.println(
            "MACHINE 02 MANUAL STOP"
          );
        }

        // ----------------------------------------------------
        // START / RELEASE MANUAL STOP
        // ----------------------------------------------------

        else
        {
          manualStop =
            false;

          setMotor(false);

          currentMode =
            0;

          faultCode =
            0;

          if (
            mqttClient.connected()
          )
          {
            mqttClient.publish(
              TOPIC_EVENT,
              "MACHINE 02 MANUAL START"
            );
          }

          Serial.println(
            "MACHINE 02 MANUAL START"
          );
        }
      }
    }
  }

  lastButtonReading =
    reading;
}

// ============================================================
// TELEMETRY
// ============================================================

void publishTelemetry()
{
  if (
    !mqttClient.connected()
  )
    return;

  char payload[700];

  const char* modeText =
    "STANDBY";

  if (
    currentMode ==
    1
  )
  {
    modeText =
      "ACTIVE";
  }
  else if (
    currentMode ==
    2
  )
  {
    modeText =
      "SAFETY STOP";
  }
  else if (
    currentMode ==
    3
  )
  {
    modeText =
      "MANUAL STOP";
  }

  const char* faultText =
    "NONE";

  if (
    faultCode ==
    1
  )
  {
    faultText =
      "COMMUNICATION FAULT";
  }
  else if (
    faultCode ==
    2
  )
  {
    faultText =
      "MOTOR FAULT";
  }

  snprintf(
    payload,
    sizeof(payload),

    "{"
    "\"machine\":2,"
    "\"temperature\":%.1f,"
    "\"humidity\":%.1f,"
    "\"motor\":%d,"
    "\"led\":%d,"
    "\"auto_mode\":%d,"
    "\"manual_stop\":%d,"
    "\"mode\":\"%s\","
    "\"fault\":\"%s\","
    "\"rs485\":1,"
    "\"mqtt\":%d,"
    "\"heartbeat\":%u"
    "}",

    temperature,
    humidity,

    motorState ? 1 : 0,
    ledState ? 1 : 0,

    autoMode ? 1 : 0,
    manualStop ? 1 : 0,

    modeText,
    faultText,

    mqttClient.connected()
      ? 1
      : 0,

    heartbeat
  );

  mqttClient.publish(
    TOPIC_TELEMETRY,
    payload
  );
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
  Serial.begin(
    115200
  );

  delay(1000);

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    " SMART MACHINE COMMUNICATION SYSTEM"
  );

  Serial.println(
    " MACHINE 02 - SLAVE"
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
    RELAY_PIN,
    OUTPUT
  );

  pinMode(
    RS485_DE_RE,
    OUTPUT
  );

  digitalWrite(
    LED_PIN,
    LOW
  );

  setMotor(false);

  rs485Receive();

  RS485Serial.begin(
    9600,
    SERIAL_8N1,
    RS485_RX,
    RS485_TX
  );

  dht.begin();

  mqttClient.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  mqttClient.setCallback(
    mqttCallback
  );

  lastMasterHeartbeat =
    millis();

  Serial.println(
    "Machine 02 ready."
  );
}

// ============================================================
// LOOP
// ============================================================

void loop()
{
  connectWiFi();
  connectMQTT();

  if (
    mqttClient.connected()
  )
  {
    mqttClient.loop();
  }

  processModbus();

  handleButton();

  checkMasterHeartbeat();

  unsigned long now =
    millis();

  // DHT
  if (
    now -
    lastSensorRead >=
    SENSOR_INTERVAL
  )
  {
    lastSensorRead =
      now;

    readDHT();
  }

  // Telemetry
  if (
    now -
    lastTelemetry >=
    TELEMETRY_INTERVAL
  )
  {
    lastTelemetry =
      now;

    publishTelemetry();
  }
}