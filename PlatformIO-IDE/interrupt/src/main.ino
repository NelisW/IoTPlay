


//See the documentation for this code here:
//https://github.com/NelisW/myOpenHab/blob/master/docs/421-ESP-PIR-alarm.md

extern "C"
{
    #include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
// start OTA block
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// end OTA block

// start fixed IP block
//put the following in platformio.ini:
//upload_port = 10.0.0.30
//this will tell platformio to use the wifi to upload
//uncomment the line in platformio.ini to disable OTA and use USB
// end fixed IP block

#include "../../../../openHABsysfiles/password.h"
//#define wifi_ssid "yourwifiSSID"
//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"
//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"

#define PIR0GPIO12D6 12  //PIR0 is on GPIO12, or D6 on the NodeMCU
#define PIR1GPIO13D7 13  //PIR1 is on GPIO13, or D7 on the NodeMCU
#define PIR2GPIO14D5 14  //PIR2 is on GPIO14, or D5 on the NodeMCU

#define LEDGPIO02D4 2  //LED is on GPIO02, or D4 on the NodeMCU
const int LEDPIRTimeOn = 1000U; //millisecs
const int LEDCtlTimeOn = 3000U; //millisecs

// start fixed IP block
//If you do OTA then also set the target IP address in platform.ini
//[env:esp12e]
//upload_port = 10.0.0.30
IPAddress ipLocal(10, 0, 0, 30);
IPAddress ipGateway(10, 0, 0, 2);
IPAddress ipSubnetMask(255, 255, 255, 0);
// end fixed IP block

WiFiClient espClient;
PubSubClient client(espClient);

//Interrupt and timer callbacks and flags
volatile bool aliveTick;   //flag set by ISR, must be volatile
volatile bool PIR0Occured; //flag set by ISR, must be volatile
volatile bool PIR1Occured; //flag set by ISR, must be volatile
volatile bool PIR2Occured; //flag set by ISR, must be volatile
os_timer_t aliveTimer;
os_timer_t LEDPIRTimer;
os_timer_t LEDCtlTimer;
void PIR0_ISR(){PIR0Occured = true;}
void PIR1_ISR(){PIR1Occured = true;}
void PIR2_ISR(){PIR2Occured = true;}
volatile bool LEDPIROn; //LED on via PIR status flag
volatile bool LEDCtlOn; //LED on via MQTT status flag

// when alive timer elapses: signal alive tick
void aliveTimerCallback(void *pArg){aliveTick = true;}

// when LED PIR timer elapses: LED off
void LEDPIRTimerCallback(void *pArg)
{
    LEDPIROn = false;
}

// when LED Ctl timer elapses: LED off
void LEDCtlTimerCallback(void *pArg)
{
    LEDCtlOn = false;
}


// when any PIR triggers: Led on & start one-shot timer to switch off later
void PIR_LED_ON()
{
    LEDPIROn = true;
    os_timer_arm(&LEDPIRTimer, LEDPIRTimeOn, false);
};

void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.print("Connecting to WiFi network: ");
    Serial.println(wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    // start fixed IP block
    WiFi.config(ipLocal, ipGateway, ipSubnetMask);
    // end fixed IP block

//start OTA block
    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
    ArduinoOTA.begin();
//end OTA block

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());

    Serial.print("Sketch size:  ");
    Serial.println(ESP.getSketchSize());
    Serial.print("Flash size:   ");
    Serial.println(ESP.getFlashChipSize());
    Serial.print("Free size: ** ");
    Serial.println(ESP.getFreeSketchSpace());
}

void reconnect()
{
    // Loop until we're reconnected
    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // Attempt to connect
        // If you do not want to use a username and password, change next line to
        if (client.connect("ESP8266Client"))
        {
            ///if (client.connect("ESP8266Client", mqtt_user, mqtt_password))
            Serial.println("connected");
            // Once connected, publish an announcement...
            client.publish("alarmW/mqtttest", "hello world");
            // ... and resubscribe
            client.subscribe("alarmW/LEDCtlOn");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}


void user_init(void)
{
    // Initialize the LED pin as an output
    pinMode(LEDGPIO02D4, OUTPUT);

    pinMode(PIR0GPIO12D6, INPUT);
    pinMode(PIR1GPIO13D7, INPUT);
    pinMode(PIR2GPIO14D5, INPUT);

    //Define a function to be called when the timer fires
    os_timer_disarm(&aliveTimer);
    os_timer_setfn(&aliveTimer, aliveTimerCallback, NULL);
    os_timer_arm(&aliveTimer, 5000, true);

    os_timer_disarm(&LEDPIRTimer);
    os_timer_setfn(&LEDPIRTimer, LEDPIRTimerCallback, NULL);

    os_timer_disarm(&LEDCtlTimer);
    os_timer_setfn(&LEDCtlTimer, LEDCtlTimerCallback, NULL);

    aliveTick = false;
    PIR0Occured = false;
    PIR1Occured = false;
    PIR2Occured = false;
    LEDPIROn = false;
    LEDCtlOn = false;
}

void mqttCallback(const char* topic, const byte* payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++)
    {Serial.print((char)payload[i]);}
    Serial.println();

    // Switch on the LED if a 1 was received as first character
    // also activate the timer to switch off after some time
    if ((char)payload[0] == '1')
    {
        LEDCtlOn = true;
        os_timer_arm(&LEDCtlTimer, LEDCtlTimeOn, false);
    }
    else{LEDCtlOn = false;}
}



void setup()
{
    digitalWrite(LEDGPIO02D4, LOW);

    Serial.begin(115200);
    Serial.println("");
    Serial.println("-------------------------------");
    Serial.println("ESP8266 Alarm with MQTT warnings");

    setup_wifi();

    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);

    attachInterrupt(PIR0GPIO12D6, PIR0_ISR, RISING);
    attachInterrupt(PIR1GPIO13D7, PIR1_ISR, RISING);
    attachInterrupt(PIR2GPIO14D5, PIR2_ISR, RISING);

    user_init();
}

void publishMQTT(const char* topic, const char* payload)
{
    if (!client.publish(topic, payload))
    {
        Serial.print("Publish failed: ");
        Serial.print(topic);
        Serial.println(payload);
    }
}

void loop()
{
    // Start OTA block
    ArduinoOTA.handle();
    // end OTA block

    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    if (aliveTick == true)
    {
        aliveTick = false;
        publishMQTT("alarmW/alive", "1");
    }

    if (PIR0Occured == true)
    {
        PIR0Occured = false;
        PIR_LED_ON();
        publishMQTT("alarmW/movement/PIR0", "1");
    }

    if (PIR1Occured == true)
    {
        PIR1Occured = false;
        PIR_LED_ON();
        publishMQTT("alarmW/movement/PIR1", "1");
    }

    if (PIR2Occured == true)
    {
        PIR2Occured = false;
        PIR_LED_ON();
        publishMQTT("alarmW/movement/PIR2", "1");
    }

    if (LEDPIROn == true || LEDCtlOn == true) { digitalWrite(LEDGPIO02D4, HIGH); }
    else{ digitalWrite(LEDGPIO02D4, LOW); }


    yield();             // or delay(0);
}
