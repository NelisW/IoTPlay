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

//start http client GET block
#include <ESP8266HTTPClient.h>
//end http client GET block

// start fixed IP block
// put the following in platformio.ini: 'upload_port = 10.0.0.32'.
IPAddress ipLocal(10, 0, 0, 32);
IPAddress ipGateway(10, 0, 0, 2);
IPAddress ipSubnetMask(255, 255, 255, 0);
// end fixed IP block

//start time of day block
#include <timelib.h>
//my timezone is 2 hours ahead of GMT
#define TZHOURS 2.0
// Update interval in seconds
#define NTPSYNCINTERVAL 300
bool timeofDayValid;

//end time of day block

#include "../../../../openHABsysfiles/password.h"
//#define wifi_ssid "yourwifiSSID"
//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"
//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"
//#define wunderground_ID "yourweatherundergroundserverusername"
//#define wunderground_password "yourweatherundergroundserverpassword"

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
String httpString;
//end DS18B20 sensor

//Interrupt and timer callbacks and flags
#define ALIVETIMEOUTPERIOD 5000
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
// adapted from https://github.com/switchdoclabs/OurWeatherWeatherPlus/blob/master/Utils.h
#define countof(a) (sizeof(a) / sizeof(a[0]))
String strDateTime(time_t t)
{
  char strn[20];

  snprintf_P(strn,
             countof(strn),
             "%04u-%02u-%02uT%02u:%02u:%02u",
             year(t),
             month(t),
             day(t),
             hour(t),
             minute(t),
             second(t) );
  return String(strn);
}


////////////////////////////////////////////////////////////////////////////////
// adapted from https://github.com/switchdoclabs/OurWeatherWeatherPlus/blob/master/Utils.h
String strDateTimeURL(time_t t)
{
  char strn[25];

  snprintf_P(strn,
             countof(strn),
             "%04u-%02u-%02u+%02u%%3A%02u%%3A%02u",
             year(t),
             month(t),
             day(t),
             hour(t),
             minute(t),
             second(t) );
  return String(strn);
}


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


// begin NTP server time update
////////////////////////////////////////////////////////////////////////////////
/*-------- NTP code ----------*/
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
// NTP Servers:
static const char ntpServerName[] = "us.pool.ntp.org";

//https://github.com/PaulStoffregen/Time/blob/master/examples/TimeNTP_ESP8266WiFi/TimeNTP_ESP8266WiFi.ino

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

////////////////////////////////////////////////////////////////////////////////
time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  Serial.print(ntpServerName);
  Serial.print(": ");
  Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Received NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + TZHOURS * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}
// end NTP server time update

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

    httpString = "Temperature not currently available";

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

    // begin NTP server time update
    // set up to do updates
    timeofDayValid = false;
    Serial.println("Starting UDP");
    Udp.begin(localPort);
    Serial.print("Local port: ");
    Serial.println(Udp.localPort());
    Serial.println("waiting for sync");
    setSyncProvider(getNtpTime);
    setSyncInterval(NTPSYNCINTERVAL);
    // end NTP server time update

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
    char nowStr[120] ;
    time_t timenow = now();
    strcpy(nowStr, strDateTime(timenow).c_str()); // get rid of newline
    nowStr[strlen(nowStr)-1] = '\0';

    // timer interrupt woke us up again
    if (aliveTick == true )
    {
       if (timeStatus() == timeNotSet)
       {
         timeofDayValid = false;
       }
       else
       {
         timeofDayValid = true;
       }

        aliveTick = false;
        httpString = String("<H1> DS18B20 Temperatures</H1>");
        httpString = httpString + String("<table border=""1"">");
        httpString = httpString + String("<tr><th>Time</th><th>Device</th><th>Temperature C</th></tr>");

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

            if (timeofDayValid)
            {
            // mqtt publish shorter and longer versions: time+temp or just temp
            publishMQTT(strM.c_str(),String(temperature).c_str());
            publishMQTT(strML.c_str(),str.c_str());
            }

            Serial.println(stringAddress(ds18b20Addr[i])+spc+str);
          }
        }
        //end DS18B20 sensor
        httpString = httpString + String("</table>");

        //start time of day block
        timenow = now();
        //end time of day block

        //start http client GET block
        //https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266HTTPClient
        //http://stackoverflow.com/questions/34078497/esp8266-wificlient-simple-http-get
        //http://links2004.github.io/Arduino/dd/d8d/class_h_t_t_p_client.html
        //https://developer.ibm.com/recipes/tutorials/use-http-to-send-data-to-the-ibm-iot-foundation-from-an-esp8266/
        //https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266HTTPClient/src/ESP8266HTTPClient.h

        // Initialize and make HTTP request
        HTTPClient httpclient;
        //build the string to send to the server

        //example string, all the following on one line
        //http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php
        //?ID=KCASANFR5&PASSWORD=XXXXXX&dateutc=2000-01-01+10%3A32%3A35&winddir=230&windspeedmph=12
        //&windgustmph=12&tempf=70&rainin=0&baromin=29.1&dewptf=68.2&humidity=90&weather=&clouds=
        //&softwaretype=vws%20versionxx&action=updateraw

        time_t nowutc = timenow - TZHOURS * 3600;

        String str = "http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php";
        str = str + "?ID="+String(wunderground_ID);
        str = str + "&PASSWORD="+String(wunderground_password);
        str = str + "&dateutc="+strDateTimeURL(nowutc);
        str = str + "&tempf="+String(int(32+180.*temperature/100.));
        str = str + "&softwaretype=testing%20version00";
        str = str + "&action=updateraw";
        Serial.println(str);

        if (false &&  timeofDayValid )
       {
          httpclient.begin(str); //HTTP
          // start connection and send HTTP header
          int httpCode = httpclient.GET();
          // httpCode will be negative on error
          if(httpCode > 0)
          {
              // HTTP header has been send and Server response header has been handled
              Serial.printf("[HTTP] GET... code: %d\n", httpCode);
              if(httpCode == HTTP_CODE_OK)
              {
                  String payload = httpclient.getString();
                  Serial.println(payload);
              }
          } else
          {
              Serial.printf("[HTTP] GET... failed, error: %s\n", httpclient.errorToString(httpCode).c_str());
          }
          httpclient.end();
        }
        //end http client GET block

    } //if (aliveTick == true)

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
        client.println("<BR>");
        client.println("<BR>");
        client.println(String("Current time: ")+String(nowStr));
        String strStatus = timeofDayValid ?String("True") : String("False");
        client.println(String("NTP time sync status is "+strStatus));
        client.println("<BR>");
        client.println("</html>");
     }
     //end web server




    //yield to wifi and other background tasks
    yield();  // or delay(0);
}
