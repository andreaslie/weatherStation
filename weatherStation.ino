 #include <Wire.h>

// enable / disable subsystems
const int isAnemometerActive = 1;
const int isRainmeterActive = 0;
const int isWindvaneActive = 0;

// direction for vane
// S, SW, W, NW, N, NE, E, SE
const int windVaneOffset = 0;

// pin definitions
const int anemometerPin = 2;
const int ledPin = 6;
const int precipitationPin = 3;
const int windvanePin = 38;

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

const unsigned int interruptSleep = 5;         // milliseconds delay to avoid multiple readings

//const unsigned int reportFeedback = 3600000; // every hour
const unsigned long thousand = 1000;
const unsigned long feedbackSeconds = 10;
const unsigned long reportFeedback = feedbackSeconds * thousand;     // every 10 seconds

//const unsigned long 
const float windSpeed = 0.666667;              // 2.4 km/h == 0.667 m/s
const float rainSpeed = 0.2794;                 // 0.2794 mm precipitation on every bucket emptying

void setup()
{
  Serial.begin(9600);
  Wire.begin(0x42);        // join i2c bus (address optional for master)
  Wire.onRequest(transmitWeatherData);
  
  Keyboard.begin();
  pinMode(ledPin, OUTPUT);

  digitalWrite(anemometerPin, HIGH);
  pinMode(anemometerPin, INPUT_PULLUP);
  pinMode(precipitationPin, INPUT_PULLUP);
    
  lastReportTime = millis();
  
  attachInterrupt(2, windInterrupt, FALLING);
  attachInterrupt(3, rainInterrupt, FALLING);
  
  digitalWrite(ledPin, HIGH);
}

void transmitWeatherData()
{
  // wind
  Wire.write(windCounter);

  // wind gust
  Wire.write(windGust);
  
  // wind vane
  Wire.write(vaneDirection);
  
  // rain
  Wire.write(rainCounter);
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
  int vaneDirectionRaw = 0;

  if (vaneValue > 800)        // north
    vaneDirectionRaw = 4;
  else if (vaneValue > 750)   // north east
    vaneDirectionRaw = 5;
  else if (vaneValue > 600)   // east
    vaneDirectionRaw = 6;
  else if (vaneValue > 400)   // north west
    vaneDirectionRaw = 3;
  else if (vaneValue > 250)   // south east
    vaneDirectionRaw = 7;
  else if (vaneValue > 140)   // west
    vaneDirectionRaw = 2;
  else if (vaneValue > 80)    // north east
    vaneDirectionRaw = 1;
  else if (vaneValue > 35)    // south
    vaneDirectionRaw = 0;
  
  return (vaneDirectionRaw + windVaneOffset) % 8;
}

void loop()
{
  // check to see if we should report anything back
  unsigned long currentTime = millis();
  
  if ((lastReportTime + reportFeedback) <= currentTime)
  {
    // update last report time to current
    lastReportTime = currentTime;
    
    // rain
    float rainAmount = rainSpeed * (float)rainCounter;
    
    // wind
    float windAmount = (windSpeed * (float)windCounter) / (float)feedbackSeconds;

    // wind gust
    windGust = (int)((windSpeed * (1000.0f / (float)gustTime)) * 1000.0f);
    
    // wind direction
    vaneDirection = mapVaneDirection();
    
    if (isWindvaneActive == 1)
        Keyboard.println(vaneDirection);
        
    if (isAnemometerActive == 1)
        Keyboard.println(windCounter);
        
    if (isRainmeterActive == 1)
        Keyboard.println(rainCounter);
        
//    Keyboard.print(windAmount);
//    Keyboard.println(" ms");
//    Keyboard.print(rainAmount);
//    Keyboard.println(" mm");
  
    //resetCounters();    
  }
 
  if (windCounter > 5)
  {
    digitalWrite(ledPin, LOW);
  }
  
//delay(100);  // do not print too fast!
}

/**0

* ISR for rain sensor measurements
*/
void rainInterrupt()
{
  volatile unsigned long currentMillis = millis();
  
  if ((previousRainInterruptTime + interruptSleep) <= currentMillis)
  {
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
      ++windCounter;
      if (gustTime < 0 || gustTime > diffTime)
        gustTime = diffTime;
        
      previousWindInterruptTime = currentMillis;
  }
}
