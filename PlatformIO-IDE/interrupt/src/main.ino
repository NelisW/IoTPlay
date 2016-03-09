//
// ESP8266 Timer Example
// SwitchDoc Labs  October 2015
//


//install PubSubClient from http://platformio.org/#!/lib
//download the tar file and untar into the project lib folder


//mosquitto_sub -v -d -t "testing/mqtttest"

extern "C" {
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

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.print("Connecting to WiFi network: ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // If you do not want to use a username and password, change next line to
    if (client.connect("ESP8266Client")) {
    ///if (client.connect("ESP8266Client", mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

os_timer_t myTimer;

bool tickOccured;

// start of timerCallback
void timerCallback(void *pArg)
{
      tickOccured = true;
} // End of timerCallback

void user_init(void)
{
//   os_timer_setfn - Define a function to be called when the timer fires
//
// void os_timer_setfn(
//       os_timer_t *pTimer,
//       os_timer_func_t *pFunction,
//       void *pArg)
//
// Define the callback function that will be called when the timer reaches zero. The pTimer parameters is a pointer to the timer control structure.
//
// The pFunction parameters is a pointer to the callback function.
//
// The pArg parameter is a value that will be passed into the called back function. The callback function should have the signature:
// void (*functionName)(void *pArg)
//
// The pArg parameter is the value registered with the callback function.

      os_timer_setfn(&myTimer, timerCallback, NULL);

//       os_timer_arm -  Enable a millisecond granularity timer.
//
// void os_timer_arm(
//       os_timer_t *pTimer,
//       uint32_t milliseconds,
//       bool repeat)
//
// Arm a timer such that is starts ticking and fires when the clock reaches zero.
//
// The pTimer parameter is a pointed to a timer control structure.
// The milliseconds parameter is the duration of the timer measured in milliseconds. The repeat parameter is whether or not the timer will restart once it has reached zero.

      os_timer_arm(&myTimer, 1000, true);
}


void setup() {

 Serial.begin(115200);
 Serial.println();
 Serial.println();

 Serial.println("");
 Serial.println("--------------------------");
 Serial.println("ESP8266 Timer Test");
 Serial.println("--------------------------");

 setup_wifi();
 client.setServer(mqtt_server, 1883);

 tickOccured = false;
 user_init();
}

void loop() {

 if (tickOccured == true)
 {
    if (! client.publish("testing/mqtttest", "yes"))
    {
      Serial.println("Publish failed");
    };
    Serial.println("Tick Occurred");
    tickOccured = false;
 }

yield();  // or delay(0);
}
