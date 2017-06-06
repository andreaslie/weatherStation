#include <SFE_BMP180.h>
#include <Wire.h>
#include <DHT.h>
#include <DHT_U.h>
#include <math.h>

// enable / disable subsystems
const int isAnemometerActive = 1;
const int isRainmeterActive = 1;
const int isWindvaneActive = 1;
const int isDhtActive = 1;
const int isBmpActive = 1;

// direction for vane
// S, SW, W, NW, N, NE, E, SE
const int windVaneOffset = 0;

// pin definitions
const int anemometerPin     = 14;
const int precipitationPin  = 13;
const int sdaPin            = 4;
const int sclPin            = 5;
const int windvanePin       = A0;
const int redLedPin         = 0;

#define DHTPIN 12         // what digital pin we're connected to
#define DHTTYPE DHT22     // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

// timekeeping
unsigned long lastNtpCheckTime        = 0;
const unsigned long thresholdTimeNtp  = 10000; // ten sec threshold

volatile unsigned long previousWindInterruptTime = 0;
volatile unsigned long previousRainInterruptTime = 0;
volatile unsigned long previousGustResetTime = 0;

// counters used in the ISR's
volatile int windCounter = 0;
volatile int rainCounterHourly = 0;
volatile int rainCounterDaily = 0;
volatile int vaneDirection = 0;
volatile int gustMaxRecorded = 0;
volatile int gustCounter = 0;

volatile boolean redLedState  = true;

const unsigned int interruptSleep = 2;         // milliseconds delay to avoid multiple readings
const unsigned long feedbackSeconds = 60*60;   // every hour

const float windSpeed = 0.666667f;                  // 2.4 km/h == 0.667 m/s
const float windSpeedMph = 1.4913;                  // 2.4 km/h == 0.667 m/s == 1.4913 m/h
const float rainSpeedInch = 0.0011f;                // 0.2794 mm precipitation on every bucket emptying
const float rainSpeedMm   = 0.2794f;                // 0.2794 mm precipitation on every bucket emptying

const String wuBegin = "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?ID=IHORDALA133&PASSWORD=MXLVDW6X&dateutc=now";
const String wuEnd = "&action=updateraw";

const String wuTemp       = "&tempf=";         // ok
const String wuHumid      = "&humidity=";      // ok
const String wuDewpt      = "&dewptf=";        // ok
const String wuRain       = "&rainin=";        // ok
const String wuDailyRain  = "&dailyrainin=";   // ok
const String wuBaro       = "&baromin=";       // ok
const String wuBaroTemp   = "&temp2f=";        // ok
const String wuWinddir    = "&winddir=";       // ok
const String wuWindspeed  = "&windspeedmph=";  // ok
const String wuGustspeed  = "&windgustmph=";   // ok

SFE_BMP180 pressure;

#define ALTITUDE 56.0 // Fantoftneset 4

String floatToString(float & floatval, int precision)
{
    int intval = (int)floatval; // downcast
    float decimals = floatval - (float)intval;

    int power = pow(10, precision);

    decimals *= power;
    int intdecimals = (int) decimals; // downcast

    String s;
    s += intval;
    s += ".";

    for (int i = 0; i < precision - 1; ++i) // n-1 times
    {
        power /= 10;
        if (intdecimals < power)
            s += "0";
    }

    s += intdecimals;

    return s;
}

void toggleLedRed()
{
    digitalWrite(redLedPin, redLedState);
    redLedState = !redLedState;
}

void setup()
{
  Serial.begin(9600);

  if (pressure.begin())
      Serial.println("BMP180 init success");
  else
      Serial.println("BMP180 init fail\n\n");

  pinMode(redLedPin, OUTPUT);
  pinMode(redLedPin, redLedState);
  digitalWrite(anemometerPin, HIGH);
  pinMode(windvanePin, INPUT);
  pinMode(anemometerPin, INPUT_PULLUP);
  pinMode(precipitationPin, INPUT_PULLUP);
  pinMode(DHTPIN, INPUT_PULLUP);

  dht.begin();
  
  attachInterrupt(anemometerPin, windInterrupt, FALLING);
  attachInterrupt(precipitationPin, rainInterrupt, FALLING);

  toggleLedRed();
  networkSetup();
}

void transmitWeatherData()
{
    String wuString = wuBegin;

    if (isWindvaneActive == 1)
    {
        vaneDirection = mapVaneDirection();
        Serial.print("Wane Dir: ");
        Serial.println(vaneDirection);
        wuString += wuWinddir;
        wuString += vaneDirection;
    }

    if (isAnemometerActive == 1)
    {
        float windAmount = (windSpeed * (((float)windCounter) / 2.0f) / (float)feedbackSeconds);
        float windAmountMph = (windSpeedMph * (((float)windCounter) / 2.0f) / (float)feedbackSeconds);
        float windGustMph = (windSpeedMph * ((float)gustMaxRecorded) / 2.0f);

        Serial.print("WindGust: ");
        Serial.print(windGustMph);
        Serial.println(" mph");

        Serial.print("WindSpeed: ");
        Serial.print(windAmountMph);
        Serial.println(" mph");

        Serial.print("Wind: ");
        Serial.print(windAmount);
        Serial.println(" ms");

        windCounter = 0; // reset
        gustMaxRecorded    = 0; // reset

        wuString += wuWindspeed;
        wuString += floatToString(windAmountMph, 4);
        wuString += wuGustspeed;
        wuString += floatToString(windGustMph, 4);
    }

    if (isRainmeterActive == 1)
    {
        float rainAmountInch  = rainSpeedInch  * (float)rainCounterHourly;
        float rainAmountMm    = rainSpeedMm    * (float)rainCounterHourly;
        Serial.print("Rain mm: ");
        Serial.print(floatToString(rainAmountMm, 4));
        Serial.print("\tRain Inches: ");
        Serial.println(floatToString(rainAmountInch, 4));
        wuString += wuRain;
        wuString += floatToString(rainAmountInch, 4);
        rainCounterHourly = 0; // reset

        if (reportNewDay())
        {
            //printTime();
            float rainAmountInchDaily  = rainSpeedInch  * (float)rainCounterDaily;
            wuString += wuDailyRain;
            wuString += floatToString(rainAmountInchDaily, 4);
            rainCounterDaily = 0;  // reset
        }
    }

    if (isDhtActive == 1)
    {
        float humidity = dht.readHumidity();
        float tempc = dht.readTemperature();      // Read temperature as Celsius (the default)
        float tempf = dht.readTemperature(true);  // Read temperature as Fahrenheit (isFahrenheit = true)
        float dew = 237.7f * (17.27f * tempc/(237.7f + tempc) + log(humidity/100.0f)) / (17.27f - (17.27f * tempc/(237.7f + tempc)+ log(humidity/100.0f)));
        dew = dew * (9.0f/5.0f) + 32.0f; // to fahrenheit
        
        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.print(" %\t");
        Serial.print("Temperature: ");
        Serial.print(tempc);
        Serial.print(" *C ");
        Serial.print(tempf);
        Serial.println(" *F");
        Serial.print("Dew:");
        Serial.println(dew);

        wuString += wuHumid;
        wuString += humidity;
        wuString += wuTemp;
        wuString += tempf;      
        wuString += wuDewpt;
        wuString += dew;
    }

    if (isBmpActive == 1)
    {
        char status;
        double T,P,p0,a;

        status = pressure.startTemperature();

        if (status != 0)
        {
            delay(status);

            status = pressure.getTemperature(T);
            if (status != 0)
            {
                // Print out the measurement:
                Serial.print("temperature: ");
                Serial.print(T,2);
                Serial.print(" deg C, ");
                Serial.print((9.0/5.0)*T+32.0,2);
                Serial.println(" deg F");

                status = pressure.startPressure(3);
                if (status != 0)
                {
                    delay(status);
                    status = pressure.getPressure(P,T);

                    if (status != 0)
                    {
                        // Print out the measurement:
                        Serial.print("absolute pressure: ");
                        Serial.print(P,2);
                        Serial.print(" mb, ");
                        Serial.print(P*0.0295333727,2);
                        Serial.println(" inHg");

                        p0 = pressure.sealevel(P,ALTITUDE);
                        Serial.print("relative (sea-level) pressure: ");
                        Serial.print(p0,2);
                        Serial.print(" mb, ");
                        Serial.print(p0*0.0295333727,2);
                        Serial.println(" inHg");

                        float fp0 = (float)(p0*0.0295333727);
                        float tf  = (9.0f/5.0f)*(float)T+32.0f;

                        wuString += wuBaro;
                        wuString += floatToString(fp0, 2);
                        wuString += wuBaroTemp;
                        wuString += floatToString(tf, 2);
                    }
                    else Serial.println("error retrieving pressure measurement\n");
                }
                else Serial.println("error starting pressure measurement\n");
            }
            else Serial.println("error retrieving temperature measurement\n");
        }
        else Serial.println("error starting temperature measurement\n");
    }

    wuString += wuEnd;
    Serial.println(wuString);

    transmitDataToWeatherUnderground(wuString);
}

int mapVaneDirection()
{
    int vaneValue = analogRead(windvanePin);
    int vaneDirectionDegrees = 0;

    if (vaneValue > 800)        // north
        vaneDirectionDegrees = 0;
    else if (vaneValue > 740)   // north east
        vaneDirectionDegrees = 45;
    else if (vaneValue > 600)   // east
        vaneDirectionDegrees = 90;
    else if (vaneValue > 500)   // north west
        vaneDirectionDegrees = 315;
    else if (vaneValue > 350)   // south east
        vaneDirectionDegrees = 135;
    else if (vaneValue > 200)   // west
        vaneDirectionDegrees = 270;
    else if (vaneValue > 100)   // south west
        vaneDirectionDegrees = 225;
    else                        // south
        vaneDirectionDegrees = 180;

    return vaneDirectionDegrees;
}

void loop()
{
    unsigned long currentTime = millis();

    if ((lastNtpCheckTime + thresholdTimeNtp) <= currentTime)
    {
        lastNtpCheckTime = currentTime;
        checkForNtpUpdate();
    }

    if (reportNewHour())
    {
        //printTime();
        transmitWeatherData();
    }
}

/**
* ISR for rain sensor measurements
*/
void rainInterrupt()
{
    volatile unsigned long currentMillis = millis();

    if ((previousRainInterruptTime + interruptSleep) <= currentMillis)
    {
        toggleLedRed();
        ++rainCounterHourly;
        ++rainCounterDaily;

        previousRainInterruptTime = currentMillis;
    }
}

/**
* ISR for wind sendor measurements
*/
void windInterrupt()
{
    volatile unsigned long currentMillis = millis();
    volatile long diffTime = (currentMillis - previousWindInterruptTime);

    if (diffTime > interruptSleep)
    {
        toggleLedRed();
        ++windCounter;

        if ((previousGustResetTime - currentMillis) < 1000) // if time is less than a second
            ++gustCounter;
        else
        {
            gustCounter = 1;
            previousGustResetTime = currentMillis;
        }

        if (gustCounter > gustMaxRecorded) // update max recorded wind
            gustMaxRecorded = gustCounter;

        previousWindInterruptTime = currentMillis;
    }
}
