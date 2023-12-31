#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "time.h"

// Provide the token generation process info.
#include "addons/TokenHelper.h"
// Provide the RTDB payload printing info and other helper functions.
#include "addons/RTDBHelper.h"

// Define tiap pin yang di pake
#define PIN_Moisture 36
#define PIN_Ntc 34

// set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

//Led Indikator
const int ledGreen = 2;
const int ledRed = 4;

// Insert your network credentials
#define WIFI_SSID "UY"
#define WIFI_PASSWORD "11111111"

// Insert Firebase project API Key
#define API_KEY "AIzaSyDay-6Vlw3eBKwWaFa0mYDMK7rzqCvLFgA"

// Insert Authorized Email and Corresponding Password
#define USER_EMAIL "_" //input email sesuai Authentication di Firebase
#define USER_PASSWORD "_" //input password sesuai Authentication di Firebase

// Insert RTDB URLefine the RTDB URL
#define DATABASE_URL "https://penelitian-gabah-default-rtdb.asia-southeast1.firebasedatabase.app/"
// Define Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Variable to save USER UID
String uid;

// Database main path (to be updated in setup with the user UID)
String databasePath;
// Database child nodes
String tempGrainPath = "/GrainTemperature";
String moisPath = "/GrainMoisture";
String tempEnvPath = "/RoomTemperature";
String timePath = "/timestamp";

// Parent Node (to be updated in every loop)
String parentPath;

int timestamp;
FirebaseJson json;

const char* ntpServer = "pool.ntp.org";

//Moisture sensor
float moisture;

// BME280 sensor
Adafruit_BME280 bme; // I2C
float temperatureEnv;

//NTC Sensor
const double VCC = 3.3; // NodeMCU on board 3.3v vcc
const double R2 = 10000; // 10k ohm series resistor
const double adc_resolution = 1023; // 10-bit adc
double Vout, Rth, adc_value;
double ntc_analog_value;
float temperatureGrain;

const double A = 0.001129148; // thermistor equation parameters
const double B = 0.000234125;
const double C = 0.0000000876741;

// Timer variables (send new readings every three minutes)
unsigned long sendDataPrevMillis = 0;
unsigned long timerDelay = 30000;

// Initialize BME280
void initBME(){
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
    while (1);
  }
}

// Initialize WiFi and LCD display
void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi ..");
  lcd.print("Connecting..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
  lcd.clear();
  lcd.print("Connected");
  Serial.println();
}

void ntcStart(){
  ntc_analog_value = 0;
  int numberoffmeasurements = 200; // do 100 readings for more stable measurement
  for (int i = 0; i < numberoffmeasurements; i++) { // < = smaller i++ = i=i+1
  ntc_analog_value = ntc_analog_value + analogRead(PIN_Ntc);
  delay(1);
  }

  adc_value = ntc_analog_value / numberoffmeasurements; // divide by 100 for more stable measurement
  //temp not jumping up and down
  Vout = (adc_value * VCC) / adc_resolution;
  Rth = (VCC * R2 / Vout) - R2;

  temperatureGrain = (1 / (A + (B * log(Rth)) + (C * pow((log(Rth)),3)))); // Temperature in kelvin
  temperatureGrain = temperatureGrain - 273.15; // Temperture in degree censius
}

void moistureStart(){
  float value = analogRead(PIN_Moisture);
  moisture = abs(map(value, 2500, 4095, 0, 100));

  if(moisture <= 14){
    digitalWrite(ledGreen, HIGH);
    digitalWrite(ledRed, LOW);
    }else{
    digitalWrite(ledRed, HIGH);
    digitalWrite(ledGreen, LOW);
  }
  delay(2000);
  }

void lcdDisplay(){
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TG:");
  lcd.print(temperatureGrain,1);
  lcd.print("C ");

  lcd.print("M:");
  lcd.print(moisture);
  
  lcd.setCursor(0, 1);
  lcd.print("temp:");
  lcd.print(bme.readTemperature());
  lcd.print("C");
}

// Function that gets current epoch time
unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return(0);
  }
  time(&now);
  return now;
}

void setup(){
  Serial.begin(115200);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);

  //initialize LCD
  lcd.init();
  lcd.backlight();


  // Initialize BME280 sensor
  initBME();
  initWiFi();
  configTime(0, 0, ntpServer);

  // Assign the api key (required)
  config.api_key = API_KEY;

  // Assign the user sign in credentials
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Assign the RTDB URL (required)
  config.database_url = DATABASE_URL;

  Firebase.reconnectWiFi(true);
  fbdo.setResponseSize(4096);

  // Assign the callback function for the long running token generation task */
  config.token_status_callback = tokenStatusCallback; //see addons/TokenHelper.h

  // Assign the maximum retry of token generation
  config.max_token_generation_retry = 5;

  // Initialize the library with the Firebase authen and config
  Firebase.begin(&config, &auth);

  // Getting the user UID might take a few seconds
  Serial.println("Getting User UID");
  while ((auth.token.uid) == "") {
    Serial.print('.');
    delay(1000);
  }
  // Print user UID
  uid = auth.token.uid.c_str();
  Serial.print("User UID: ");
  Serial.println(uid);

  // Update database path
  databasePath = "/UsersData/" + uid + "/readings";
}

void loop(){
  ntcStart();
  moistureStart();
  lcdDisplay();

  // Send new readings to database
  if (Firebase.ready() && (millis() - sendDataPrevMillis > timerDelay || sendDataPrevMillis == 0)){
    sendDataPrevMillis = millis();

    //Get current timestamp
    timestamp = getTime();
    Serial.print ("time: ");
    Serial.println (timestamp);

    parentPath= databasePath + "/" + String(timestamp);
    json.set(tempGrainPath.c_str(), String(temperatureGrain));
    json.set(moisPath.c_str(), String(moisture));
    json.set(tempEnvPath.c_str(), String(bme.readTemperature()));
    // json.set(presPath.c_str(), String(bme.readPressure()/100.0F));
    json.set(timePath, String(timestamp));
    Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, parentPath.c_str(), &json) ? "ok" : fbdo.errorReason().c_str());
  }
}