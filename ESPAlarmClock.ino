#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_TSL2561_U.h>
#include <Time.h>
#include <TimeAlarms.h>
#include <DS1307RTC.h>
#include <ArduinoJson.h>

// TODO
// >- Implement alarm times, switch on the sound
// - Have a button (or proximity sensor?) to stop the alarm
// - Create prototype with perfboard and casing
// - Have a RPI as the home server with Mosquitto, Node-Red and mongoDB
// - Store the alarm info on SPIFFS "disk"
// - Store the Wifi info for all supported AP's on SPIFFS "disk"
// - When the WIFI router goes down, handle reconnecting to the AP
//
// - Have a touch LCD (nextion) display? Or a big 7-segment?
// - Have a way to enter wake time for next day (rotary encoder?)
// - When alarm is near, grow the light to maximum
// - Send also installed sensors metadata on connection
// - Logging, where the log is a queue and errors/warnings are sent over MQTT
// - Have a server socket for logging, including traces
// - Check if the BME280 gives correct temperature/humidity readings [try other libraries?]
// - More precise RTC? [DS3231]
// - Check if light is really a Lux value
// - Simple discovery and join network/server scenario
// - How do we solve the fact that the server can be on DHCP; changing server ip addresses?
// - FOTA
//
// * Send thing metadata to server (ID, version software, type of hardware) on connection
// * Have a version number in the software
// * Allow multiple WIFI SSID/PWD/ServerIP [scanNetworks method didn't work!]
// * Signal quality can reach 126, should be limited to 100
// * Include signal quality in sensor readings
// * Include battery voltage in sensor readings

struct WifiAp {
  char* ssid;
  char* password;
  char* server;
};

struct WifiAp wifis[] = {
  { "KIBI", "welkom123", "192.168.1.102" },
  { "kibi74", "130872tim250969", "192.168.2.3" }
};

char mqtt_observations[24] = "/observations/";  // Channel for sensor observations
char mqtt_metadata[22] = "/metadata/";          // Channel for device metadata
char mqtt_reqTime[26] = "/time/request/";       // Channel for requesting the time from the server
char mqtt_respTime[26] = "/time/response/";     // Channel for the server time response
char mqtt_reqAlarm[26] = "/alarm/request/";     // Channel for requesting the alarms from the server
char mqtt_respAlarm[26] = "/alarm/response/";   // Channel for the server alarms response

WiFiClient wifiClient;
PubSubClient client(wifiClient);
Adafruit_BME280 bme;
Adafruit_7segment display = Adafruit_7segment();
Adafruit_TSL2561_Unified tsl = Adafruit_TSL2561_Unified(TSL2561_ADDR_FLOAT, 12345);
ADC_MODE(ADC_VCC);

#define ALARM_PIN 13
#define SOFTWARE_VERSION "0.1.0"

void setup() {
  Serial.begin(115200);
  Serial.println("\nClock starting!");

  pinMode(ALARM_PIN, OUTPUT);
  digitalWrite(ALARM_PIN, LOW);
  setupMQTT();

  initDisplay();
  initTime();
  showTime();
  Alarm.timerRepeat(1, showTime);

  if (!RTC.chipPresent())
    Serial.println("Could not find a valid DS1307 Realtime Clock, check wiring!");
  if (!bme.begin()) 
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
  if (!tsl.begin()) 
    Serial.println("Could not find a valid TSL2561 sensor, check wiring!");
  tsl.enableAutoRange(true);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  struct WifiAp* ap = setup_wifi();
  if(ap != NULL) {
    client.setServer(ap->server, 1883);
    client.setCallback(mqtt_callback);
  }

  Alarm.timerRepeat(1, processMQTT);
  Alarm.timerRepeat(30, sendSensorValues);
  Alarm.timerRepeat(30, checkAlarm);
  Alarm.timerRepeat(300, requestTimeFromServer);
}

void loop() {
  wait(1000);
}


// ======================================================
// Time
// ======================================================

void initTime() {
  setSyncProvider(getRtc);
  setTimeFromRtc();
}

time_t getRtc() {
  if(RTC.chipPresent())
    return RTC.get();
  return (time_t)0;
}

void setRtc(time_t t) {
  setTime(t);
  if(RTC.chipPresent()) {
    RTC.set(t);
  }
}

void setTimeFromRtc() {
  if(RTC.chipPresent()) {
    tmElements_t tm;
    breakTime(RTC.get(), tm);
    setTime(tm.Hour, tm.Minute, tm.Second, tm.Day, tm.Month, tm.Year);
  }
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

void checkAlarm() {
  int wd = weekday();

  if(wd > 1 && wd < 7) {
    if(hour() == 6 && minute() == 45) {
      digitalWrite(ALARM_PIN, 1);
    }
  }
}


// ======================================================
// Display
// ======================================================

void initDisplay() {
  display.begin(0x70);
}

void showTime() {
  adjustBrightness();
  displayDateTime(display, now());
}

void adjustBrightness() {
  sensors_event_t event;
  tsl.getEvent(&event);
  float light = event.light; // 3 - 2000 LUX
  int brightness = map(light, 0, 1500, 0, 15);
  display.setBrightness(brightness);
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


// ======================================================
// MQTT
// ======================================================

void setupMQTT() {
  char* deviceId = getDeviceId();
  strcat(mqtt_observations, deviceId);
  strcat(mqtt_reqTime, deviceId);
  strcat(mqtt_respTime, deviceId);
  strcat(mqtt_reqAlarm, deviceId);
  strcat(mqtt_respAlarm, deviceId);  
  strcat(mqtt_metadata, deviceId);
}

void sendSensorValues() {
  char number[15];  
  char msg[75];

  strcpy(msg,"{\"DT\":\"");
  printDateTime(msg+strlen(msg), now());
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
  sendObservation(msg);

  strcpy(msg,"{\"DT\":\"");
  printDateTime(msg+strlen(msg), now());
  strcat(msg,"\",\"HF\":");
  uint32_t free = ESP.getFreeHeap();
  strcat(msg,itoa(free, number, 10));
  strcat(msg,",\"SQ\":");
  uint8_t quality = getSignalQuality();
  strcat(msg,itoa(quality, number, 10));
  double vcc = ESP.getVcc() / 1000.0;
  strcat(msg,",\"VC\":");
  strcat(msg,dtostrf(vcc, 0, 2, number));
  strcat(msg,"}");
  sendObservation(msg);
}

void sendObservation(char* message) {
  if (client.connected()) {
    Serial.print("Publish "); Serial.print(mqtt_observations); Serial.print(": "); Serial.println(message);
    client.publish(mqtt_observations, message);
  }  
}

uint8_t getSignalQuality() {
  uint32_t quality = 2 * (WiFi.RSSI() + 100); // rssi in dBm: [-100 to -50]
  return quality < 0 ? 0 : quality > 100 ? 100 : quality;
}

void processMQTT() {
  if (!client.connected())
    reconnect();
  client.loop();
}

void handleTimeResponse(byte* payload) {
    const char* tm = (const char*) payload;
    tmElements_t tmElements;
    tmElements.Year = atoi(tm) - 1970;
    tmElements.Month = atoi(tm + 5);
    tmElements.Day = atoi(tm + 8);
    tmElements.Hour = atoi(tm + 11);
    tmElements.Minute = atoi(tm + 14);
    tmElements.Second = atoi(tm + 17);    
    time_t t = makeTime(tmElements);
    setRtc(t);
    setTimeFromRtc();
    Serial.print("Time Sync done - ");
    char s[25];
    printDateTime(s, t);
    Serial.println(s);  
}

void handleAlarmResponse(byte* payload, unsigned int length) {
  /*
  {start: "", end: "", weekday: "MO", time: "07:00:00Z"}
  {start: "2015-11-30", end: "2015-12-10", time: ""}
  {date: "2015-12-30", time: "10:30:00Z"}
  */
  payload[length] = 0;
  Serial.print("JSON parse "); Serial.println((const char*)payload);

  StaticJsonBuffer<256> jsonBuffer;
  JsonObject& alarm = jsonBuffer.parseObject((const char*) payload);

  if(!alarm.success()) {
    Serial.println("Error parsing JSON!");
  }
  else {
    for (JsonObject::iterator it=alarm.begin(); it!=alarm.end(); ++it) {
      Serial.print(it->key);
      Serial.print(": ");
      Serial.println(it->value.asString());
    }
  }
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("MQTT Incoming - "); Serial.println(topic);

  if(strcmp(topic, mqtt_respTime) == 0) {
    handleTimeResponse(payload);
  }
  else if(strcmp(topic, mqtt_respAlarm) == 0) {
    handleAlarmResponse(payload, length);
  }
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("Attempting MQTT connection... ");
    if (client.connect(getDeviceId())) {
      Serial.println("connected");

      requestTimeFromServer();
      requestAlarmTimesFromServer();
      sendMetadata();
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      wait(1000);
    }
  }
}

void requestTimeFromServer() {
  if (client.connected()) {
    client.subscribe(mqtt_respTime);
    Serial.print("Sync Time Request "); Serial.println(mqtt_reqTime);
    client.publish(mqtt_reqTime, "");
  }  
}

void requestAlarmTimesFromServer() {
  if (client.connected()) {
    client.subscribe(mqtt_respAlarm);
    Serial.print("Request Alarm Times "); Serial.println(mqtt_reqAlarm);
    client.publish(mqtt_reqAlarm, "");
  }
}

void sendMetadata() {
  char msg[128] = "";
  char tmp[10];

  strcat(msg,"{\"SDK\":\"");
  strcat(msg, ESP.getSdkVersion());
  strcat(msg, "\",\"BVER\":");
  itoa(ESP.getBootVersion(), tmp, 10);
  strcat(msg, tmp);
  strcat(msg, ",\"BMODE\":");
  itoa(ESP.getBootMode(), tmp, 10);
  strcat(msg, tmp);
  strcat(msg, ",\"FLSH\":");
  itoa(ESP.getFlashChipRealSize()/1024, tmp, 10);
  strcat(msg, tmp);
  strcat(msg, ",\"SKTCH\":");
  itoa(ESP.getSketchSize()/1024, tmp, 10);
  strcat(msg, tmp);
  strcat(msg, ",\"SKFREE\":");
  itoa(ESP.getFreeSketchSpace()/1024, tmp, 10);
  strcat(msg, tmp);
  strcat(msg, ",\"VER\":\"");
  strcat(msg, SOFTWARE_VERSION);
  strcat(msg, "\"}");

  if (client.connected()) {
    Serial.print("Publish "); Serial.print(mqtt_metadata); Serial.print(": "); Serial.println(msg);
    client.publish(mqtt_metadata, msg);
  }
}


// ======================================================
// ESP
// ======================================================

static char deviceId[10] = "";

char* getDeviceId() {
  if(deviceId[0] == '\0') {
    sprintf(deviceId, "%08X", ESP.getChipId());
  }
  return deviceId;
}

struct WifiAp* setup_wifi() {
  for(int i = 0; i < sizeof(wifis)/sizeof(wifis[0]); i++) {
    struct WifiAp* ap = &(wifis[i]);
    Serial.println();
    Serial.print("Connecting to ");
    Serial.print(ap->ssid);
  
    WiFi.begin(ap->ssid, ap->password);

    for(int j = 0; j < 25; j++ ) {
      if(WiFi.status() == WL_CONNECTED)
        break;
      wait(500);
      Serial.print(".");
    }

    if(WiFi.status() == WL_CONNECTED) {
      Serial.println();
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      return ap;
    }
  }
  return NULL;
}

void wait(unsigned long ms) {
  unsigned long start = millis();
  while(millis() < start + ms) {
    Alarm.delay(1);
    delay(1);
  }
}

