/*
Read all the DS18B20 temperature sensors on the wire
and publish to MQTT topic
*/

extern "C"
{
    #include "user_interface.h"
}

#include <ESP8266WiFi.h>

// start OTA block
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// end OTA block

// start fixed IP block
// put the following in platformio.ini: 'upload_port = 10.0.0.32'.
IPAddress ipLocal(10, 0, 0, 32);
IPAddress ipGateway(10, 0, 0, 2);
IPAddress ipSubnetMask(255, 255, 255, 0);
// end fixed IP block

//start time of day block
#include <time.h>

#include "../../../../openHABsysfiles/password.h"
//#define wifi_ssid "yourwifiSSID"
//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"
//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"

//start web server
WiFiServer wifiserver(80);
//end web server

#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient mqttclient(espClient);

//start DS18B20 sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define DS18GPIO00 0  //DS18B20 is on GPIO00
OneWire oneWire(DS18GPIO00);
DallasTemperature tempsensor(&oneWire);
//end DS18B20 sensor

//Interrupt and timer callbacks and flags
const int aliveTimout  = 5000; // in milliseconds
volatile bool aliveTick;   //flag set by ISR, must be volatile
os_timer_t aliveTimer; // send alive signals every so often
void aliveTimerCallback(void *pArg){aliveTick = true;} // when alive timer elapses

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
    //careful here, lambda functions! Don't 'fix' the code!!!!
    ArduinoOTA.onStart([](){Serial.println("Start");});
    ArduinoOTA.onEnd([](){Serial.println("\nEnd"); });
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

    //get the wifi up
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.print("WiFi connected: ");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    //start web server
    wifiserver.begin();
    Serial.print("Server started. use this URL to connect: ");
    Serial.print("http://");
    Serial.print(WiFi.localIP());
    Serial.println("/");
    //end web server

    //memory status
    Serial.print("Sketch size:  ");
    Serial.println(ESP.getSketchSize());
    Serial.print("Flash size:   ");
    Serial.println(ESP.getFlashChipSize());
    Serial.print("Free size: ** ");
    Serial.println(ESP.getFreeSketchSpace());
}

// a little wrapper function to print to serial if publish failed
void publishMQTT(const char* topic, const char* payload)
{
    if (!mqttclient.publish(topic, payload))
    {
        Serial.print("Publish failed: ");
        Serial.print(topic);
        Serial.println(payload);
    }
}


void mqttReconnect()
{
    while (!mqttclient.connected())
    {
        Serial.print("Attempting MQTT connection...");
        // If you do not want to use a username and password, change next line to
        // if (mqttclient.connect("ESP8266Client", mqtt_user, mqtt_password))
        if (mqttclient.connect("ESP8266Client"))
        {
            Serial.println("connected");
            publishMQTT("home/DS18B20S0/alive", "DS18B20S0 online");
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqttclient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

//start time of day block
void synchroniseLocalTime()
{
    //we are not overly concerned with the real time,
    //just do it as init values and once per day
    if (WiFi.status() == WL_CONNECTED)
    {
        //my timezone is 2 hours ahead of GMT
        configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        Serial.println("\nWaiting for time");
        while (!time(nullptr))
            {
              Serial.print(".");
              delay(1000);
            }
        time_t now = time(nullptr);
        publishMQTT("home/DS18B20S0/timesynchronised", ctime(&now));
    }
}
//end time of day block


void setup()
{
    Serial.begin(115200);
    Serial.println("");
    Serial.println("-------------------------------");
    Serial.println("ESP8266 DS18B20 sensors with MQTT notification");

    setup_wifi();

    //set up mqtt and register the callback to subscribe
    mqttclient.setServer(mqtt_server, 1883);
    if (!mqttclient.connected())
    {
        mqttReconnect();
    }

    //start DS18B20 sensor
    pinMode(DS18GPIO00, INPUT);
    tempsensor.begin();
    //end DS18B20 sensor

    //init values
    aliveTick = false;

    // get wall clock time from the servers
    synchroniseLocalTime();
}

void loop()
{
    // Start OTA block
    ArduinoOTA.handle();
    // end OTA block

    if (!mqttclient.connected()){mqttReconnect();}
    mqttclient.loop();

    float temperature;
    char strbuffer[120] ;

    //send message to confirm that we are still alive
    if (aliveTick == true)
    {
        aliveTick = false;

        //start DS18B20 sensor
        tempsensor.requestTemperatures();
        delay (200); //wait for result
        time_t now = time(nullptr);
        temperature = tempsensor.getTempCByIndex(0);
        //Arduino don't have float formatting for sprintf
        //so copy the time to buffer, format float and overwrite \r\n
        strcpy(strbuffer, ctime(&now));
        dtostrf(temperature, 7, 1, &strbuffer[strlen(strbuffer)-1]);
        publishMQTT("home/DS18B20S0/temperatureDS18B20-TC",strbuffer);
        //also publish a shorter version with only the numeric
        publishMQTT("home/DS18B20S0/temperatureDS18B20-C",dtostrf(temperature, 0, 1, strbuffer));
        //end DS18B20 sensor

        //start time of day block
        //sync with the NTP server every day at noon
        now = time(nullptr);
        struct tm* p_tm = localtime(&now);
        if (p_tm->tm_hour==12 && p_tm->tm_min==0 && p_tm->tm_sec<aliveTimout/1000)
        {
            synchroniseLocalTime();
        }
        //end time of day block
    }

     // Check if a client has connected to our web server
     WiFiClient client = wifiserver.available();
     if (client)
     {
        // Return the response
        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: text/html");
        client.println(""); //  do not forget this one
        client.println("<!DOCTYPE HTML>");
        client.println("<html>");

        time_t now = time(nullptr);
        strcpy(strbuffer, ctime(&now));
        client.println(strbuffer);
        client.println("<br><br>");

        if (!isnan(temperature))
        {
            client.print("Temperature: ");
            client.print(dtostrf(temperature, 0, 0, strbuffer));
            client.println(" %<br/>\n");
        }
        client.println("</html>");
     }
     //end web server

    //yield to wifi and other background tasks
    yield();  // or delay(0);
}
