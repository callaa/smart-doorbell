#include <PubSubClient.h>
#include <ESP8266WiFi.h>

#include "bell.h"

// Create a "settings.h" file with your local
// settings like this:
/*
#define WIFI_SSID "ssid"
#define WIFI_PASSWORD "pass"
#define MQTT_SERVER "address"
#define MQTT_PORT 1883
#define CLIENT_ID "doorbell"
#define MQTT_USERNAME NULL
#define MQTT_PASSWORD NULL
*/

#include "settings.h"

// MQTT prefixes.
// Discovery prefix is used for Home Assistant device auto discovery
#define TOPIC_PREFIX "diy/" NODE_NAME "/"
#define DISCOVERY_PREFIX "homeassistant/"

// Pin configuration
static const int IN_DOOR = D1;
static const int IN_BUTTON = D2;
static const int OUT_LED = D4;
//static const int IN_PIR = D5;
static const int OUT_BELL = D6;

// Home assistant device discovery messages
#define DEVICE_JSON "\"device\": {\"identifiers\": \"" NODE_NAME "\", \"manufacturer\": \"DIY\", \"model\": \"Smart Doorbell\", \"name\": \"Doorbell\"}"
#define AVAILABILITY_TOPIC ",\"availability_topic\": \"" TOPIC_PREFIX "available" "\""
#define JSON_KV(key, value) ",\"" key "\":\"" value "\""

const char CONFIG_TOPIC1[] = DISCOVERY_PREFIX "binary_sensor/" NODE_NAME "/door/config";
const byte CONFIG_MSG1[] PROGMEM = "{"
  DEVICE_JSON
  AVAILABILITY_TOPIC
  JSON_KV("state_topic", TOPIC_PREFIX "door")
  JSON_KV("device_class", "door")
  JSON_KV("name", "Door")
  JSON_KV("unique_id", NODE_NAME "_door")
  JSON_KV("payload_on", "open")
  JSON_KV("payload_off", "closed")
  "}";

const char CONFIG_TOPIC2[] = DISCOVERY_PREFIX "device_automation/" NODE_NAME "/button_short_press/config";
const byte CONFIG_MSG2[] PROGMEM = "{"
  DEVICE_JSON
  JSON_KV("topic", TOPIC_PREFIX "button")
  JSON_KV("automation_type", "trigger")
  JSON_KV("type", "button_short_press")
  JSON_KV("subtype", "button1")
  JSON_KV("payload", "press")
  "}";


// State of the button
static int lastButtonState = 1;
static int steadyButtonState = 1;
static unsigned long buttonDebounceTs = 0;
static const unsigned long DEBOUNCE_TIME = 100;

// State of the door sensor
static int lastDoorState = 0;
static int steadyDoorState = 0;
static unsigned long doorDebounceTs = 0;

// The bell
Bell bell(OUT_BELL);

// Connectivity
void onMqttMessage(char *topic, byte *payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqttClient(MQTT_SERVER, MQTT_PORT, onMqttMessage, wifiClient);
static unsigned long lastConnectionAttemptTs = 0;
static const unsigned long CONNECT_RETRY_TIME = 20 * 1000;
String clientId;

// Notification LED
static unsigned long ledBlinkTs = 0;
static const unsigned long LED_BLINK_NO_WIFI = 1000;
static const unsigned long LED_BLINK_NO_MQTT = 100;
static int ledState = 0;

String macAddressString()
{
  uint8_t mac[6];
  WiFi.macAddress(mac);

  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

void setup() {
  pinMode(IN_DOOR, INPUT_PULLUP);
  //pinMode(IN_PIR, INPUT_PULLUP);
  pinMode(IN_BUTTON, INPUT_PULLUP);
  pinMode(OUT_BELL, OUTPUT);
  pinMode(OUT_LED, OUTPUT);

  Serial.begin(115200);
  delay(10);

  WiFi.softAPdisconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  clientId = macAddressString();
}

void loop() {
  if(WiFi.status() != WL_CONNECTED) {
	// Blink LED while not connected to WiFi
    if((unsigned long)(millis() - ledBlinkTs) >= LED_BLINK_NO_WIFI) {
      ledState = !ledState;
      digitalWrite(OUT_LED, ledState);
      ledBlinkTs = millis();
    }

  } else if(mqttClient.loop()) {
    // All good. (note: on a ESP8266 board, the LED is active-low)
    if(ledState == 0) {
      ledState = 1;
      digitalWrite(OUT_LED, ledState);
      Serial.println("MQTT connection established.");
    }

  } else {
    if((unsigned long)(millis() - lastConnectionAttemptTs) >= CONNECT_RETRY_TIME) {
      lastConnectionAttemptTs = millis();

      // LED on while connecting
      ledState = 0;
      digitalWrite(LED_BUILTIN, ledState);

      Serial.print("No MQTT connection. Client state is ");
      Serial.println(mqttClient.state());
      Serial.print("Connecting to MQTT server...");

      if(mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD, TOPIC_PREFIX "available", 0, 1, "offline", true)) {
        Serial.println("Connected!");

        // Connected! Send home assistant discovery messages
        mqttClient.publish(TOPIC_PREFIX "available", "online", true);
        mqttClient.publish_P(CONFIG_TOPIC1, CONFIG_MSG1, sizeof(CONFIG_MSG1)-1, true);
        mqttClient.publish_P(CONFIG_TOPIC2, CONFIG_MSG2, sizeof(CONFIG_MSG2)-1, true);

        // A topic for remotely ringing the bell
        if(!mqttClient.subscribe(TOPIC_PREFIX "bell")) {
          Serial.println("Could not subscribe to bell topic!");
        }

      } else {
        Serial.println("Connection failed!");
      }

    } else {
      // Blink LED while waiting for next reconnect
      if((unsigned long)(millis() - ledBlinkTs) >= LED_BLINK_NO_MQTT) {
        ledState = !ledState;
        digitalWrite(OUT_LED, ledState);
        ledBlinkTs = millis();
      }
    }
  }

  // Check button state
  const int buttonState = digitalRead(IN_BUTTON);

  if(buttonState != lastButtonState) {
    buttonDebounceTs = millis();
    lastButtonState = buttonState;
  }

  if((unsigned long)(millis() - buttonDebounceTs) >= DEBOUNCE_TIME) {
    if(steadyButtonState == 1 && buttonState == 0) {
      Serial.print("Button press");
      bell.ring(1);
      if(!mqttClient.publish(TOPIC_PREFIX "button", "press")) {
        Serial.println("Button press publish failed.");
      }
    }
    steadyButtonState = buttonState;
  }

  // Check door state
  const int doorState = digitalRead(IN_DOOR);

  if(doorState != lastDoorState) {
    doorDebounceTs = millis();
    lastDoorState = doorState;
  }

  if((unsigned long)(millis() - doorDebounceTs) >= DEBOUNCE_TIME) {
    if(steadyDoorState == 1 && doorState == 0) {
      Serial.println("Door closed");
      if(!mqttClient.publish(TOPIC_PREFIX "door", "closed", true)) {
        Serial.println("Door closed publish failed.");
      }

    } else if(steadyDoorState == 0 && doorState == 1) {
      Serial.println("Door opened");
      if(!mqttClient.publish(TOPIC_PREFIX "door", "open", true)) {
        Serial.println("Door open publish failed.");
      }
    }
    steadyDoorState = doorState;
  }

  // Check PIR sensor
#if 0
  if(digitalRead(IN_PIR) == 0) {
    if(motionDetectedTs == 0) {
      // Send notification
      Serial.println("Motion detected");
      if(!mqttClient.publish(TOPIC_PREFIX "pir", "motion")) {
        Serial.println("Motion detect publish failed.");
      }
    }

    motionDetectedTs = millis();

  } else if(motionDetectedTs > 0 && (unsigned long)(millis() - motionDetectedTs) >= MOTION_DETECT_TIME) {
    motionDetectedTs = 0;
    Serial.println("Motion cleared");
    if(!mqttClient.publish(TOPIC_PREFIX "pir", "clear")) {
      Serial.println("Motion clear publish failed.");
    }
  }
#endif

  bell.step();
}

void onMqttMessage(char *topic, byte *payload, unsigned int length)
{
  // Handle MQTT message. We listen to only one topic at the moment:
  // the bell ringing.
  // Payload is a number: 0-9
  if(length > 0) {
    int rings = payload[0];
    if(rings >= '0' && rings <= '9') {
      Serial.print("Ringing via MQTT ");
      Serial.print(int(rings - '0'));
      Serial.println(" times.");
      bell.ring(rings - '0');
    }
  }
}
