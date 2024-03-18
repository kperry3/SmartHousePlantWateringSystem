/*
 * Project Smart Houseplan Watering System
 * Author: Kathryn Perry
 * Date: March 17, 2024
 */

// Include Particle Device OS APIs
#include "Adafruit_BME280.h"
#include "Adafruit_MQTT/Adafruit_MQTT_SPARK.h"
#include "Air_Quality_Sensor.h"
#include "Particle.h"
#include "credentials.h"
#include "math.h"
#include "Adafruit_SSD1306.h"

const int BME280ADDR = 0x76;
const int DEGREESYM = 0xF8;
const int AQPIN = A0;
const int SOILMOISTPIN = A2;
const int DUSTPIN = D3;
const int RELAYPIN = D4;
const int OLEDADDR = 0x3C;
const int OLEDRESET = -1;
const int BUTTONPIN = D12;

// Declare variables
float temperature, humidity, soilMoisture;
int aqLevel, aqIndex;
unsigned long lowpulseoccupancy = 0;
float concentration = 0;
float ratio = 0;
unsigned int last, lastTime, waterStartTime;
unsigned int updateTime = 15000;
unsigned int waterTime = 500;
int subValue;
String message;
bool watering = false;

Adafruit_BME280 bme;
AirQualitySensor aqSensor(AQPIN);
Adafruit_SSD1306 display(OLEDRESET);

/************ Global State (you don't need to change this!) *** ***************/
TCPClient TheClient;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and
// login details.
Adafruit_MQTT_SPARK mqtt(&TheClient, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/
// Setup Feeds to publish or subscribe
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Subscribe manualFeed = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/waterbutton");
Adafruit_MQTT_Publish aqFeed = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/aqsensorfeed");
Adafruit_MQTT_Publish dustFeed =  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/dustsensorfeed");
Adafruit_MQTT_Publish tempFeed =  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/bme289temp");
Adafruit_MQTT_Publish humidityFeed =  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/humidityfeed");
Adafruit_MQTT_Publish moistureFeed =  Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/moisturefeed");

/************Declare Functions*************/
void MQTT_connect();
bool MQTT_ping();
void getConc() ;

// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

void setup() {
    // Initialize Serial port for dubugging purposes
    Serial.begin(9600);
    waitFor(Serial.isConnected, 10000);

    // Initialize Relay
    pinMode(RELAYPIN, OUTPUT);
    digitalWrite(RELAYPIN, LOW); // Make sure pump is off

    // Connect to the internet
    WiFi.on();
    WiFi.connect();
    while (WiFi.connecting()) {
        Serial.printf(".");
    }
    Serial.printf("\n\n\n");

    // Initialize BME280
    if (!bme.begin(BME280ADDR)) {
        Serial.printf("BME280 at address 0x%02x failed to initialize\n\n",  BME280ADDR);
    }

    // Initialize AQ
    aqSensor.init();

    // Initialize Moisture
    pinMode(SOILMOISTPIN, INPUT);

    // Initialize Dust
    pinMode(DUSTPIN, INPUT);
   // new Thread("concThread", getConc);

    // Initialize OLED
     display.begin(SSD1306_SWITCHCAPVCC, OLEDADDR);
     display.clearDisplay();   

     // Initialize button
     pinMode(BUTTONPIN, INPUT);


    // Setup MQTT subscription
    mqtt.subscribe(&manualFeed);
}

void loop() {
    MQTT_connect();
    MQTT_ping();

  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(100))) {
    if (subscription == &manualFeed) {
      subValue = atoi((char *)manualFeed.lastread);
      Serial.printf("Water button value: %d\n", subValue);
        if(subValue ==  HIGH){
          // Water plant for 500ms
          digitalWrite(RELAYPIN, HIGH);
          watering = true;
          waterStartTime = millis();
        }
    }
  } 

    if (((millis()-waterStartTime) > waterTime) && (watering)) {  
      // Shut pump off
      digitalWrite(RELAYPIN, LOW);
      watering = false;
      Serial.printf("Turning pump off!!\n\n");
    }
  
    // Get readings and Publish data
    if ((millis()-lastTime) > updateTime) {
 
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0,0);
        display.clearDisplay();
        display.display();

       // Get Temperature
        temperature = bme.readTemperature() * 9/5 + 32;
        Serial.printf("Temperature: %0.2f%cF\n", temperature, DEGREESYM);
        display.printf("Temp: %.02f˚F\n", temperature);
        display.display();

        // Get Humidity
        humidity = bme.readHumidity();
        Serial.printf("Humidity: %0.2f\n", humidity);
        display.printf("Humidity: %0.2f\n", humidity);
        display.display();


        // Get Air Quality
        aqLevel = aqSensor.slope();
        aqIndex = aqSensor.getValue();

        if (aqLevel == AirQualitySensor::FORCE_SIGNAL) {
            // Display High Pollution
            message = "Air: High Pollution\n";
        }    else if (aqLevel == AirQualitySensor::HIGH_POLLUTION) {
            // Display High Pollution
            message = "Air: High Pollution\n";
        } else if (aqLevel == AirQualitySensor::LOW_POLLUTION) {
            // Display Low Pollution
            message = "Air: Low Pollution\n";
        } else if (aqLevel == AirQualitySensor::FRESH_AIR) {
            // Display Fresh Air
            message = "Air: Fresh\n";
        }
        display.printf("%s", message.c_str());
        display.display();
        Serial.printf("%s", message.c_str());
    

        // Get Moisture Level
        soilMoisture = analogRead(SOILMOISTPIN);
        if((soilMoisture > 3200) && !watering){
            Serial.printf("Turning pump on!!\n\n");
            digitalWrite(RELAYPIN, HIGH);
            waterStartTime = millis();    
            watering = true; 
       }
        display.printf("Moisture: %0.2f\n", soilMoisture);
        display.display();
        Serial.printf("Moisture: %0.2f\n", soilMoisture);

        display.printf("Dust: %0.2f\n", concentration);
        display.display();
        Serial.printf("Dust: %0.2f\n\n\n", concentration);
 
         if(mqtt.Update()) {
            dustFeed.publish(concentration);
            aqFeed.publish(aqIndex);
            humidityFeed.publish(humidity);
            tempFeed.publish(temperature);
            moistureFeed.publish(soilMoisture);
        }       
       lastTime = millis();
    } 
 }

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
    int8_t ret;

    // Return if already connected.
    if (mqtt.connected()) {
        return;
    }

    Serial.print("Connecting to MQTT... ");

    while ((ret = mqtt.connect()) !=  0) {  // connect will return 0 for connected
        Serial.printf("Error Code %s\n", mqtt.connectErrorString(ret));
        Serial.printf("Retrying MQTT connection in 5 seconds...\n");
        mqtt.disconnect();
        delay(5000);  // wait 5 seconds and try again
    }
    Serial.printf("MQTT Connected!\n");
}

bool MQTT_ping() {
    static unsigned int last;
    bool pingStatus;

    if ((millis() - last) > 120000) {
        Serial.printf("Pinging MQTT \n");
        pingStatus = mqtt.ping();
        if (!pingStatus) {
            Serial.printf("Disconnecting \n");
            mqtt.disconnect();
        }
        last = millis();
    }
    return pingStatus;
}

void getConc() {
    const int sampleTime = 30000;
    unsigned int duration, startTime;
    startTime = 0;
    lowpulseoccupancy = 0;
    while (true) {  // Run the below loop forever
        duration = pulseIn(DUSTPIN, LOW);
        lowpulseoccupancy = lowpulseoccupancy + duration;
        if ((millis() - startTime) > sampleTime) {
            ratio = lowpulseoccupancy / (sampleTime * 10.0);
            concentration = 1.1 * pow(ratio, 3) - 3.8 * pow(ratio, 2) + 520 * ratio + 0.62;
           // Serial.printf("Dust Concentration: %.02f\n", concentration);
            startTime = millis();
            lowpulseoccupancy = 0;
        }
    }
}
