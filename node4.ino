#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Wi-Fi and MQTT settings
const char* ssid        = "1819_Guest";
const char* password    = "";
const char* mqtt_server = "172.20.0.229";  // MQTT broker IP
constexpr int MQTT_PORT = 1883;

const char* NODE_ID = "Node_4";

String telemetryTopic = String("scada/node/") + NODE_ID + "/data";
String statusTopic    = String("scada/") + NODE_ID + "/status";
String commandTopic   = String("scada/") + NODE_ID + "/command"; // Web UI command listener

// HC-SR04 & Output Pins
constexpr int TRIG_PIN = 5;
constexpr int ECHO_PIN = 18;
constexpr int LED_PIN = 2; // Built-in ESP32 LED for toggling

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool outputState = false;
constexpr unsigned long PUBLISH_INTERVAL_MS = 2000;
constexpr unsigned long MQTT_RETRY_INTERVAL_MS = 5000;
unsigned long lastPublish = 0;
unsigned long lastMqttAttempt = 0;

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to Wi-Fi");
  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.print("Wi-Fi failed; status code: ");
    Serial.println(WiFi.status());
  }
}

// Catch commands from the Flask Web Dashboard
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  Serial.print("Command received on topic [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);

  if (message == "toggle") {
    outputState = !outputState;
    digitalWrite(LED_PIN, outputState ? HIGH : LOW);
    Serial.println(outputState ? "Output turned ON" : "Output turned OFF");
  }
}

float measureDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout after 30 ms; returns 0 if no echo is received.
  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return NAN;
  
  // Sound travels to the object and back, so divide by two.
  return duration * 0.0343f / 2.0f;
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttempt = now;

  String clientId = String("ESP32-") + NODE_ID;

  Serial.print("Connecting to MQTT...");
  if (mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "Offline")) {
    Serial.println(" connected");
    mqtt.publish(statusTopic.c_str(), "Online", true);
    
    // Subscribe to the web dashboard command topic
    mqtt.subscribe(commandTopic.c_str());
  } else {
    Serial.print(" failed, state=");
    Serial.println(mqtt.state());
  }
}

void publishTelemetry() {
  float distanceCm = measureDistanceCm();

  StaticJsonDocument<128> doc;
  doc["node"] = NODE_ID;

  if (isnan(distanceCm)) {
    doc["value"] = nullptr;
    Serial.println("No echo received");
  } else {
    doc["value"] = distanceCm;
  }

  char payload[128];
  size_t length = serializeJson(doc, payload, sizeof(payload));

  if (length > 0) {
    mqtt.publish(telemetryTopic.c_str(), payload);
    Serial.print("Published: ");
    Serial.println(payload);
  } else {
    Serial.println("Could not serialize telemetry");
  }
}

void setup() {
  Serial.begin(9600);

  // Initialize ultrasonic pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  // Initialize LED output
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  mqtt.setServer(mqtt_server, MQTT_PORT);
  mqtt.setCallback(mqttCallback);

  Serial.println("Starting ultrasonic sensor...");
  connectWiFi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  reconnectMQTT();

  if (mqtt.connected()) {
    mqtt.loop();

    unsigned long now = millis();
    if (now - lastPublish >= PUBLISH_INTERVAL_MS) {
      lastPublish = now;
      publishTelemetry();
    }
  }
}