#include <DHT.h>
#include <DHT_U.h>

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

//#define DHTPIN 13         // what digital pin we're connected to
//#define DHTTYPE DHT22     // DHT 22  (AM2302), AM2321
//DHT dht(DHTPIN, DHTTYPE);

// timekeeping
unsigned long lastReportTime = 0;
volatile unsigned long previousWindInterruptTime = 0;
volatile unsigned long previousRainInterruptTime = 0;

// counters used in the ISR's
volatile int windCounter = 0;
volatile int rainCounter = 0;
volatile int vaneDirection = 0;
volatile int gustTime = -1;

// reporting data
volatile int windGust = 0;

volatile boolean redLedState  = true;
volatile boolean blueLedState = false;

const unsigned int interruptSleep = 2;         // milliseconds delay to avoid multiple readings

//const unsigned int reportFeedback = 3600000; // every hour
const unsigned long thousand = 1000;
const unsigned long feedbackSeconds = 5;
const unsigned long reportFeedback = feedbackSeconds * thousand;     // every 10 seconds

//const unsigned long 
const float windSpeed = 0.666667;              // 2.4 km/h == 0.667 m/s
const float rainSpeed = 0.2794;                // 0.2794 mm precipitation on every bucket emptying

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
  //pinMode(DHTPIN, INPUT_PULLUP);

  //dht.begin();

  lastReportTime = millis();
  
  attachInterrupt(anemometerPin, windInterrupt, FALLING);
  attachInterrupt(precipitationPin, rainInterrupt, FALLING);

  toggleLedRed();
  toggleLedBlue();
}

void transmitWeatherData()
{
  // wind
 //Wire.write(windCounter);

  // wind gust
 // Wire.write(windGust);
  
  // wind vane
 // Wire.write(vaneDirection);
  
  // rain
 // Wire.write(rainCounter);
}

/**
* Resets all counters used in the weather station
*/
void resetCounters()
{
    rainCounter = 0;
    windCounter = 0;
    windGust = 0.0f;
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

    // rain
    float rainAmount = rainSpeed * (float)rainCounter;

    // wind
    float windAmount = (windSpeed * (((float)windCounter) / 2.0f) / (float)feedbackSeconds);

    // wind gust
    windGust = (int)((windSpeed * (1000.0f / (float)gustTime)) * 1000.0f);

    // wind direction
    vaneDirection = mapVaneDirection();

    if (isWindvaneActive == 1)
    {
        Serial.print("Wane Dir: ");
        Serial.println(vaneDirection);
    }

    if (isAnemometerActive == 1)
    {
        Serial.print("Wind: ");
        Serial.print(windAmount);
        Serial.println(" ms");
    }

    if (isRainmeterActive == 1)
    {
        Serial.print("Rain: ");
        Serial.print(rainAmount);
        Serial.println(" mm");
    }

    resetCounters();

//    if (isDhtActive == 1)
//    {
//      float h = dht.readHumidity();
//      // Read temperature as Celsius (the default)
//      float t = dht.readTemperature();
//      // Read temperature as Fahrenheit (isFahrenheit = true)
//      float f = dht.readTemperature(true);
//
//      // Check if any reads failed and exit early (to try again).
//      // if (isnan(h) || isnan(t) || isnan(f)) {
//      //    Serial.println("Failed to read from DHT sensor!");
//      //    return;
//      // }
//
//      // Compute heat index in Fahrenheit (the default)
//      float hif = dht.computeHeatIndex(f, h);
//      // Compute heat index in Celsius (isFahreheit = false)
//      float hic = dht.computeHeatIndex(t, h, false);
//
//      Serial.print("Humidity: ");
//      Serial.print(h);
//      Serial.print(" %\t");
//      Serial.print("Temperature: ");
//      Serial.print(t);
//      Serial.print(" *C ");
//      Serial.print(f);
//      Serial.print(" *F\t");
//      Serial.print("Heat index: ");
//      Serial.print(hic);
//      Serial.print(" *C ");
//      Serial.print(hif);
//      Serial.println(" *F");
//    }
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
      if (gustTime < 0 || gustTime > diffTime)
        gustTime = diffTime;
        
      previousWindInterruptTime = currentMillis;
  }
}
