#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

const char* ssid        = "1819_Guest";
const char* password    = "";
const char* mqtt_server = "172.20.0.229";
constexpr int MQTT_PORT = 1883;
const char* NODE_ID = "Node_3";

String telemetryTopic = String("scada/node/") + NODE_ID + "/data";
String statusTopic    = String("scada/") + NODE_ID + "/status";
String commandTopic   = String("scada/") + NODE_ID + "/command"; // Web UI command listener

constexpr int DHT_PIN = 4;
constexpr int MQ2_PIN = 34;
constexpr int DHT_TYPE = DHT11;
constexpr int LED_PIN = 2;

DHT dht(DHT_PIN, DHT_TYPE);
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
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi connected");
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) message += (char)payload[i];
  
  if (message == "toggle") {
    outputState = !outputState;
    digitalWrite(LED_PIN, outputState ? HIGH : LOW);
    Serial.println(outputState ? "Output ON" : "Output OFF");
  }
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED || mqtt.connected()) return;

  unsigned long now = millis();
  if (now - lastMqttAttempt < MQTT_RETRY_INTERVAL_MS) return;
  lastMqttAttempt = now;

  String clientId = String("ESP32-") + NODE_ID;
  if (mqtt.connect(clientId.c_str(), statusTopic.c_str(), 1, true, "Offline")) {
    mqtt.publish(statusTopic.c_str(), "Online", true);
    mqtt.subscribe(commandTopic.c_str()); 
  }
}

void publishTelemetry() {
  float humidity = dht.readHumidity();
  float temperatureC = dht.readTemperature();
  int mq2Raw = analogRead(MQ2_PIN);

  StaticJsonDocument<192> doc;
  doc["node"] = NODE_ID;
  
  if (isnan(temperatureC)) {
    doc["temperature_c"] = nullptr;
  } else {
    doc["temperature_c"] = temperatureC;
  }
  if (isnan(humidity)) {
    doc["humidity_percent"] = nullptr;
  } else {
    doc["humidity_percent"] = humidity;
  }
  
  doc["mq2_adc"] = mq2Raw;

  char payload[192];
  size_t length = serializeJson(doc, payload, sizeof(payload));
  if (length > 0) mqtt.publish(telemetryTopic.c_str(), payload);
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  dht.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(MQ2_PIN, ADC_11db);

  mqtt.setServer(mqtt_server, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
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