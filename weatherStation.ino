#include <DHT.h>
#include <DHT_U.h>
#include <math.h>

// enable / disable subsystems
const int isAnemometerActive = 1;
const int isRainmeterActive = 1;
const int isWindvaneActive = 1;
const int isDhtActive = 1;
const int isBmpActive = 0;

// direction for vane
// S, SW, W, NW, N, NE, E, SE
const int windVaneOffset = 0;

// pin definitions
const int anemometerPin = 4;
const int precipitationPin = 5;
const int windvanePin = A0;
const int redLedPin = 0;
const int blueLedPin = 2;

#define DHTPIN 12         // what digital pin we're connected to
#define DHTTYPE DHT22     // DHT 22  (AM2302), AM2321
DHT dht(DHTPIN, DHTTYPE);

// timekeeping
unsigned long lastReportTime = 0;
volatile unsigned long previousWindInterruptTime = 0;
volatile unsigned long previousRainInterruptTime = 0;

// counters used in the ISR's
volatile int windCounter = 0;
volatile int rainCounter = 0;
volatile int vaneDirection = 0;
volatile int gustTime = 0;

volatile boolean redLedState  = true;
volatile boolean blueLedState = false;

const unsigned int interruptSleep = 2;         // milliseconds delay to avoid multiple readings

const unsigned long thousand = 1000;
const unsigned long feedbackSeconds = 10;
const unsigned long reportFeedback = feedbackSeconds * thousand;     // every 10 seconds

//const unsigned long 
const float windSpeed = 0.666667f;                  // 2.4 km/h == 0.667 m/s
const float windSpeedMph = 1.4913;                  // 2.4 km/h == 0.667 m/s == 1.4913 m/h
const float rainSpeedInch = 0.0011f;                // 0.2794 mm precipitation on every bucket emptying
const float rainSpeedMm   = 0.2794f;                // 0.2794 mm precipitation on every bucket emptying

const String wuBegin = "https://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?ID=IHORDALA133&PASSWORD=MXLVDW6X&dateutc=now";
const String wuEnd = "&action=updateraw";

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

void toggleLedBlue()
{
    digitalWrite(blueLedPin, blueLedState);
    blueLedState = !blueLedState;
}

void setup()
{
  Serial.begin(9600);

  pinMode(redLedPin, OUTPUT);
  pinMode(redLedPin, redLedState);
  pinMode(blueLedPin, OUTPUT);
  pinMode(blueLedPin, blueLedState);
  
  digitalWrite(anemometerPin, HIGH);
  pinMode(windvanePin, INPUT);
  pinMode(anemometerPin, INPUT_PULLUP);
  pinMode(precipitationPin, INPUT_PULLUP);
  pinMode(DHTPIN, INPUT_PULLUP);

  dht.begin();

  lastReportTime = millis();
  
  attachInterrupt(anemometerPin, windInterrupt, FALLING);
  attachInterrupt(precipitationPin, rainInterrupt, FALLING);

  toggleLedRed();
  toggleLedBlue();
}

void transmitWeatherData()
{
  String wuString = wuBegin;
  
  const String wuTemp = "&tempf=";      // ok
  const String wuHumid = "&humidity=";  // ok
  const String wuDewpt = "&dewptf=";
  const String wuRain = "&rainin=";     // ok 60 minutes
  const String wuBaro = "&baromin=";
  const String wuWinddir = "&winddir="; // ok
  const String wuWindspeed = "&windspeedmph="; // ok
  // windgustmph_10m - [mph past 10 minutes wind gust mph ]

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
        float windGustMph = (windSpeedMph * (500.0f / (float)gustTime)); // 2 rev per sec == 0.66667 m/s

        if (gustTime == 0) // check if no wind at all
            windGustMph = 0;

        Serial.print("WindGust: ");
        Serial.print(windGustMph);
        Serial.println(" mph");

        Serial.print("WindSpeed: ");
        Serial.print(windAmountMph);
        Serial.println(" mph");

        Serial.print("Wind: ");
        Serial.print(windAmount);
        Serial.println(" ms");

        wuString += wuWindspeed;
        wuString += floatToString(windAmountMph, 4);
    }

    if (isRainmeterActive == 1)
    {
        float rainAmountInch  = rainSpeedInch  * (float)rainCounter;
        float rainAmountMm    = rainSpeedMm    * (float)rainCounter;
        Serial.print("Rain mm: ");
        Serial.print(floatToString(rainAmountMm, 4));
        Serial.print("\tRain Inches: ");
        Serial.println(floatToString(rainAmountInch, 4));
        wuString += wuRain;
        wuString += floatToString(rainAmountInch, 4);
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

    wuString += wuEnd;
    Serial.println(wuString);
}

/**
* Resets all counters used in the weather station
*/
void resetCounters()
{
    rainCounter = 0;
    windCounter = 0;
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
  // check to see if we should report anything back
  unsigned long currentTime = millis();
  
  if ((lastReportTime + reportFeedback) <= currentTime)
  {  
    toggleLedBlue();
    
    // update last report time to current
    lastReportTime = currentTime;

    transmitWeatherData();
    resetCounters();
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
      ++rainCounter;
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

      if (gustTime == 0 || gustTime > diffTime) // recorded gust is longer than what is now
        gustTime = diffTime;                    // time between each sampled gusts of wind

      previousWindInterruptTime = currentMillis;
  }
}
