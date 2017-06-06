#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

#define NETWORK_DR
#ifdef NETWORK_DR
const char* ssid = "datarespons";
const char* password = "DRwireless";
#endif

ESP8266WiFiMulti WiFiMulti;
HTTPClient http;
WiFiUDP UDP;                     // Create an instance of the WiFiUDP class to send and receive
IPAddress timeServerIP;
const char* NTPServerName = "no.pool.ntp.org";
const int NTP_PACKET_SIZE = 48;  // NTP time stamp is in the first 48 bytes of the message
byte NTPBuffer[NTP_PACKET_SIZE]; // buffer to hold incoming and outgoing packets

// ntp
unsigned long intervalNTP = 60000; // Request NTP time every minute
unsigned long prevNTP = 0;
unsigned long lastNTPResponse = 0;
uint32_t timeUNIX = 0;
unsigned long prevActualTime = 0;
unsigned long currentMillis = 0;

// current time
volatile int m_currentHour   = 0;
volatile int m_currentMinute = 0;
volatile int m_everyOtherMinute = 0;

void sendNTPpacket()
{
    memset(NTPBuffer, 0, NTP_PACKET_SIZE);  // set all bytes in the buffer to 0
    // Initialize values needed to form NTP request
    NTPBuffer[0] = 0b11100011;   // LI, Version, Mode
    // send a packet requesting a timestamp:
    UDP.beginPacket(timeServerIP, 123); // NTP requests are to port 123
    UDP.write(NTPBuffer, NTP_PACKET_SIZE);
    UDP.endPacket();
}

void networkSetup()
{
    WiFiMulti.addAP(ssid, password);
    UDP.begin(123);

    if(!WiFi.hostByName(NTPServerName, timeServerIP)) // Get the IP address of the NTP server
    {
        Serial.println("DNS lookup failed. Rebooting.");
        Serial.flush();
        ESP.reset();
    }
    
    Serial.print("Time server IP:\t");
    Serial.println(timeServerIP);
  
    Serial.println("\r\nSending NTP request ...");
    sendNTPpacket();  
}

void transmitDataToWeatherUnderground(String & wuAddressWithData)
{
    if((WiFiMulti.run() == WL_CONNECTED))
    {
        http.begin(wuAddressWithData, "12 DB BB 24 8E 0F 6F D4 63 EC 45 DD 5B ED 37 D7 6F B1 5F E5"); // wunderground ssl fingerprint
        int httpCode = http.GET();

        if(httpCode > 0)
        {
            if(httpCode == HTTP_CODE_OK)
            {
                String payload = http.getString();
                Serial.println(payload);
            }
        }
        else
        {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
        }

        http.end();
    }
}

void checkForNtpUpdate()
{
    currentMillis = millis();
  
    if (currentMillis - prevNTP > intervalNTP)   // If a minute has passed since last NTP request
    { 
        prevNTP = currentMillis;
        Serial.println("\r\nSending NTP request ...");
        sendNTPpacket();
    }

    uint32_t time = getTime();                   // Check if an NTP response has arrived and get the (UNIX) time

    if (time) // If a new timestamp has been received
    {
        timeUNIX = time;
        Serial.print("NTP response:\t");
        Serial.println(timeUNIX);
        lastNTPResponse = currentMillis;
    }
    else if ((currentMillis - lastNTPResponse) > 3600000)
    {
        Serial.println("More than 1 hour since last NTP response. Rebooting.");
        Serial.flush();
        ESP.reset();
    }

    //printTime();
}

uint32_t getTime()
{
    if (UDP.parsePacket() == 0) 
    { // If there's no response (yet)
      return 0;
    }
  
    UDP.read(NTPBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    // Combine the 4 timestamp bytes into one 32-bit number
    uint32_t NTPTime = (NTPBuffer[40] << 24) | (NTPBuffer[41] << 16) | (NTPBuffer[42] << 8) | NTPBuffer[43];
    // Convert NTP time to a UNIX timestamp:
    // Unix time starts on Jan 1 1970. That's 2208988800 seconds in NTP time:
    const uint32_t seventyYears = 2208988800UL;
    // subtract seventy years:
    uint32_t UNIXTime = NTPTime - seventyYears;
    return UNIXTime;
}

bool reportNewHour()
{
    int nowMinute = getMinutes(getUnixTime());
    bool timeRollover = false;

    if (nowMinute < m_currentMinute)
        timeRollover = true;
          
    m_currentMinute = nowMinute;
    return timeRollover;
}

bool reportNewDay()
{
    int nowHour = getHours(getUnixTime());
    bool timeRollover = false;

    if (nowHour < m_currentHour)
        timeRollover = true;

    m_currentHour = nowHour;
    return timeRollover;
}

bool reportEveryOtherMinute()
{
    int nowMinute = getMinutes(getUnixTime());
    bool timeRollover = false;

    if ((nowMinute % 2 == 0) && nowMinute != m_everyOtherMinute)
        timeRollover = true;

    m_everyOtherMinute = nowMinute;
    return timeRollover;
}

void printTime()
{
    uint32_t actualTime = getUnixTime();

    if (actualTime != prevActualTime && timeUNIX != 0) { // If a second has passed since last print
        prevActualTime = actualTime;
        Serial.printf("\rUTC time:\t%d:%d:%d\n", getHours(actualTime), getMinutes(actualTime), getSeconds(actualTime));
    }
}

inline uint32_t getUnixTime()
{
    currentMillis = millis();
    return (uint32_t)(timeUNIX + (currentMillis - lastNTPResponse)/1000);
}

inline int getSeconds(uint32_t UNIXTime)  { return UNIXTime % 60; }
inline int getMinutes(uint32_t UNIXTime)  { return UNIXTime / 60 % 60; }
inline int getHours(uint32_t UNIXTime)    { return UNIXTime / 3600 % 24; }
