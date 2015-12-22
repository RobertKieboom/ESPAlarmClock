#include <Wire.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "Adafruit_GFX.h"
#include "Adafruit_LEDBackpack.h"
#include <RtcDS3231.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

char ssid[] = "KIBI";                 // Network SSID (name)
char pass[] = "welkom123";            // Network password

unsigned int localPort = 2390;        // local port to listen for UDP NTP packets
IPAddress timeServerIP;               // time.nist.gov NTP server address
const char* ntpServerName = "time.nist.gov";
const int NTP_PACKET_SIZE = 48;       // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE];  // buffer to hold incoming and outgoing packets
WiFiUDP udp;
int hourNTPSynced = -1;

Adafruit_BME280 bme;
Adafruit_7segment display = Adafruit_7segment();
RtcDS3231 Rtc;

void setup() {
  Serial.begin(115200);
  Serial.println("Clock starting!");

  display.begin(0x70);
  showEmptyDisplay();
  Rtc.Begin();

  if (!bme.begin()) {  
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }

  Serial.print("Connecting to "); Serial.println(ssid);
  WiFi.begin(ssid, pass);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  udp.begin(localPort);
  Serial.print("UDP local port: ");
  Serial.println(udp.localPort());
}

void loop() {
  WiFi.hostByName(ntpServerName, timeServerIP); 

  if(hourNTPSynced != Rtc.GetDateTime().Hour()) {
    Serial.println("Sync Time");
    sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  }
  delay(1000);

  unsigned long epoch = parseNTPPacket();
  if(epoch > 0) {
    RtcDateTime now(epoch + 3600);
    Serial.println("Setting RTC");
    printDateTime(now);
    Rtc.SetDateTime(now);
    hourNTPSynced = now.Hour();
  }

  RtcDateTime now = Rtc.GetDateTime();
  displayDateTime(display, now);

  Serial.print("Temperature = ");
  Serial.print(bme.readTemperature());
  Serial.println(" *C");
  
  Serial.print("Pressure = ");
  Serial.print(bme.readPressure());
  Serial.println(" Pa");
  
  Serial.print("Humidity = ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");
  
  Serial.println();
}

unsigned long parseNTPPacket() {
  unsigned long epoch = 0;
  int cb = udp.parsePacket();
  if (cb) {
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    epoch = secsSince1900 - seventyYears;
  }
  return epoch;
}

void showEmptyDisplay() {
  for(int i = 0; i < 4; i++)
    display.writeDigitRaw(i, 0x00);
  display.drawColon(true);
  display.writeDisplay();
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
  display.writeDigitNum(4, minute%10, false);

  int second = dt.Second();
  display.drawColon((second % 2) == 0);

  display.writeDisplay();
}

void printDateTime(const RtcDateTime& dt) {
  char datestring[25];
  sprintf_P(datestring, 
      PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
      dt.Month(), dt.Day(), dt.Year(),
      dt.Hour(), dt.Minute(), dt.Second() );
  Serial.println(datestring);
}

unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

