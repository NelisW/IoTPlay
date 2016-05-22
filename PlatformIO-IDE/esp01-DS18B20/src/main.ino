/*
Read all the DS18B20 temperature sensors on the wire
and publish to MQTT topic
*/

extern "C"
{
    #include "user_interface.h"
}

#include <ESP8266WiFi.h>

//start OTA block
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//end OTA block

// start fixed IP block
// put the following in platformio.ini: 'upload_port = 10.0.0.32'.
IPAddress ipLocal(10, 0, 0, 32);
IPAddress ipGateway(10, 0, 0, 2);
IPAddress ipSubnetMask(255, 255, 255, 0);
// end fixed IP block

//start time of day block
#include <time.h>
bool timeofDayValid;
//end time of day block

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
#define TEMPERATURE_PRECISION 9
// arrays to hold device addresses
DeviceAddress * ds18b20Addr;
//end DS18B20 sensor

//Interrupt and timer callbacks and flags
#define ALIVETIMEOUTPERIOD 1000
int aliveTimout; // in milliseconds
volatile bool aliveTick;   //flag set by ISR, must be volatile
os_timer_t aliveTimer; // send alive signals every so often
void aliveTimerCallback(void *pArg){aliveTick = true;} // when alive timer elapses

////////////////////////////////////////////////////////////////////////////////
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

    //get the wifi up
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.print("WiFi connected: ");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    //start OTA block
    Serial.println("Starting OTA");
    //careful here, lambda functions! Don't 'fix' the code!!!!
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
    Serial.println("Finished OTA");
    //end OTA block

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

////////////////////////////////////////////////////////////////////////////////
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


////////////////////////////////////////////////////////////////////////////////
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
            Serial.println(" try again in 1 seconds");
            delay(1000);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
//start time of day block
bool synchroniseLocalTime()
{
    //we are not overly concerned with the real time,
    //just do it as init values and once per day
    bool timeofDayValid = false;
    int timeCnt = 0;
    if (WiFi.status() == WL_CONNECTED)
    {
        int timeCntMax = 5;
        //my timezone is 2 hours ahead of GMT
        configTime(2 * 3600, 0, "pool.ntp.org", "time.nist.gov");
        Serial.print("Waiting for local time update ");
        while (!time(nullptr) && timeCnt<timeCntMax)
            {
              Serial.print(".");
              delay(1000);
              timeCnt++;
            }
        time_t now = time(nullptr);
        if (timeCnt<timeCntMax) {
          timeofDayValid = true;
          Serial.println(" updated.");
          publishMQTT("home/DS18B20S0/timesynchronised/true", ctime(&now));
        }
        else
        {
          timeofDayValid = false;
          publishMQTT("home/DS18B20S0/timesynchronised/false", ctime(&now));
          Serial.println(" not updated.");
        }
    }
    return (timeofDayValid);
}
//end time of day block

////////////////////////////////////////////////////////////////////////////////
//start DS18B20 sensor
//return a string representation of the device address
String stringAddress(DeviceAddress deviceAddress)
{
  String str="";
  for (uint8_t i = 0; i < 8; i++)
  {
    // zero pad the address if necessary
    if (deviceAddress[i] < 16) str = String(str + "0");
    str = String(str + String(deviceAddress[i], HEX));
  }
  return (str);
}
//end DS18B20 sensor


////////////////////////////////////////////////////////////////////////////////
void setup()
{
    Serial.begin(115200);
    Serial.println("");
    Serial.println("-------------------------------");
    Serial.println("ESP8266 DS18B20 sensors with MQTT notification");

    setup_wifi();

    //set up mqtt and register the callback to subscribe
    mqttclient.setServer(mqtt_server, 1883);
    if (!mqttclient.connected()) {  mqttReconnect(); }

    //start DS18B20 sensor
    pinMode(DS18GPIO00, INPUT);
    //https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/master/examples/Multiple/Multiple.pde
    tempsensor.begin();
    // locate devices on the bus
    Serial.print("Locating devices...");
    Serial.print("Found ");
    Serial.print(tempsensor.getDeviceCount(), DEC);
    Serial.println(" devices.");

    // report parasite power requirements
    Serial.print("Parasite power is: ");
    if (tempsensor.isParasitePowerMode()) Serial.println("ON"); else Serial.println("OFF");

    ds18b20Addr = (DeviceAddress*) calloc(tempsensor.getDeviceCount(), sizeof(DeviceAddress));
    for (uint8_t i = 0; i < tempsensor.getDeviceCount(); i++)
    {
      tempsensor.getAddress(ds18b20Addr[i], i);
      if (ds18b20Addr[i])
      {
        // tempsensor.setResolution(ds18b20Addr[i], TEMPERATURE_PRECISION);
        Serial.print("Device Address: ");
        Serial.print(stringAddress(ds18b20Addr[i]));
        Serial.print(" ");
        Serial.print("Resolution: ");
        Serial.print(tempsensor.getResolution(ds18b20Addr[i]), DEC);
        Serial.println();
      }
    }
    //end DS18B20 sensor

    //init values
    aliveTick = false;
    aliveTimout  = ALIVETIMEOUTPERIOD; // in milliseconds
    //Define a function to be called when the timer fires
    os_timer_disarm(&aliveTimer);
    os_timer_setfn(&aliveTimer, aliveTimerCallback, NULL);
    os_timer_arm(&aliveTimer, aliveTimout, true);

    // get wall clock time from the servers
    timeofDayValid = synchroniseLocalTime();

    Serial.println("Setup completed!");
}


////////////////////////////////////////////////////////////////////////////////
void loop()
{
    //start OTA block
    ArduinoOTA.handle();
    //end OTA block

    if (!mqttclient.connected()){mqttReconnect();}
    mqttclient.loop();

    float temperature;
    temperature = 0. ;
    String httpString = "Temperature not currently available";
    char nowStr[120] ;

    // timer interrupt woke us up again
    if (aliveTick == true)
    {
        aliveTick = false;
        httpString = String("<H1> DS18B20 Temperatures</H1>");
        httpString = httpString + String("<table border=""1"">");
        httpString = httpString + String("<tr><th>Time</th><th>Device</th><th>Temperature C</th></tr>");

        time_t now = time(nullptr);
        strcpy(nowStr, ctime(&now)); // get rid of newline
        nowStr[strlen(nowStr)-1] = '\0';

        //start DS18B20 sensor
        // call sensors.requestTemperatures() to issue a global temperature
        // request to all devices on the bus
        tempsensor.requestTemperatures();
        delay (400); //wait for result
        // for all devices
        for (uint8_t i = 0; i < tempsensor.getDeviceCount(); i++)
        {
          tempsensor.getAddress(ds18b20Addr[i], i);
          if (ds18b20Addr[i])
          {
            temperature = tempsensor.getTempC(ds18b20Addr[i]);

            String spc = String(" ");
            String sls = String("/");
            String tros = String("<tr>");
            String troe = String("</tr>");
            String tcos = String("<td>");
            String tcoe = String("</td>");
            String str = String(nowStr)+spc+String(temperature);
            String strM = String("home/DS18B20S0/temperature")+sls+stringAddress(ds18b20Addr[i]);
            String strML = strM + String("T");
            httpString = httpString + tros
            + tcos + String(nowStr) + tcoe
            + tcos + stringAddress(ds18b20Addr[i]) + tcoe
            + tcos + String(temperature) + tcoe
            + troe + String("<BR>");

            // mqtt publish shorter and longer versions: time+temp or just temp
            publishMQTT(strM.c_str(),String(temperature).c_str());
            publishMQTT(strML.c_str(),str.c_str());

            Serial.println(stringAddress(ds18b20Addr[i])+spc+str);
          }
        }
        //end DS18B20 sensor
        httpString = httpString + String("</table>");

        //start time of day block
        //sync with the NTP server every day at noon or if we do not have valid time
        now = time(nullptr);
        struct tm* p_tm = localtime(&now);
        if (! timeofDayValid || (p_tm->tm_hour==12 && p_tm->tm_min==0 && p_tm->tm_sec<aliveTimout/1000))
        {
            timeofDayValid = synchroniseLocalTime();
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
        client.println(httpString);

        // time_t now = time(nullptr);
        // strcpy(strbuffer, ctime(&now));
        // client.println(strbuffer);
        //
        // client.println("<br><br>");
        //
        // if (!isnan(temperature))
        // {
        //     client.print("Temperature: ");
        //     client.print(dtostrf(temperature, 0, 0, strbuffer));
        //     client.println(" %<br/>\n");
        // }
        client.println("<BR>");
        client.println("</html>");
     }
     //end web server

    //yield to wifi and other background tasks
    yield();  // or delay(0);
}
