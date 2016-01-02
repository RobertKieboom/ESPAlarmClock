#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include "Adafruit_GFX.h"
#include "Adafruit_LEDBackpack.h"
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include "Time.h"
#include "TimeAlarms.h"
#include "DS1307RTC.h"

const char ssid[] = "kibi74";               // Network SSID (name)
const char password[] = "130872tim250969";  // Network password
const char* mqtt_server = "192.168.2.8";    // MQTT Server
#define DEVICE_ID "DEV001"                  // The ID of this device

const char* mqtt_observations = "/observations/" DEVICE_ID;   // Channel for sensor observations
const char* mqtt_reqTime = "/time/request/" DEVICE_ID;   // Channel for requesting the time from the server
const char* mqtt_respTime = "/time/response/" DEVICE_ID; // Channel for the server time response

WiFiClient wifiClient;
PubSubClient client(wifiClient);
Adafruit_BME280 bme;
Adafruit_7segment display = Adafruit_7segment();
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);

void setup() {
  Serial.begin(115200);
  Serial.println("Clock starting!");

  display.begin(0x70);
  setTimeFromRtc();
  showTime();

  if (!bme.begin()) 
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  if (!tsl.begin()) 
    Serial.println("Could not find a valid TSL2561 sensor, check wiring!");
  tsl.enableAutoRange(true);            /* Auto-gain ... switches automatically between 1x and 16x */

  Alarm.timerRepeat(1, showTime);

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
  tmElements_t tm;
  breakTime(RTC.get(), tm);
  setTime(tm.Hour, tm.Minute, tm.Second, tm.Day, tm.Month, tm.Year);  
}

void showTime() {
  adjustBrightness();
  displayDateTime(display, RTC.get());
}

void adjustBrightness() {
  sensors_event_t event;
  tsl.getEvent(&event);
  float light = event.light; // 3 - 1500
  int brightness = map(light, 0, 1500, 0, 15);
  display.setBrightness(brightness);
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
  printDateTime(msg+strlen(msg), RTC.get());
  strcat(msg,"\",\"T\":");
  strcat(msg,dtostrf(bme.readTemperature(), 0, 2, number));
  strcat(msg,",\"P\":");
  strcat(msg,dtostrf(bme.readPressure(), 0, 2, number));
  strcat(msg,",\"H\":");
  strcat(msg,dtostrf(bme.readHumidity(), 0, 2, number));
  strcat(msg,",\"L\":");
  sensors_event_t event;
  tsl.getEvent(&event);
  strcat(msg,dtostrf(event.light, 0, 2, number));
  strcat(msg,"}");

  if (client.connected()) {
    Serial.print("Publish "); Serial.print(mqtt_observations); Serial.print(": "); Serial.println(msg);
    client.publish(mqtt_observations, msg);
  }
}

void processMQTT() {
  if (!client.connected())
    reconnect();
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
    tmElements_t tmElements;
    tmElements.Year = atoi(tm) - 1970;
    tmElements.Month = atoi(tm + 5);
    tmElements.Day = atoi(tm + 8);
    tmElements.Hour = atoi(tm + 11);
    tmElements.Minute = atoi(tm + 14);
    tmElements.Second = atoi(tm + 17);    
    time_t t = makeTime(tmElements);
    RTC.set(t);
    setTimeFromRtc();
    Serial.print("Time Sync done - ");
    char s[25];
    printDateTime(s, t);
    Serial.println(s);
  }
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP8266Client")) {
      client.subscribe(mqtt_respTime);
      syncTime();
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      wait(1000);
    }
  }
}

void displayDateTime(Adafruit_7segment& display, time_t t) {
  tmElements_t tm;
  breakTime(t, tm);

  int hour = tm.Hour;
  int firstDigit = hour/10;
  if(firstDigit == 0)
    display.writeDigitRaw(0, 0x00);
  else
    display.writeDigitNum(0, firstDigit, false);
  display.writeDigitNum(1, hour%10, false);

  int minute = tm.Minute;
  display.writeDigitNum(3, minute/10, false);
  bool isConnected = client.connected();
  display.writeDigitNum(4, minute%10, isConnected);

  int second = tm.Second;
  display.drawColon((second % 2) == 0);

  display.writeDisplay();
}

void printDateTime(char *dateString, time_t dt) {
  tmElements_t tm;
  breakTime(dt, tm);
  sprintf_P(dateString, 
      PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
      tm.Year + 1970, tm.Month, tm.Day,
      tm.Hour, tm.Minute, tm.Second );
}

void printDateTime(char *dateString, tmElements_t tm) {
  sprintf_P(dateString, 
      PSTR("%04u/%02u/%02u %02u:%02u:%02u"),
      tm.Year, tm.Month, tm.Day,
      tm.Hour, tm.Minute, tm.Second );
}

void wait(unsigned long ms) {
  unsigned long start = millis();
  while(millis() < start + ms) {
    Alarm.delay(1);
    delay(1);
  }
}

