// Code written for ESP8266 Wimos D1 mini 4M/1M SPIFFS
// Stepper Motorcontroller connected to D5, D6, D1, D7
// Main control is MQTT
// Simple webinterface available as backup if MQTT control is down

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <Stepper.h>
#include "secrets.h"

// ****** All the configuration happens here ******
// Wifi
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;
IPAddress ip(192, 168, 1, 66); // update this to the desired IP Address
IPAddress dns(192, 168, 1, 1); // set dns to local
IPAddress gateway(192, 168, 1, 1); // set gateway to match your network
IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network

// MQTT
const char* mqttServer        = MQTT_SERVER;
const char* mqttUsername      = MQTT_USER;
const char* mqttPassword      = MQTT_PASSWORD;
const int   mqttPort          = 1883;
const char* mqttTopicAnnounce = "workshop-blinds/announce";
const char* mqttTopicSet      = "workshop-blinds/set";
const char* mqttTopicStep     = "workshop-blinds/step";
const char* mqttTopicPosition = "workshop-blinds/position";

// Host name and OTA password
const char* hostName    = "workshop-blinds";
const char* otaPassword = OTA_PASSWORD;

// Stepper Motor - H-bridge driver pins and steps per revolution
// Change direction by swapping D1 and D7
const int stepsPerRevolution = 3000;  // incl gear
Stepper stepper(stepsPerRevolution, D5,D6,D1,D7);


// ****** End of configuration ******

ESP8266WebServer server(80);

WiFiClient espClient;
PubSubClient client(espClient);

// Global Variables
// At boot we assume position is max and we move it to 0
// Calibrates the position by hammering the stepper into the 0 position
// The software operates internally with steps but communicates in
// percent in MQTT messages
int currentPosition = 0;                 // Assumed closed when booted
int desiredPosition = 0; 
int positionPercent = 0;                 // 0 to 100
unsigned long previousMillis;
unsigned long mqttReconnectTimer = 0;
unsigned long wifiReconnectTimer = 0;


void mqttCallback(char* topic, byte* payload, unsigned int length) {

    // Creating safe local copies of topic and payload
    // enables publishing MQTT within the callback function
    // We avoid functions using malloc to avoid memory leaks
    
    char topicCopy[strlen(topic) + 1];
    strcpy(topicCopy, topic);
    
    char message[length + 1];
    for (int i = 0; i < length; i++) message[i] = (char)payload[i];
    
    message[length] = '\0';

    int positionValue = -101;
    positionValue =  (int)strtol(message, NULL, 10);

    // First we test for step and it has to be between -100 or 100
    if ( strcmp( topicCopy, mqttTopicStep ) == 0 ) {
        if (positionValue >= -100 || positionValue <= 100 ) {
            moveSteps(positionValue);
            return;
        } else return;
    }

    // OK so the topic was not step    
    if ( positionValue < 0 || positionValue > 100 ) return;
  
    if ( strcmp( topicCopy, mqttTopicSet ) == 0 ) {
        positionPercent = positionValue;
        desiredPosition = map(positionValue, 0, 100, 0, stepsPerRevolution);
    }

    // After boot we get last position and unsubscribe to avoid loop
    if ( strcmp( topicCopy, mqttTopicPosition ) == 0 ) {
        positionPercent = positionValue;
        desiredPosition = map(positionValue, 0, 100, 0, stepsPerRevolution);
        currentPosition = desiredPosition;
        client.unsubscribe(mqttTopicPosition);
    }
}

bool mqttConnect() {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(hostName, mqttUsername, mqttPassword)) {
        Serial.println("connected");
        // Once connected, publish an announcement...
        client.publish(mqttTopicAnnounce, "connected");
        client.subscribe(mqttTopicSet);
        client.subscribe(mqttTopicStep);
        client.subscribe(mqttTopicPosition);
    }

    return client.connected();
}


void moveSteps(int dir) {
    // One real step is too small so we actually jump 10 steps per step requested
    desiredPosition += dir * 10;
    if ( desiredPosition > stepsPerRevolution ) desiredPosition = stepsPerRevolution;
    if ( desiredPosition < 0  ) desiredPosition = 0;
    positionPercent = map(desiredPosition, 0, stepsPerRevolution, 0, 100);
}

void positionReached(void) {
    char mqttBuf[10];
    itoa( positionPercent, mqttBuf, 10 );
    client.publish(mqttTopicPosition, mqttBuf, true );
    // Turn off motor when done - we do not need holding torque
    digitalWrite(D5, LOW);
    digitalWrite(D6, LOW);
    digitalWrite(D7, LOW);
    digitalWrite(D1, LOW);
}

void sendWebPage(void) {
    String webPage = "";
    webPage += "<html><head></head><body>\n";
    webPage += "<h1>Stepper Blinds</h1>";
    webPage += "<p>Position is ";
    webPage += currentPosition;
    webPage += "</p>";
    webPage += "<p><a href=\"/\"><button>Status Only</button></a></p>\n";
    webPage += "<p><a href=\"/up\"><button>UP</button></a></p>\n";
    webPage += "<p><a href=\"/down\"><button>DOWN</button></a></p>\n";
    webPage += "</body></html>\n";
    server.send(200, "text/html", webPage);
}

void setup_wifi() {
  
    Serial.print(F("Setting static ip to : "));
    Serial.println(ip);
   
    // Connect to WiFi network
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);
  
    // ESP8266 does not follow same order as Arduino
    WiFi.mode(WIFI_STA);
    WiFi.config(ip, gateway, subnet, dns); 
    WiFi.begin(ssid, password);
   
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");  
}

void setup() {

    //Setup UART
    Serial.begin(115200);
    Serial.println("");
    Serial.println("Booting..");
 
    // Setup WIFI
    setup_wifi();
    wifiReconnectTimer = millis();

    //Setup webserver
    server.on("/", [](){
        sendWebPage();
    });

    server.on("/up",    [](){ moveSteps(1);    sendWebPage(); });
    server.on("/down",  [](){ moveSteps(-1);   sendWebPage(); });

    server.begin();

    //Setup Over The Air (OTA) reprogramming
    ArduinoOTA.setHostname(hostName);
    ArduinoOTA.setPassword(otaPassword);
    
    ArduinoOTA.onStart([]() {
        Serial.end();
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";
    
        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    });
    ArduinoOTA.onEnd([]() {  });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {  });
    ArduinoOTA.onError([](ota_error_t error) {  });
    ArduinoOTA.begin();

    //Setup MQTT
    client.setServer(mqttServer, mqttPort);
    client.setCallback(mqttCallback);
    mqttReconnectTimer = 0;
    mqttConnect();

    //Init other stuff used in loop
    previousMillis = millis();
    stepper.setSpeed(5);
}

void loop() {

    ESP.wdtFeed();
    delay(1);
    
    unsigned long currentTime = millis();

    // Handle WiFi
    if ( WiFi.status() != WL_CONNECTED ) {
        if ( currentTime - wifiReconnectTimer > 20000 )
            ESP.reset();
    } else
        wifiReconnectTimer = currentTime;

    // Handle MQTT
    if (!client.connected()) {
        if ( currentTime - mqttReconnectTimer > 5000 ) {
            mqttReconnectTimer = currentTime;
            if ( mqttConnect() ) {
                mqttReconnectTimer = 0;
            }
        }
    } else {
      client.loop();
    }

    // Handle Webserver Requests
    server.handleClient();
    // Handle OTA requests
    ArduinoOTA.handle();
    
    if ( desiredPosition > currentPosition ) {
        stepper.step(1);
        currentPosition++;
        if ( desiredPosition == currentPosition ) positionReached();
    } else if ( desiredPosition < currentPosition ) {
        stepper.step(-1);
        currentPosition--;
        if ( desiredPosition == currentPosition ) positionReached();
    }

    // Increase initial delay to 2 or 3 if motor runs too fast
    // But not too much as OTA and web handling will
    // suffer if look waits too much
}
