#define DEBUG_SERIAL 1

#if DEBUG_SERIAL
  #define LOG_BEGIN(baud)    Serial.begin(baud)
  #define LOG_PRINT(...)     Serial.print(__VA_ARGS__)
  #define LOG_PRINTLN(...)   Serial.println(__VA_ARGS__)
#else
  #define LOG_BEGIN(baud)    ((void)0)
  #define LOG_PRINT(...)     ((void)0)
  #define LOG_PRINTLN(...)   ((void)0)
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <SPI.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <MFRC522.h>

// Wi-Fi and MQTT settings
const char* ssid        = "1819_Guest";
const char* password    = "";
const char* mqtt_server = "172.20.0.229";  
constexpr int MQTT_PORT = 1883;

const char* NODE_ID = "Node_2";

// Topics aligned with the master ecosystem
String telemetryTopic = String("scada/node/") + NODE_ID + "/data";
String statusTopic    = String("scada/") + NODE_ID + "/status";
String commandTopic   = String("scada/") + NODE_ID + "/command"; // Web UI command listener

// Pin assignments
constexpr int PIR_PIN         = 34;
constexpr int CAM_TRIGGER_PIN = 25;

constexpr int RFID_SS_PIN     = 5;
constexpr int RFID_RST_PIN    = 22;

constexpr int RED_LED         = 27;
constexpr int GREEN_LED       = 14;
constexpr int BLUE_LED        = 21;
constexpr int BUZZER          = 26;

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

constexpr unsigned long PUBLISH_INTERVAL_MS = 2000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;

unsigned long lastPublish = 0;
unsigned long lastMqttAttempt = 0;

bool readRFID(String& uid) {
  uid = "";
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return false;
  }

  char byteText[3];
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    snprintf(byteText, sizeof(byteText), "%02X", mfrc522.uid.uidByte[i]);
    if (i > 0) uid += ":";
    uid += byteText;
  }

  LOG_PRINT("RFID UID: ");
  LOG_PRINTLN(uid);

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  return true;
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  LOG_PRINT("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    LOG_PRINT(".");
  }

  LOG_PRINTLN("\nWi-Fi connected");
  LOG_PRINT("IP address: ");
  LOG_PRINTLN(WiFi.localIP());
}

// Catch commands from the Flask Web Dashboard
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  LOG_PRINT("Command received: ");
  LOG_PRINTLN(message);

  if (message == "toggle") {
    LOG_PRINTLN("Manual Camera Trigger via Web UI!");
    digitalWrite(CAM_TRIGGER_PIN, HIGH);
    delay(100);
    digitalWrite(CAM_TRIGGER_PIN, LOW);
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttempt = now;

  String clientId = String("ESP32-") + NODE_ID;
  LOG_PRINT("Connecting to MQTT...");

  if (mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "Offline")) {
    LOG_PRINTLN(" connected");
    mqtt.publish(statusTopic.c_str(), "Online", true);
    mqtt.subscribe(commandTopic.c_str()); 
  } else {
    LOG_PRINT(" failed, state=");
    LOG_PRINTLN(mqtt.state());
  }
}

void publishTelemetry(bool pirDetected, bool rfidDetected, const String& rfidUid) {
  StaticJsonDocument<192> doc;

  doc["node"] = NODE_ID;
  doc["pir_detected"] = pirDetected;
  doc["rfid_detected"] = rfidDetected;
  doc["buzzer_on"] = digitalRead(BUZZER) == HIGH;
  
  // Injecting "value" so the UNO Q TFT screen has a metric to display
  doc["value"] = pirDetected ? 1.0 : 0.0;

  if (rfidDetected) {
    doc["rfid_uid"] = rfidUid;
  } else {
    doc["rfid_uid"] = nullptr;
  }

  char payload[192];
  size_t length = serializeJson(doc, payload, sizeof(payload));

  if (length > 0) {
    mqtt.publish(telemetryTopic.c_str(), payload);
    LOG_PRINT("Published: ");
    LOG_PRINTLN(payload);
  }
}

void happy() {
  digitalWrite(BUZZER, LOW); delay(250);
  digitalWrite(BLUE_LED, LOW); delay(250);
  digitalWrite(RED_LED, LOW); delay(250);
  digitalWrite(GREEN_LED, HIGH); delay(250);
}

void gtg() {
  digitalWrite(GREEN_LED, LOW); delay(250);
  digitalWrite(RED_LED, LOW); delay(250);
  digitalWrite(BLUE_LED, HIGH); delay(250);
  digitalWrite(BUZZER, HIGH); delay(5000);
}

void gtgh() {
  digitalWrite(BLUE_LED, LOW); delay(250);
  digitalWrite(GREEN_LED, LOW); delay(250);
  digitalWrite(RED_LED, HIGH); delay(250);
  digitalWrite(BUZZER, HIGH); delay(250);

  digitalWrite(CAM_TRIGGER_PIN, HIGH);
  delay(100);
  digitalWrite(CAM_TRIGGER_PIN, LOW);

  delay(5000);
}

void setup() {
  LOG_BEGIN(9600);

  pinMode(PIR_PIN, INPUT);
  pinMode(CAM_TRIGGER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(CAM_TRIGGER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(BLUE_LED, LOW);
  digitalWrite(BUZZER, LOW);

  SPI.begin(18, 19, 23, RFID_SS_PIN);  // SCK, MISO, MOSI, SS
  mfrc522.PCD_Init();

  mqtt.setServer(mqtt_server, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  LOG_PRINTLN("RFID ready");
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  reconnectMQTT();
  if (mqtt.connected()) mqtt.loop();

  String rfidUid;
  bool rfidDetected = readRFID(rfidUid);
  bool pirDetected = digitalRead(PIR_PIN) == HIGH;

  if (rfidDetected) {
    happy();
    delay(5000);
  } else if (pirDetected) {
    digitalWrite(BUZZER, HIGH);
    delay(5000);

    if (digitalRead(PIR_PIN) == HIGH) {
      gtg();
      delay(5000);

      if (digitalRead(PIR_PIN) == HIGH) {
        gtgh();
      }
    }
  }

  if (mqtt.connected()) {
    mqtt.loop();
    unsigned long now = millis();
    if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
      lastPublish = now;
      publishTelemetry(pirDetected, rfidDetected, rfidUid);
    }
  }
}