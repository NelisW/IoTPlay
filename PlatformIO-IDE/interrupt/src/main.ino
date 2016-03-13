
//install PubSubClient from http://platformio.org/#!/lib
//download the tar file and untar into the project lib folder

//ESP Pin interrupts are supported through attachInterrupt(), detachInterrupt()
//functions. Interrupts may be attached to any GPIO pin except GPIO16,
//but since GPIO6-GPIO11 are typically used to interface with the flash memory ICs
//on most esp8266 modules, applying interrupts to these pins are likely to cause problems.
//Standard Arduino interrupt types are supported: CHANGE, RISING, FALLING.


//mosquitto_sub -v -d -t "testing/mqtttest"
//mosquitto_sub -v -d -t "testing/+"

extern "C"
{
    #include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#include "../../../../openHABsysfiles/password.h"
//#define wifi_ssid "yourwifiSSID"
//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"
//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"

#define PIR0GPIO12D6 12  //PIR is on GPIO12, or D6 on the NodeMCU

// the static IP address for the shield:
//If you do OTA then also set the target IP address in platform.ini
//[env:esp12e]
//upload_port = 10.0.0.30
IPAddress ipLocal(10, 0, 0, 30);
IPAddress ipGateway(10, 0, 0, 2);
IPAddress ipSubnetMask(255, 255, 255, 0);

WiFiClient espClient;
PubSubClient client(espClient);
os_timer_t aliveTimer;
volatile bool aliveTick;   //flag set by ISR, must be volatile
volatile bool PIR0Occured; //flag set by ISR, must be volatile

void setup_wifi()
{
    delay(10);
    // We start by connecting to a WiFi network
    Serial.print("Connecting to WiFi network: ");
    Serial.println(wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    //set up the static IP
    WiFi.config(ipLocal, ipGateway, ipSubnetMask);

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
            client.publish("testing/mqtttest", "hello world");
            // ... and resubscribe
            client.subscribe("testing/espinput");
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



void aliveTimerCallback(void *pArg)
{
    aliveTick = true;
}

void PIR0_ISR()
{
    PIR0Occured = true;
}

void user_init(void)
{
    // Initialize the BUILTIN_LED pin as an output
    pinMode(BUILTIN_LED, OUTPUT);

    pinMode(PIR0GPIO12D6, INPUT);

    //Define a function to be called when the timer fires
    os_timer_disarm(&aliveTimer);
    os_timer_setfn(&aliveTimer, aliveTimerCallback, NULL);
    os_timer_arm(&aliveTimer, 5000, true);
    aliveTick = false;
    PIR0Occured = false;

}

void mqttCallback(const char* topic, const byte* payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    for (int i = 0; i < length; i++)
    {
        Serial.print((char)payload[i]);
    }
    Serial.println();

    // Switch on the LED if an 1 was received as first character
    if ((char)payload[0] == '1')
    {
        digitalWrite(BUILTIN_LED, LOW);
        // Turn the LED on (Note that LOW is the voltage level
        // but actually the LED is on; this is because
        // it is acive low on the ESP-01)
    }
    else
    {
        digitalWrite(BUILTIN_LED, HIGH);                     // Turn the LED off by making the voltage HIGH
    }
}



void setup()
{

    Serial.begin(115200);
    Serial.println("");
    Serial.println("------------------------------");
    Serial.println("ESP8266 Alarm withMQTT warnings");
    Serial.println("------------------------------");

    setup_wifi();

    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);

    attachInterrupt(PIR0GPIO12D6, PIR0_ISR, RISING);

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
    if (!client.connected())
    {
        reconnect();
    }
    client.loop();

    if (aliveTick == true)
    {
        aliveTick = false;
        publishMQTT("testing/mqtttest", "alive");
        Serial.println("alive tick");
    }

    if (PIR0Occured == true)
    {
        PIR0Occured = false;
        publishMQTT("testing/movement", "Motion!");
        Serial.println("PIR0 rising edge");
    }


    yield();             // or delay(0);
}
