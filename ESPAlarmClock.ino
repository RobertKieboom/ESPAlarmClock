#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Adafruit_GFX.h"
#include "Adafruit_LEDBackpack.h"
#include <RtcDS3231.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "Time.h"
#include "TimeAlarms.h"

const char ssid[] = "kibi74";               // Network SSID (name)
const char password[] = "130872tim250969";  // Network password
const char* mqtt_server = "192.168.2.8";    // MQTT Server
#define DEVICE_ID "DEV001"                  // The ID of this device

const char* mqtt_observations = "/observations/" DEVICE_ID; // Channel for sensor observations
const char* mqtt_reqTime = "/time/request/" DEVICE_ID;      // Channel for requesting the time from the server
const char* mqtt_respTime = "/time/response/" DEVICE_ID;    // Channel for the server time response

WiFiClient wifiClient;
PubSubClient client(wifiClient);
Adafruit_BME280 bme;
Adafruit_7segment display = Adafruit_7segment();
RtcDS3231 Rtc;


void setup() {
  Serial.begin(115200);
  Serial.println("Clock starting!");

  display.begin(0x70);
  Rtc.Begin();
  setTimeFromRtc();
  showTime();

  Alarm.timerRepeat(1, showTime);

  if (!bme.begin()) 
    Serial.println("Could not find a valid BME280 sensor, check wiring!");

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);

  Alarm.timerRepeat(1, processMQTT);
  Alarm.timerRepeat(30, sendSensorValues);
  Alarm.timerRepeat(300, syncTime);
}

void loop() {
  wait(1000);
}

void setTimeFromRtc() {
  RtcDateTime dt = Rtc.GetDateTime();
  setTime(dt.Hour(), dt.Minute(), dt.Second(), dt.Day(), dt.Month(), dt.Year());  
}

void showTime() {
  RtcDateTime now = Rtc.GetDateTime();
  displayDateTime(display, now);
}

void syncTime() {
  if (client.connected()) {
    Serial.print("Sync Time Request "); Serial.println(mqtt_reqTime);
    client.publish(mqtt_reqTime, "");
  }  
}

void sendSensorValues() {
  char number[15];  
  char msg[80] = "";

  strcat(msg,"{\"DT\":\"");
  printDateTime(msg+strlen(msg), Rtc.GetDateTime());
  strcat(msg,"\",\"T\":");
  strcat(msg,dtostrf(bme.readTemperature(), 0, 2, number));
  strcat(msg,",\"P\":");
  strcat(msg,dtostrf(bme.readPressure(), 0, 2, number));
  strcat(msg,",\"H\":");
  strcat(msg,dtostrf(bme.readHumidity(), 0, 2, number));
  strcat(msg,"}");

  if (client.connected()) {
    Serial.print("Publish "); Serial.print(mqtt_observations); Serial.print(": "); Serial.println(msg);
    client.publish(mqtt_observations, msg);
  }
}

void processMQTT() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();  
}

void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    wait(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  if(strcmp(topic, mqtt_respTime) == 0) {
    const char* tm = (const char*) payload;
    int year = atoi(tm);
    int month = atoi(tm + 5);
    int day = atoi(tm + 8);
    int hour = atoi(tm + 11);
    int minute = atoi(tm + 14);
    int second = atoi(tm + 17);
    RtcDateTime dt(year, month, day, hour, minute, second);
    Rtc.SetDateTime(dt);
    setTimeFromRtc();
    Serial.println("Time Sync done");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      client.subscribe(mqtt_respTime);
      syncTime();
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}

void displayDateTime(Adafruit_7segment& display, const RtcDateTime& dt) {
  int hour = dt.Hour();
  int firstDigit = hour/10;
  if(firstDigit == 0)
    display.writeDigitRaw(0, 0x00);
  else
    display.writeDigitNum(0, firstDigit, false);
  display.writeDigitNum(1, hour%10, false);

  int minute = dt.Minute();
  display.writeDigitNum(3, minute/10, false);
  bool isConnected = client.connected();
  display.writeDigitNum(4, minute%10, isConnected);

  int second = dt.Second();
  display.drawColon((second % 2) == 0);

  display.writeDisplay();
}

void printDateTime(char *dateString, const RtcDateTime& dt) {
  sprintf_P(dateString, 
      PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
      dt.Year(), dt.Month(), dt.Day(),
      dt.Hour(), dt.Minute(), dt.Second() );
}

void wait(unsigned long ms) {
  unsigned long start = millis();
  while(millis() < start + ms) {
    Alarm.delay(1);
    delay(1);
  }
}

