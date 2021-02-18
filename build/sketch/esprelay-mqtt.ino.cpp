#include <Arduino.h>
#line 1 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
/*

   MQTT-based WiFi relay

   Made for Individual project course

   2020-2021 (c) Vladislav Kalugin a.k.a CoolXacker

   https://kaluginvlad.com

   https://vk.com/coolxacker
   https://twitter.com/coolxacker
   https://t.me/kaluginvlad

*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <string.h>
#include <Ticker.h>

// ************ Some self-written libraries: **************

#include "EEPROM_tools.h"       // EEPROM tools
#include "config_tools.h"       // Configuration tools
#include "WiFi_configurator.h"  // WiFi WEB configuration utility
#include "mqtt_ca.h"            // CA authority for MQTT broker

// Web pages
#include "web_assets/c++/html_css.h"  // CSS
#include "web_assets/c++/html_root.h" // root HTML

// Build version
#define BUILD "MQTT-alpha-1.1"

bool config_ready;
bool switch_state = false;

const bool switch_reverse = false;

Ticker timetable_ticker;

/* Configuration */

#define RELAY_PIN             0

const byte DNS_PORT = 53;                           // Internal DNS server port

#define NTP_SERVER            "ntp1.stratum1.ru"    // NTP server host
#define NTP_UTC_OFFSET        0                     // NTP UTC offset

const char* MQTT_SERVER = "mqtt.minhinprom.ru";     // MQTT broker host
const int   MQTT_PORT = 8883;                       // MQTT broker port

/* ************** */

const char* DEVICE_KEY;

char MQTT_TOPIC_IN[100];
char MQTT_TOPIC_OUT[100];
char MQTT_TOPIC_REPORT[100];

// Internal DNS-server
DNSServer dns_server;

// Configuration web server port 80
ESP8266WebServer web_srv(80);

// HTTP firmware updater
ESP8266HTTPUpdateServer http_updater;

// SSL/TLS client
WiFiClientSecure wifiClient;

// MQTT client
PubSubClient mqtt_client(MQTT_SERVER, 8883, wifiClient);

// Load CA
X509List CA_CERT_X509(TRUST_MQTT_CA);

// Structure of timetable
typedef struct  {
  int timestamp;
  int timeout;
  bool switch_state;
} timetable_struct;

// Create struct for timetable (for 50 entries)
timetable_struct actions_timetable[50] = {NULL, NULL, NULL};

// Last timetable sync time
int timetable_sync = 0;
bool timetable_sync_required = false;

// Simple MQTT response function
#line 102 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void string_mqtt_response(String request_id, String response_status);
#line 113 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void json_mqtt_response(char* topic, StaticJsonDocument<256> response_data);
#line 122 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void mqtt_handler_callback(char* topic, byte* payload, unsigned int length);
#line 200 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void start_mqtt_connection();
#line 218 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void mqtt_connect();
#line 251 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void send_detailed_report();
#line 274 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void send_status_report();
#line 285 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void check_timetable();
#line 315 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void load_timetable(String timetable_payload);
#line 343 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void setup();
#line 421 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void loop();
#line 435 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void HTTP_handleRoot();
#line 440 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void HTTP_handleCSS();
#line 102 "D:\\Projects\\esp8266\\esprelay-mqtt\\esprelay-mqtt.ino"
void string_mqtt_response(String request_id, String response_status) {
  // Generate and publish response
  char mqtt_response[100];
  String mqtt_response_str = "{\"id\":" + request_id + ", \"status\": \"" + response_status + "\"}";
  Serial.print("[MQTT_SENT]: ");
  Serial.println(mqtt_response_str);
  mqtt_response_str.toCharArray(mqtt_response, 100);
  mqtt_client.publish(MQTT_TOPIC_OUT, mqtt_response);
}

// Sends JSON document VIA MQTT
void json_mqtt_response(char* topic, StaticJsonDocument<256> response_data) {
  char serialized_doc[512];
  serializeJson(response_data, serialized_doc);
  Serial.print("[MQTT_SENT]: ");
  Serial.println(serialized_doc);
  mqtt_client.publish(topic, serialized_doc);
}

// Command handler
void mqtt_handler_callback(char* topic, byte* payload, unsigned int length) {
  // Add EOL
  payload[length] = '\0';
  String mqtt_request = String((char*)payload);

  // Request format:
  // [int (request_id)]_[string (action)]

  // Get separator index
  int sep_ind = mqtt_request.indexOf("_");

  // Validate index
  if (sep_ind == -1) {
    Serial.println("[ACTIONS]: Can't parse request!");
    string_mqtt_response("-1", "parseerr");
    return;
  }

  // Parse data
  String request_id = mqtt_request.substring(0, sep_ind);
  String action = mqtt_request.substring(sep_ind + 1);

  String additional_info = "";

  // Get additional info if presented
  sep_ind = action.indexOf("_");
  if (sep_ind != -1) {
    additional_info = action.substring(sep_ind + 1);
    action = action.substring(0, sep_ind);
  }

  // DEBUG
  Serial.println("[ACTIONS]: Got request:");
  Serial.print("\t");
  Serial.println(request_id);
  Serial.print("\t");
  Serial.println(action);
  Serial.print("\t");
  Serial.println(additional_info);
  
  // Simple implementation of command interpreter
  if (action == "on") {
    digitalWrite(RELAY_PIN, !switch_reverse);
    switch_state = true;
    string_mqtt_response(request_id, "ok");
  } else if (action == "off") {
    digitalWrite(RELAY_PIN, switch_reverse);
    switch_state = false;
    string_mqtt_response(request_id, "ok");
  } else if (action == "getswitch") {
    send_status_report();
    string_mqtt_response(request_id, "ok");
  } else if (action == "getreport") {
    send_detailed_report();
    string_mqtt_response(request_id, "ok");
  } else if (action == "ntpsync") {
    Serial.println("Syncing NTP by request...");
    //time_client.update();
  } else if (action == "ttblsync") {
    Serial.println("[TIMETABLE]: Syncing timetable by request...");
    load_timetable(additional_info);
  } else if (action == "clreeprom") {
    string_mqtt_response(request_id, "ok");
    EEPROMClear();
    ESP.restart();
  } else if (action == "reboot") {
    string_mqtt_response(request_id, "ok");
    ESP.restart();
  } else if (action == "ping") {
    string_mqtt_response(request_id, "pong");
  }
  else {
    Serial.println("[ACTIONS]: Got unknown action!");
    string_mqtt_response(request_id, "unknown_action");
  }
}

// Inits SSL TCP connection to MQTT server
void start_mqtt_connection() {
  Serial.print("[MQTT]: Connecting to ");
  Serial.println(MQTT_SERVER);

  // Setup SSL cert
  wifiClient.setTrustAnchors(&CA_CERT_X509);

  while (!wifiClient.connect(MQTT_SERVER, MQTT_PORT)) {
    Serial.println("TCP connection failed! Retrying in 5 seconds...");
    delay(5000);
    return;
  }

  Serial.println("[TCP]: connection established!");
  mqtt_connect();
}

// Connects to MQTT broker
void mqtt_connect() {
  Serial.println("[MQTT]: Attempting connection...");

  if (mqtt_client.connect(DEVICE_KEY, DEVICE_KEY, DEVICE_KEY)) {
    Serial.println("[MQTT]: Connected!");

    // Subscribe to relay-in topic
    mqtt_client.subscribe(MQTT_TOPIC_IN);

    // Send "online" message to server
    string_mqtt_response("-1", "online");
    // Request timtable sync
    string_mqtt_response("-1", "get_timetable");

  } else {
    Serial.print("Fail: ");
    int mqtt_state = mqtt_client.state();
    Serial.println(mqtt_state);

    // On authentication error
    if (mqtt_state == 4 || mqtt_state == 5) {

      Serial.println("MQTT authentication failed: wrong creds!");
      EEPROMClear();

      ESP.restart();
    }

    wifiClient.stop();
  }
}

// Send detailed report to server as json
void send_detailed_report() {
  StaticJsonDocument<256> conf = readWiFiConfig();

  StaticJsonDocument<256> report_doc;
  JsonObject root = report_doc.to<JsonObject>();

  root["dev_type"] = "esp_mqtt_relay";

  root["ssid"] = conf["ssid"].as<String>();

  conf.clear(); // Unload JSON

  root["ip"] = WiFi.localIP().toString();
  root["mask"] = WiFi.subnetMask().toString();
  root["gw"] = WiFi.gatewayIP().toString();
  root["dns"] = WiFi.dnsIP().toString();
  root["switch"] = switch_state;
  root["build"] = BUILD;

  json_mqtt_response(MQTT_TOPIC_REPORT, report_doc);
}

// Send simple report (relay state) to server as json
void send_status_report() {
  StaticJsonDocument<256> status_report;
  JsonObject root = status_report.to<JsonObject>();

  root["switch"] = switch_state;

  json_mqtt_response(MQTT_TOPIC_REPORT, status_report);
}

// Checks timetable for action
// Runs by Ticker every 1000 ms
void check_timetable() {
  // Sync timetable the next day
  if (time(nullptr) - timetable_sync >= 86000 ) {
    timetable_sync_required = true;
  }

  for (int i = 0; i < 50; i++) {
    if (actions_timetable[i].timestamp == NULL) continue;

    int timedelta = time(nullptr) - actions_timetable[i].timestamp;

    if ( timedelta >= 0 and timedelta <= actions_timetable[i].timeout ) {
      Serial.println("[TIMETABLE]: Action!");

      switch_state = actions_timetable[i].switch_state;

      // Toggle switch by action
      if (switch_reverse){
        digitalWrite(RELAY_PIN, !switch_state);
      } else {
        digitalWrite(RELAY_PIN, switch_state);
      }

      // Clear completed record
      actions_timetable[i] = {NULL, NULL, NULL};
    }
  }
}

// Loads actions timetable
void load_timetable(String timetable_payload) {
  // JSON Doc for timetable
  StaticJsonDocument<1024> timetable_doc;

  // Decode data
  deserializeJson(timetable_doc, timetable_payload);

  // Clean payload string
  timetable_payload = "";

  // Clear old data
  for (int i; i < 50; i++) {
    actions_timetable[i] = {NULL, NULL, NULL};
  }

  // Write new timetable data
  for (int i = 0; i < timetable_doc.size(); i++) {
    actions_timetable[i] = {timetable_doc[i][0], timetable_doc[i][1], timetable_doc[i][2]};
  }

  Serial.println("[TIMETABLE]: Timetable sync completed!");

  // Write last timetable sync time
  timetable_sync = time(nullptr);

  timetable_ticker.attach(1, check_timetable);
}

void setup() {
  // Begin serial
  Serial.begin(115200);
  Serial.print("\nESP8266 IOT MQTT RELAY\nBuild: ");
  Serial.println(BUILD);
  Serial.println(
    "    __ __      __            _     _    ____          __                    \n"
    "   / //_/___ _/ /_  ______ _(_)___| |  / / /___ _____/ /_________  ____ ___ \n"
    "  / ,< / __ `/ / / / / __ `/ / __ \\ | / / / __ `/ __  // ___/ __ \\/ __ `__ \\\n"
    " / /| / /_/ / / /_/ / /_/ / / / / / |/ / / /_/ / /_/ // /__/ /_/ / / / / / /\n"
    "/_/ |_\\__,_/_/\\__,_/\\__, /_/_/ /_/|___/_/\\__,_/\\__,_(_)___/\\____/_/ /_/ /_/ \n"
    "                   /____/                                                   "
    );
  
  Serial.print("2020-2021 (c) Vladislav Kalugin | Some rights reserved");
  Serial.println("\n");

  // Begin EEPROM
  EEPROM.begin(4096);

  // Start WiFi init
  config_ready = WiFiConfigure(web_srv);

  // Configure DNS server
  dns_server.setTTL(300);
  dns_server.start(DNS_PORT, "smart_dev.local", IPAddress(192, 168, 4, 1));

  // Add WEB server handlers
  web_srv.on("/", HTTP_handleRoot);
  web_srv.on("/style.css", HTTP_handleCSS);

  // Start HTTP Updater
  http_updater.setup(&web_srv, "/firmware");
  // Start WEB-server
  web_srv.begin();
  Serial.println("\n[WEB_SERVER]: Ready!");

  // Setup output pin
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, switch_reverse);

  // Get access key from config
  StaticJsonDocument<256> iotconf = readWiFiConfig();
  DEVICE_KEY = iotconf["dkey"].as<char*>();
  iotconf.clear();

  // Generate MQTT topics
  String topic_str = "mqtt_relay/in/" + String(DEVICE_KEY);
  topic_str.toCharArray(MQTT_TOPIC_IN, 100);
  topic_str = "mqtt_relay/out/" + String(DEVICE_KEY);
  topic_str.toCharArray(MQTT_TOPIC_OUT, 100);
  topic_str = "mqtt_relay/report/" + String(DEVICE_KEY);
  topic_str.toCharArray(MQTT_TOPIC_REPORT, 100);

  if (config_ready) {
    Serial.print("[NTP]: Server: ");
    Serial.println(NTP_SERVER);
    Serial.print("[NTP]: Syncing TIME...");
    configTime(NTP_UTC_OFFSET, 0, NTP_SERVER);

    time_t time_now = time(nullptr);
    while (time_now < 8 * 3600 * 2) {
      delay(50);
      time_now = time(nullptr);
    }

    Serial.print("[NTP]: Current UNIX timestamp: ");
    Serial.println(time_now);

    // Start MQTT connection
    start_mqtt_connection();
    mqtt_client.setCallback(mqtt_handler_callback);

    Serial.println("[INFO]: Ready!\n");
  }

}

void loop() {
  if (config_ready) {
    if (!mqtt_client.connected()) {
      start_mqtt_connection();
    }
    mqtt_client.loop();
  }
  else {
    // Handle DNS requests in config mode
    dns_server.processNextRequest();
  }
  web_srv.handleClient(); 
}

void HTTP_handleRoot() {
  web_srv.send(200, "text/html", html_root);
}

// Handle style file
void HTTP_handleCSS() {
  web_srv.send(200, "text/css", html_css);
}

