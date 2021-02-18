#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "mhp_ca_cert.h"

// Update these with values suitable for your network.
const char* ssid = "MHP-IOT";
const char* password = "I@T_5ec(())ndary";
const char* mqtt_server = "mqtt.minhinprom.ru";

// Login and password are same. They will be same with an access key
const char* mqtt_login = "CBJBVC5blA3FLHYvfqDHSU8iIeaGK0A";
const char* mqtt_passw = "CBJBVC5blA3FLHYvfqDHSU8iIeaGK0A";
const char* mqtt_in_topic = "CBJBVC5blA3FLHYvfqDHSU8iIeaGK0A-relay-in";

WiFiClientSecure wifiClient;
PubSubClient client(mqtt_server, 8883, wifiClient);

X509List cert(trust_mhp_root);

int HeatingPin = 16;
String switch1;
String strTopic;
String strPayload;

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  strTopic = String((char*)topic);

  switch1 = String((char*)payload);
  if(switch1 == "ON")
    {
      Serial.println("ON");
      digitalWrite(HeatingPin, HIGH);
    }
  else
    {
      Serial.println("OFF");
      digitalWrite(HeatingPin, LOW);
    }
}

void connecttls() {
  // Use WiFiClientSecure class to create TLS connection
  Serial.print("connecting to ");
  Serial.println(mqtt_server);
  wifiClient.setTrustAnchors(&cert);
  if (!wifiClient.connect(mqtt_server, 8883)) {
    Serial.println("connection failed");
    return;
  }
  else {
    Serial.println("ssl/tls connection ok!");
    reconnect();
  }
}
 
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("esp8266_iot", mqtt_login, mqtt_passw)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.subscribe(mqtt_in_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
 
void setup()
{
  Serial.begin(115200);
  setup_wifi(); 

  // Set time for TLS verification
  configTime(3 * 3600, 0, "ntp1.stratum1.ru");

  Serial.print("Waiting for NTP time sync: ");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
  
  connecttls();
  client.setCallback(callback);

  pinMode(HeatingPin, OUTPUT);
  digitalWrite(HeatingPin, HIGH);
}
 
void loop()
{
  if (!client.connected()) {
    connecttls();
  }
  client.loop();
}
