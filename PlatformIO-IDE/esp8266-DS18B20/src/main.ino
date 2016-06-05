/*
Read all the DS18B20 temperature sensors on the wire interface and
- publish to MQTT topic.
- publish to Weather Underground as additional temperatures.
- publish on a web server.

Brief description is given here:
https://github.com/NelisW/myOpenHab/blob/master/docs/423-ESP-multi-DS18B20.md

Requires the following libraries (in the platformio terminal):
1. PubSubClient: install with platformio lib install 89
2. DallasTemperature: install with platformio lib install 54
3. OneWire: install  with platformio lib install 1
4. WifiManager: istall with platformio lib install 567
5. Json: platformio lib install 64

todo:
mqtt password
mqtt topic
wunderground
*/


extern "C"
{
    #include "user_interface.h"
}

#define wunderground_ID "IGAUTENG211"
#define wunderground_password "coat68-pot"
bool uploadtoWU = false;


#include <FS.h>                   // filesstem: this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>

//start OTA block
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//end OTA block


//begin wifi manager block
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>
#include "WiFiManager.h"  //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
WiFiManager wifiManager;
#define WiFiManGPIO00 0  // demand WifiManager hardware captive portal (GPIO00 must go low)
bool shouldSaveConfig = false; //flag for saving data to file system

//end wifi manager block

//start http client GET block
#include <ESP8266HTTPClient.h>
//end http client GET block

// start fixed IP block
// put the following in platformio.ini: 'upload_port = 10.0.0.32'.
// IPAddress ipLocal(10, 0, 0, 32);
// IPAddress ipGateway(10, 0, 0, 2);
// IPAddress ipSubnetMask(255, 255, 255, 0);
// end fixed IP block

//start time of day block
//https://github.com/PaulStoffregen/Time
#include <timelib.h>
//my timezone is 2 hours ahead of GMT
#define TZHOURS 2.0
// Update interval in seconds
#define NTPSYNCINTERVAL 300
bool timeofDayValid;

//end time of day block

//#include "../../../../openHABsysfiles/password.h"
//define your default values here, if there are different values in config.json, they are overwritten.
//length should be max size + 1 //#define wifi_ssid "yourwifiSSID"

//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"

//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"

//#define wunderground_ID "yourweatherundergroundserverusername"
//#define wunderground_password "yourweatherundergroundserverpassword"

//start web server
WiFiServer wifiserver(80);
//end web server

WiFiClient espClient;


//start mqtt block
#include <PubSubClient.h>
PubSubClient mqttclient(espClient);
char mqtt_server[40] = "10.0.0.16";
char mqtt_user[40];
char mqtt_password[40];
char mqtt_port[6] = "1883";

//end mqtt block

//start DS18B20 sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define DS18GPIO02 2  //DS18B20 is on GPIO02
OneWire oneWire(DS18GPIO02);
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

//begin wifi manager block
//default custom static IP
char static_ip[16] = "10.0.0.32";
char static_gw[16] = "10.0.0.2";
char static_sn[16] = "255.255.255.0";
//end wifi manager block

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


bool mqttReconnect()
{
    int cnt = 0;
    bool mqttConnected = false;
    while (!mqttclient.connected() && cnt<2)
    {
      cnt++;
        Serial.print("Attempting MQTT connection...");
        // If you do not want to use a username and password, change next line to
        // if (mqttclient.connect("ESP8266Client", mqtt_user, mqtt_password))
        if (mqttclient.connect("ESP8266Client"))
        {
            Serial.println("connected");
            publishMQTT("home/DS18B20S0/alive", "DS18B20S0 online");
            mqttConnected = true;
        }
        else
        {
            Serial.print("failed, rc=");
            Serial.print(mqttclient.state());
            Serial.println(" try again in 1 seconds");
            delay(1000);
        }
    }
    return(mqttConnected);
}


////////////////////////////////////////////////////////////////////////////////
//begin wifi manager block
//callback notifying us of the need to save config
void saveConfigCallback ()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager)
{
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
}

////////////////////////////////////////////////////////////////////////////////
void setup_wifimanager(void)
{
  //Local intialization. Once its business is done, there is no need to keep it around
  wifiManager.setDebugOutput(true);

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 40);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 40);

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  IPAddress _ip,_gw,_sn;
  _ip.fromString(static_ip);
  _gw.fromString(static_gw);
  _sn.fromString(static_sn);

  wifiManager.setSTAStaticIPConfig(_ip, _gw, _sn);

  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);

  //reset saved settings
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  wifiManager.setMinimumSignalQuality();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  //wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  wifiManager.setTimeout(600);
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("DS18B20","12345678"))
  {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig)
  {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;

    json["ip"] = WiFi.localIP().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["subnet"] = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile)
    {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }


}
//end wifi manager block
////////////////////////////////////////////////////////////////////////////////
void setup_wifi()
{

    //set up the pin to be used to demand captive portal
    pinMode(WiFiManGPIO00, INPUT);

    delay(10);
    Serial.print("Connecting to WiFi network: ");
  //   Serial.println(wifi_ssid);
  //   if (0)
  //   {
  //   // We start by connecting to a WiFi network
  //   WiFi.mode(WIFI_STA);
  //   WiFi.begin(wifi_ssid, wifi_password);
  //   // start fixed IP block
  //   WiFi.config(ipLocal, ipGateway, ipSubnetMask);
  //   // end fixed IP block
  //
  //   //get the wifi up
  //   while (WiFi.status() != WL_CONNECTED)
  //   {
  //       delay(500);
  //       Serial.print(".");
  //   }
  // }
  // else
  // {
setup_wifimanager();
  // }

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
void readConfigSpiffs()
{

  //clean FS, for testing
  SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin())
  {
      if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success())
        {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_port, json["mqtt_port"]);

          if(json["ip"])
          {
            Serial.println("setting custom ip from config");
            //static_ip = json["ip"];
            strcpy(static_ip, json["ip"]);
            strcpy(static_gw, json["gateway"]);
            strcpy(static_sn, json["subnet"]);
            //strcat(static_ip, json["ip"]);
            //static_gw = json["gateway"];
            //static_sn = json["subnet"];
            Serial.println(static_ip);
/*            Serial.println("converting ip");
            IPAddress ip = ipFromCharArray(static_ip);
            Serial.println(ip);*/
          } else
          {
            Serial.println("no custom ip in config");
          }
        } else
        {
          Serial.println("failed to load json config");
        }
      }
    }
  } else
  {
    Serial.println("failed to mount FS");
  }
  //end read

}


////////////////////////////////////////////////////////////////////////////////
void setup()
{
    Serial.begin(115200);
    Serial.println("");
    Serial.println("-------------------------------");
    Serial.println("ESP8266 with WiFiManager and DS18B20 sensors with MQTT notification");

    //for testing: force the captive portal
    //testWifiManPortalRequested();

    readConfigSpiffs();

    Serial.println("After reading in from config file:");
    Serial.println(static_ip);
    Serial.println(mqtt_server);
    Serial.println(mqtt_port);
    Serial.println(mqtt_user);
    Serial.println(mqtt_password);

    setup_wifi();

    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.gatewayIP());
    Serial.println(WiFi.subnetMask());


    //set up mqtt and register the callback to subscribe
    Serial.print("Setting up MQTT to server """);
    Serial.print(mqtt_server);
    Serial.print(""" on port """);
    Serial.print(mqtt_port);
    Serial.print("""");

    mqttclient.setServer(mqtt_server, String(mqtt_port).toInt());
    //if (!mqttclient.connected()) {  mqttReconnect(); }

    //start DS18B20 sensor
    pinMode(DS18GPIO02, INPUT);
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

/////////////////////////////////////////////////////////////////////////////////
void testWifiManPortalRequested()
{

    //WiFiManager Local init. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // reset settings - for testing
    //wifiManager.resetSettings();
    //SPIFFS.format();
    // end reset settings - for testing

    //sets timeout until configuration portal gets turned off
    //useful to make it all retry or go to sleep. in seconds
    //wifiManager.setTimeout(120);

    //it starts an access point with the specified name "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration

    //WITHOUT THIS THE AP DOES NOT SEEM TO WORK PROPERLY WITH SDK 1.5 , update to at least 1.5.1
    //WiFi.mode(WIFI_STA);

    if (!wifiManager.startConfigPortal("OnDemandAP")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(5000);
    }

    //if you get here you have connected to the WiFi
    Serial.println("Connected and captive portal should be online now)");

}
////////////////////////////////////////////////////////////////////////////////
void loop()
{
  // is configuration portal requested?
  if ( digitalRead(WiFiManGPIO00) == LOW ) {
    testWifiManPortalRequested();
  }

    //start OTA block
    ArduinoOTA.handle();
    //end OTA block

    if (!mqttclient.connected())
    {
      if (mqttReconnect())
      {
        mqttclient.loop();
      }
      }

    float temperature;
    temperature = 0. ;
    char nowStr[120] ;
    time_t timenow = now();
    strcpy(nowStr, strDateTime(timenow).c_str()); // get rid of newline
    nowStr[strlen(nowStr)-1] = '\0';

    // timer interrupt woke us up again
    if (aliveTick == true )
    {

      // track the free heap space
      Serial.print("Free heap on ESP8266:");
      int heapSize = ESP.getFreeHeap();
      Serial.println(heapSize, DEC);

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
            String strM = String("home/DS18B20/temperatureC")+sls+stringAddress(ds18b20Addr[i]);
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
        //https://github.com/esp8266/Arduino/blob/master/libraries/ESP8266HTTPClient/src/ESP8266HTTPClient.h

        // Initialize and make HTTP request
        HTTPClient httpclient;
        //build the string to send to the server

        //example string, all the following on one line
        //http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php
        //?ID=KCASANFR5&PASSWORD=XXXXXX&dateutc=2000-01-01+10%3A32%3A35&winddir=230&windspeedmph=12
        //&windgustmph=12&tempf=70&rainin=0&baromin=29.1&dewptf=68.2&humidity=90&weather=&clouds=
        //&softwaretype=vws%20versionxx&action=updateraw

        //assign dummy values for ourWeather sensor results
        //these vars are used here temporary for Weather Undergound upload testing
        // begin this block must be removed
        float BMP180_Temperature = 32.+1.8*temperature;
        float BMP180_Pressure = 0.02953*1015;
        float AM2315_Temperature = 32.+1.8*10.;
        float AM2315_Humidity = 65.;
        float currentWindSpeed = 0.621371 * 10.;
        float currentWindGust = 0.621371 * 15.;
        float currentWindDirection = 125.;
        float rainTotal = 0.0254 * 42.4;
        int uvIndex = 3;
        // end this block must be removed

        time_t nowutc = timenow - TZHOURS * 3600;

        //Weather underground website
        String str = "http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php";
        //compulsory data
        str = str + "?ID="+String(wunderground_ID);
        str = str + "&PASSWORD="+String(wunderground_password);
        str = str + "&dateutc="+strDateTimeURL(nowutc);
        str = str + "&action=updateraw";

        //these are not compulsory, but obviously some should be present!
        str = str + "&winddir="+String(currentWindDirection);   // - [0-360 instantaneous wind direction]
        str = str + "&windspeedmph="+String(currentWindSpeed);  // - [mph instantaneous wind speed]
        str = str + "&windgustmph="+String(currentWindGust);   // - [mph current wind gust, using software specific time period]
        // str = str + "&windgustdir="+String(xx);   //- [0-360 using software specific time period]
        // str = str + "&windspdmph_avg2m="+String(xx);    // - [mph 2 minute average wind speed mph]
        // str = str + "&winddir_avg2m="+String(xx);    // - [0-360 2 minute average wind direction]
        // str = str + "&windgustmph_10m="+String(x);   // - [mph past 10 minutes wind gust mph ]
        // str = str + "&windgustdir_10m="+String(xx);    // - [0-360 past 10 minutes wind gust direction]
        str = str + "&humidity="+String(AM2315_Humidity);    // - [% outdoor humidity 0-100%]
        // str = str + "&dewptf="+String(x);     // - [F outdoor dewpoint F]
        str = str + "&tempf="+String(AM2315_Temperature);   // - [F outdoor temperature] * for extra outdoor sensors use temp2f, temp3f, and so on
        // str = str + "&rainin="+String(x);    // - [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
        str = str + "&dailyrainin="+String(rainTotal);   //- [rain inches so far today in local time]
        str = str + "&baromin="+String(BMP180_Pressure);    // - [barometric pressure inches]
        // str = str + "&solarradiation="+String(xx);     //- [W/m^2]
        str = str + "&UV="+String(uvIndex);   //- [index]
        str = str + "&indoortempf="+String(BMP180_Temperature);    //- [F indoor temperature F]
        // str = str + "&indoorhumidity="+String(x);    //- [% indoor humidity 0-100]

        str = str + "&softwaretype=testing%20version00";

        if (timeofDayValid && uploadtoWU)
       {
         Serial.println(str);
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
