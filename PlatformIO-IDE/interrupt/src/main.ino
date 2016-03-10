//
// ESP8266 Timer Example
// SwitchDoc Labs  October 2015
//


//install PubSubClient from http://platformio.org/#!/lib
//download the tar file and untar into the project lib folder


//mosquitto_sub -v -d -t "testing/mqtttest"
//mosquitto_sub -v -d -t "testing/+"

extern "C" {
#include "user_interface.h"
}

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

//#include "DHT.h"
//#define DHTPIN 13     // what digital pin we're connected to
// Uncomment whatever type you're using!
//#define DHTTYPE DHT11   // DHT 11
//#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)
//DHT dht(DHTPIN, DHTTYPE);

#include "../../../../openHABsysfiles/password.h"
//#define wifi_ssid "yourwifiSSID"
//#define wifi_password "yourwifipassword"
//#define mqtt_server "yourmqttserverIP"
//#define mqtt_user "yourmqttserverusername"
//#define mqtt_password "yourmqttserverpassword"

WiFiClient espClient;
PubSubClient client(espClient);
int counter = 0;
int previousReading = LOW;
os_timer_t myTimer;
bool tickOccured;

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
      // Once connected, publish an announcement...
      client.publish("testing/mqtttest", "hello world");
      // ... and resubscribe
      client.subscribe("testing/espinput");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


// start of timerCallback
void timerCallback(void *pArg)
{
      tickOccured = true;
}

void user_init(void)
{
// Initialize the BUILTIN_LED pin as an output
  pinMode(BUILTIN_LED, OUTPUT);
  //PIR is on GPIO12, or D6 on the NodeMCU
  pinMode(12, INPUT);

    //Define a function to be called when the timer fires
    os_timer_setfn(&myTimer, timerCallback, NULL);
    // Enable a millisecond granularity timer.
    os_timer_arm(&myTimer, 1000, true);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}

void setup() {

 Serial.begin(115200);
 Serial.println("");
 Serial.println("--------------------------");
 Serial.println("ESP8266 Timer Test");
 Serial.println("--------------------------");

 setup_wifi();
 client.setServer(mqtt_server, 1883);
 client.setCallback(mqttCallback);

 tickOccured = false;
 user_init();

 //dht.begin();

}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  /*long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
    // Read humidity and temperature as Celsius (the default)
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Check if any reads failed and exit early (to try again).
    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }
    char t2[50];
    char h2[50];
    dtostrf(t, 5, 2, t2);
    dtostrf(h, 5, 2, h2);
    client.publish("/sensor/temp", t2 );
    client.publish("/sensor/humidity", h2 );
  }*/


 if (tickOccured == true)
 {
    tickOccured = false;
    if (! client.publish("testing/mqtttest", "yes!! Wow!!"))
    {
      Serial.println("Publish failed");
    };
    Serial.println("Tick Occurred");


    int reading = digitalRead(12);
    Serial.println(reading);
    Serial.println(previousReading);
    Serial.println(" ");
    if (previousReading == LOW && reading == HIGH) {
      Serial.println("*** ");
    counter++;
    client.publish("testing/movement", "Motion!");
    Serial.print("PIR ");
    Serial.print(counter);
    //Serial.print("x Times! ");

 }
 previousReading = reading;

}

yield();  // or delay(0);
}
